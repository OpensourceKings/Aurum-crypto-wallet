<div align="center">

# 🪙 Aurum

**A native Linux desktop crypto wallet — C++20, Qt 6, zero Electron.**

![version](https://img.shields.io/badge/version-0.7.2-gold) ![license](https://img.shields.io/badge/license-MIT-blue) ![platform](https://img.shields.io/badge/platform-Linux-lightgrey) ![C++](https://img.shields.io/badge/C%2B%2B-20-informational) ![status](https://img.shields.io/badge/status-experimental-orange)

</div>

> ⚠️ **Experimental software, not independently audited.** Do not use it as the sole protection for money you cannot afford to lose. See [SECURITY.md](SECURITY.md) and [TESTING.md](TESTING.md).

## ✨ Features

- 🌐 **7 networks, 11 assets** — BTC, LTC, SOL, ETH, POL, BNB, XMR, plus USDT/USDC on Ethereum and Solana
- 🔐 **Encrypted storage** — Argon2id + XChaCha20-Poly1305, guarded and locked secret memory
- 🧱 **Isolated signer** — a short-lived, seccomp-restricted process; the GUI never decrypts private keys
- 🕵️ **Privacy-minded** — Monero over Tor, optional Mullvad integration, no analytics, no cloud key storage
- 🎨 **11 appearances** — switch themes live from the header
- 👛 **Multiple wallet profiles** — separate encrypted profiles with their own addresses
- 🧪 **Offline demo mode** — `./build/aurum --demo` with public test keys

## 🚀 Quick start

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
./build/aurum --demo
```

Requires Qt 6, OpenSSL, libsodium, libsecp256k1, Crypto++, libqrencode, CMake and a C++20 compiler. Full details in the [GUIDE](GUIDE.md).

## 📚 Docs

| 📄 | |
|---|---|
| [GUIDE.md](GUIDE.md) | Usage, assets, connections, build |
| [SECURITY.md](SECURITY.md) | Threat model and protections |
| [TESTING.md](TESTING.md) | Test coverage and history |
| [ELECTRUM.md](ELECTRUM.md) | Litecoin backend |
| [MULLVAD.md](MULLVAD.md) | VPN → Tor routing |
| [THIRD_PARTY.md](THIRD_PARTY.md) | Dependency licenses |

## ⚠️ Known issue

Monero sync over Tor through public nodes can time out; XMR send/receive readiness is not established in this snapshot.

## 📜 License

[MIT](LICENSE). Third-party components keep their own licenses.

## Swaps, catalog and Windows (unverified build)

- **Swap**: All assets → Swap. Uses Trocador; set `AURUM_TROCADOR_API_KEY`. Endpoints were written without access to the (browser-gated) API docs; verify with a live key.
- **Catalog**: `src/catalog.cpp` lists 107 swap-only coins. No keys or addresses are derived for them; the user supplies the payout address.
- **Windows**: signer runs via `QProcess` with a job-object sandbox. Monero is disabled on Windows, and the seccomp/`mlock` hardening is replaced by weaker Windows equivalents. This path has not been compiled.
