# Dependencies and artwork

The source uses Qt 6, OpenSSL, libsodium, libsecp256k1, Crypto++, and libqrencode. System libraries remain subject to their own licenses. The Fedora test build used Qt 6.11.2, OpenSSL 3.5.9, libsodium 1.0.22, libsecp256k1 0.6.0, Crypto++ 8.8.0, and libqrencode 4.1.1.

The binary bundle ships dynamically linked, unmodified Fedora builds of **libsecp256k1** (MIT) and **Crypto++** (public domain with license exceptions described in its notice). Their license texts are included. It uses the host's remaining runtime libraries and Qt SVG plugin; those are not bundled.

Project sources:

- https://github.com/bitcoin-core/secp256k1
- https://github.com/weidai11/cryptopp
- https://github.com/jedisct1/libsodium
- https://www.openssl.org/source/
- https://www.qt.io/download-open-source
- https://github.com/fukuchi/libqrencode

Artwork under `assets/coins` comes from [Cryptocurrency Icons](https://github.com/spothq/cryptocurrency-icons) under CC0. Artwork under `assets/ui` comes from [Lucide](https://github.com/lucide-icons/lucide) under ISC. These are upstream logos/icons, not original coin artwork claimed by this project. License texts are retained in `licenses/`. Network names and marks belong to their respective owners. No endorsement is implied.

Optional integration test packages have separate licenses distributed by their upstream projects and package manager. They are neither bundled into nor required by the wallet executable.

The Aurum app icon (`assets/aurum.svg`) is original vector artwork. The Exodus-inspired appearance uses an original implementation of the user-supplied asset-detail layout; it includes no proprietary Exodus logo, artwork or source. Electrum-LTC was reviewed for protocol and validation behavior; its Python source is not shipped or executed.

Roboto and Roboto Condensed (Copyright 2011 Google Inc.) are bundled under Apache 2.0. The unmodified WOFF versions match those shipped by the official Exodus desktop package. Their license is in `licenses/Roboto-Apache-2.0.txt`; provenance is in `assets/fonts/NOTICE.txt` and `EXODUS-REFERENCE.md`. All font files are embedded into the Qt resources, with no font download at runtime.
