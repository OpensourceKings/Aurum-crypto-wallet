# Aurum

A native desktop cryptocurrency wallet written in C++20 and Qt 6. No Electron, no webview, no cloud key storage, no analytics.

[![CI](https://github.com/OpensourceKings/Aurum-crypto-wallet/actions/workflows/ci.yml/badge.svg)](https://github.com/OpensourceKings/Aurum-crypto-wallet/actions/workflows/ci.yml)
![Version](https://img.shields.io/badge/version-0.7.2-blue)
![License](https://img.shields.io/badge/license-MIT-green)
![Status](https://img.shields.io/badge/status-experimental-orange)

> **Experimental software. Not independently audited.**
> Do not use it as the sole protection for funds you cannot afford to lose. See [SECURITY.md](SECURITY.md) for what is and is not defended against.

## Contents

- [Overview](#overview)
- [Supported assets](#supported-assets)
- [Architecture and security design](#architecture-and-security-design)
- [Build](#build)
- [Usage](#usage)
- [Platform support](#platform-support)
- [Verification status](#verification-status)
- [Known limitations](#known-limitations)
- [Documentation](#documentation)
- [License](#license)

## Overview

| | |
|---|---|
| Language / toolchain | C++20, Qt 6.4+, CMake 3.22+ |
| Key storage | Local encrypted vault; Argon2id + XChaCha20-Poly1305 |
| Signing | Isolated short-lived helper process (`aurum-signer`) |
| Networking | Public explorers/RPC; optional Tor and Mullvad routing |
| UI | 11 switchable appearances, multiple wallet profiles |
| Demo | Offline mode with public test keys: `aurum --demo` |

## Supported assets

Eleven assets across seven networks, with full key derivation, receive, and send support:

| Network | Assets |
|---|---|
| Bitcoin | BTC |
| Litecoin | LTC |
| Solana | SOL, USDT, USDC (SPL) |
| Ethereum | ETH, USDT, USDC (ERC-20) |
| Polygon | POL |
| BNB Chain | BNB |
| Monero | XMR (via upstream `monero-wallet-rpc`) |

**Swaps.** A built-in swap screen routes through [Trocador](https://trocador.app). It requires an API key in `AURUM_TROCADOR_API_KEY`. A catalog of 107 additional coins (`src/catalog.cpp`) can be selected as swap *destinations*; Aurum derives no keys or addresses for them, and the user supplies the payout address.

## Architecture and security design

- **Vault.** The wallet file is 160 bytes: a fresh 16-byte salt and 24-byte nonce, with the key derived by Argon2id (libsodium moderate parameters: 3 passes, 256 MiB) and authenticated by XChaCha20-Poly1305. No private key is stored in plaintext.
- **Isolated signer.** Key creation, restore, address derivation and signing run in a separate `aurum-signer` process. The GUI never decrypts a private key. The password is passed over an anonymous local socket pair from locked memory, never through arguments, environment, or temporary files. The helper performs exactly one operation and exits.
- **Syscall sandbox (Linux).** Before reading the password, the signer installs a seccomp allowlist that denies filesystem opens, sockets, process creation and ptrace.
- **Memory handling.** Secrets live in libsodium guarded allocations that are locked, excluded from core dumps, zeroed on release, and made read-only after construction. Core dumps and ptrace attachment are disabled for the main process.
- **Constrained signing.** The signer accepts only supported transfer formats (BTC/LTC transfers, EVM native and ERC-20 transfers, native SOL/SPL transfers). Token approvals, arbitrary contract calls and unexpected Solana instructions are rejected.
- **Network hygiene.** TLS verification is on, cross-origin redirects are refused, and requests have size and time limits.
- **Monero.** Handled by the signed upstream `monero-wallet-rpc` rather than Aurum's own vault. The default route is a public onion node over local Tor; Aurum does not silently fall back to clearnet.

The full threat model, including what is explicitly out of scope, is in [SECURITY.md](SECURITY.md).

## Build

**Dependencies:** Qt 6.4+ (Core, Gui, Widgets, Network, WebSockets, Svg, Test), OpenSSL, libsodium, libsecp256k1 (with the recovery module), Crypto++, libqrencode, CMake, and a C++20 compiler.

Debian / Ubuntu:

```sh
sudo apt-get install cmake ninja-build g++ qt6-base-dev qt6-websockets-dev qt6-svg-dev \
  libqt6svg6 libssl-dev libcrypto++-dev libsodium-dev libsecp256k1-dev libqrencode-dev
```

Configure, build, test:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

Run the offline demo (no network, public test keys only):

```sh
./build/aurum --demo
```

## Usage

1. Start Aurum and create or restore an encrypted wallet, or open an existing profile.
2. Use **Receive** to show an address and QR code, and **Send** for a reviewed, password-confirmed transfer.
3. Use **All assets → Swap** to quote and create a Trocador swap, then send the deposit from the wallet.
4. Optional: configure Tor and Mullvad routing under **Connection settings**.

See [GUIDE.md](GUIDE.md) for the complete walkthrough.

## Platform support

| Platform | Status | Notes |
|---|---|---|
| Linux x86-64 | Supported | Full hardening: seccomp, `PR_SET_DUMPABLE`, locked memory. Built and tested in CI. |
| Windows x86-64 | **Experimental** | Builds with MSYS2/MinGW in CI. The signer runs under a job-object sandbox. Monero is disabled. Hardening is weaker than on Linux: no seccomp equivalent, and no dump-exclusion for secret pages. |
| macOS | Not supported | |

Releases are produced automatically: when the version in `CMakeLists.txt` has no matching `v*` tag on `main`, the release workflow builds, tests, tags and publishes artifacts with checksums.

## Verification status

- **Automated:** unit, integration and UI suites run in CI (signer, security controls, core transaction vectors, market data, Electrum, wallet profiles, UI). Historical results and their scope are recorded in [TESTING.md](TESTING.md).
- **Manual mainnet check:** sending and receiving were verified by the maintainers on **Litecoin and Solana**, each with a balance above US$50.
- **Not verified:** the Trocador integration (the endpoints were implemented without access to the provider's browser-gated documentation and have not been exercised with a live key) and the Windows build path.
- **Not performed:** an external security audit, a fuzzing campaign, formal verification, and hardware-wallet integration.

## Known limitations

- **Monero** synchronization through public nodes over Tor can time out. XMR send/receive readiness is not established in this snapshot.
- Remote nodes and public APIs learn your addresses and IP address, and may fail or return false data.
- Process protections do not defend against root, a compromised kernel, physical memory capture, or same-user malware (keyloggers, clipboard replacement, replaced binaries).
- Windows builds have weaker isolation than Linux builds (see above).

## Documentation

| Document | Purpose |
|---|---|
| [GUIDE.md](GUIDE.md) | Usage, assets, connections, build |
| [SECURITY.md](SECURITY.md) | Threat model and protections |
| [TESTING.md](TESTING.md) | Test coverage and validation history |
| [ELECTRUM.md](ELECTRUM.md) | Litecoin backend |
| [MULLVAD.md](MULLVAD.md) | VPN to Tor routing |
| [MARKET-DATA.md](MARKET-DATA.md) | Price and chart data |
| [CONNECTION-TROUBLESHOOTING.md](CONNECTION-TROUBLESHOOTING.md) | Network issues |
| [THIRD_PARTY.md](THIRD_PARTY.md) | Dependency licenses |

Do not include a password, wallet file, secret key or recovery material in a bug report.

## License

Released under the [MIT License](LICENSE). Third-party components retain their own licenses; see [THIRD_PARTY.md](THIRD_PARTY.md).
