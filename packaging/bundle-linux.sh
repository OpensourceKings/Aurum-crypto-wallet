#!/usr/bin/env bash
# Bundle non-system shared libraries and Qt plugins into an installed tree so
# the package runs on distributions that lack the exact dependency versions.
# Usage: bundle-linux.sh <install-root>   (contains bin/aurum, bin/aurum-signer)
set -euo pipefail
root=$(realpath "$1")
lib="$root/lib"
plugins="$root/plugins"
mkdir -p "$lib" "$plugins"

# Provided by every desktop Linux system; bundling these breaks more than it fixes.
exclude='^(linux-vdso|ld-linux|libc\.|libm\.|libdl\.|libpthread\.|librt\.|libresolv\.|libutil\.|libstdc\+\+|libgcc_s|libGL|libEGL|libOpenGL|libGLX|libGLdispatch|libgbm|libdrm|libX|libxcb|libxkb|libwayland|libfontconfig|libfreetype|libharfbuzz|libdbus|libglib|libgobject|libgio|libgmodule|libsystemd|libz\.|libexpat|libpng)'

qtplugins=$(qtpaths6 --plugin-dir 2>/dev/null || true)
[ -d "$qtplugins" ] || qtplugins=/usr/lib/x86_64-linux-gnu/qt6/plugins
for d in platforms platformthemes imageformats iconengines xcbglintegrations tls styles; do
  [ -d "$qtplugins/$d" ] && cp -rL "$qtplugins/$d" "$plugins/"
done

cat > "$root/bin/qt.conf" <<'CONF'
[Paths]
Prefix = ..
Plugins = plugins
Libraries = lib
CONF

declare -A seen
queue=("$root/bin/aurum" "$root/bin/aurum-signer")
while read -r f; do queue+=("$f"); done < <(find "$plugins" -name '*.so')
while [ ${#queue[@]} -gt 0 ]; do
  f=${queue[0]}; queue=("${queue[@]:1}")
  [ -n "${seen[$f]:-}" ] && continue
  seen[$f]=1
  while read -r dep; do
    base=$(basename "$dep")
    [[ $base =~ $exclude ]] && continue
    if [ ! -e "$lib/$base" ]; then
      cp -L "$dep" "$lib/$base"
      queue+=("$lib/$base")
    fi
  done < <(ldd "$f" 2>/dev/null | awk '/=> \// {print $3}')
done

# RPATH (not RUNPATH) is not transitive, so give every bundled object its own.
find "$lib" -type f -name '*.so*' -exec patchelf --set-rpath '$ORIGIN' {} \;
find "$plugins" -type f -name '*.so' -exec patchelf --set-rpath '$ORIGIN/../../lib' {} \;
echo "Bundled $(ls "$lib" | wc -l) libraries and $(find "$plugins" -name '*.so' | wc -l) plugins"
