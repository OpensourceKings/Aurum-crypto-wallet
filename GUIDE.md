# Aurum 0.7.2

Native Linux desktop wallet in C++20 and Qt 6. Eleven appearances, seven networks, eleven assets. No Python, Electron, webview, cloud key storage, or analytics.

**Experimental software, not independently audited.** The tests below demonstrate transaction formats and several security controls. They do not establish that this is safe for real funds. Do not use this build as the sole protection for money you cannot afford to lose.

## Open-source snapshot

This source snapshot is distributed under the [MIT license](LICENSE), with
third-party assets and libraries retaining their own licenses as described in
[THIRD_PARTY.md](THIRD_PARTY.md). It includes the C++ wallet, tests, fonts,
icons, and dependency notices. It excludes user wallets, Monero runtime
databases, credentials, local configuration, test output, and compiled binaries.

**Known issue:** Monero synchronization through public nodes over Tor has
produced startup and RPC timeouts. The current source contains changes under
development for that problem. A complete live synchronization and reopening
test has not passed on this final snapshot; XMR send/receive readiness is not
established. Historical test results in `TESTING.md` apply to the versions
identified there and are not certification of this snapshot.

The source ZIP is a development snapshot labeled 0.7.2, rather than a verified
production release. Build and review it before distributing executables.

## Run

For this source distribution, build using the instructions below and run
`./build/aurum --demo` for the offline preview. `./build/aurum` opens the wallet.
Keep `aurum-signer` and `libaurum-monero-hardener.so` beside the application
binary. It requires Linux with seccomp and close_range support and the runtime
libraries listed below. No precompiled `Aurum-Linux` bundle is included in this
source ZIP.

Copper is the default appearance for new settings. Use **Appearance** (the palette control in the header, or the sidebar item) to switch between all eleven designs. Your selection persists after restart; the wallet and addresses are unchanged.

The launcher removes common library/plugin injection environment variables. Start it as your normal user. It needs neither sudo nor root. A second live instance is refused.

The demo is disconnected from networks and uses **public test keys**. Its addresses must never receive real funds. Screenshots contain demo data, not a funded wallet.

## Assets and scope

| Asset | Network | Implemented transfers |
|---|---|---|
| BTC | Bitcoin mainnet | Native SegWit P2WPKH inputs; legacy, SegWit and Taproot destinations |
| LTC | Litecoin mainnet | Native SegWit P2WPKH inputs; standard Litecoin destinations |
| SOL | Solana mainnet-beta | System Program transfer |
| ETH | Ethereum mainnet, chain ID 1 | EIP-155 signed native transfer |
| POL | Polygon mainnet, chain ID 137 | EIP-155 signed native transfer |
| BNB | BNB Smart Chain, chain ID 56 | EIP-155 signed native transfer |
| USDT, USDC | Ethereum mainnet | ERC-20 transfer; ETH required for gas |
| USDT, USDC | Solana mainnet-beta | SPL TransferChecked; associated account creation; SOL required for fee/rent |
| XMR | Monero mainnet | Monero wallet RPC refresh and private transfer; encrypted upstream wallet files |

Mainnet mint and contract addresses are fixed in `src/assets.cpp`. Tokens on other networks are not supported. Polygon/BNB bridged stablecoins are not included.

This is a single-address wallet per base network. It does not implement HD recovery phrases, imported WIF keys, hardware wallets, multisig, MWEB, staking, swaps, NFTs, Token-2022, EIP-1559 transactions, automatic replacement transactions. EVM full history opens in the block explorer. SPL history covers the associated token account; balances also include other token accounts. SPL spending currently requires one token account with sufficient funds.

The **Wallets** picker can create separate encrypted wallet profiles and switch
between them. Each profile has its own addresses and Monero directory. Only
one profile is active at a time; each profile needs its own encrypted backup.

## Create and recover

1. Open wallet and choose a long, unique password. Minimum 12 bytes; a strong multiword passphrase is preferable.
2. Export an encrypted backup and keep a separate copy and the password. Restore the backup on a clean test setup and verify the same public addresses before relying on it.
3. Receive selects the asset **and network**. Copy its address or scan the QR. Check the address on a trusted display before using it.
4. Sending displays the amount, destination, network and fee before requiring your password again. A checkbox enables signing. A successful broadcast still requires on-chain confirmation.

The encrypted file is normally `$XDG_DATA_HOME/AurumWallet/Aurum/wallet.aurum`, falling back to `~/.local/share/AurumWallet/Aurum/wallet.aurum`. Settings normally live under `$XDG_CONFIG_HOME/AurumWallet` and contain endpoints and appearance, not private keys. There is **no recovery phrase**. Loss of both the encrypted backup and active file, or loss of the password, means loss of access. Backups are not uploaded anywhere.

## Security

See [SECURITY.md](SECURITY.md) for the exact threat boundary. Key protections include Argon2id, authenticated XChaCha20-Poly1305 encryption, guarded/locked secret allocations, explicit dump exclusion, disabled process dumpability and core dumps, read-only key pages, password cleanup, and a separate, short-lived signer restricted by a Linux seccomp allowlist. The GUI never decrypts private keys. The signer has no network/file access after receiving a request and exits after address derivation or signing. Mainnet identities are checked and broadcasts are not automatically repeated after an uncertain result.

**Keys must briefly exist in memory to sign.** The application blocks ordinary same-user process memory reads in the verified Linux configuration; it cannot protect against a compromised kernel, privileged malware, code injected before startup, keyloggers, or replacement of the program. A hardware signing device is needed to keep private keys out of PC memory entirely; this version does not support one.

## Connections

| Network | Default endpoint |
|---|---|
| Bitcoin | https://blockstream.info/api |
| Litecoin | ssl://electrum1.cipig.net:20063 (Electrum TLS) |
| Solana | https://api.mainnet-beta.solana.com |
| Ethereum | https://ethereum-rpc.publicnode.com |
| Polygon | https://polygon-bor-rpc.publicnode.com |
| BNB Smart Chain | https://bsc-rpc.publicnode.com |
| Monero | Triplebit public onion RPC (HTTP through local Tor) |

Public endpoints can rate limit, change availability, and observe queried addresses and your IP. No unlimited quota or fastest-provider claim is made. Native SOL uses a WebSocket subscription; other balances refresh every 30 seconds. Requests use a four-request concurrency limit, per-host spacing, timeouts and HTTP 429 backoff. An immediate refresh is available. URLs may be changed in Connections; BTC requires an Esplora-compatible indexer. LTC accepts Electrum SSL or Esplora HTTPS; neither accepts Core JSON-RPC. HTTPS/WSS/SSL is required except on loopback. Credentials embedded in URLs are refused.

Endpoint checks compare BTC/LTC/SOL genesis identifiers and EVM chain IDs. This catches accidental network mixups; a dishonest endpoint can still lie about balances, fees or confirmation. Confirmation should be checked independently when it matters. Broadcasting may disclose a signed transaction to the configured provider.

XMR uses the verified upstream Monero wallet RPC. The default is a public restricted onion RPC routed through a local Tor SOCKS proxy at `127.0.0.1:9050`, so Aurum does not download a blockchain and does not use a clearnet fallback. The wallet opens as soon as its address is available; daemon refresh continues in the background and sending waits for a spendable synchronized balance. If Tor is unavailable, XMR remains clearly marked unavailable. You can replace the node and proxy in Connections, or select the loopback endpoint to use the bundled pruned daemon; that mode can require roughly 100 GiB for initial synchronization. Monero's on-chain privacy does not make a remote node trustworthy; it can still observe wallet queries and transaction broadcasts, while Tor changes the network peer's view of your source IP. XMR backups include an `.xmr` sidecar directory beside the selected `.aurum` backup.

The Connections dialog can also log in and connect the host's official Mullvad client. When Mullvad is connected before Tor, the intended route is Mullvad VPN → Tor → the public node. Aurum does not store the Mullvad account number or implement a second VPN stack. See [MULLVAD.md](MULLVAD.md).

The new Litecoin backend and its validation limits are documented in [ELECTRUM.md](ELECTRUM.md). Circuit’s compact rows now fit inside their panel; the eleventh theme is Exodus-inspired. The app icon is included as an original scalable vector.

Provider references: [Solana clusters and limits](https://solana.com/docs/references/clusters), [Esplora API](https://github.com/Blockstream/esplora/blob/master/API.md), [Litecoin Space API](https://litecoinspace.org/docs/api/rest), [PublicNode](https://www.publicnode.com/).

## Build from source

Install development packages for Qt 6 Core/Gui/Widgets/Network/WebSockets/Test, OpenSSL, libsodium, libsecp256k1 with recovery module, Crypto++, libqrencode, CMake and a C++20 compiler. Qt's SVG image/icon plugin is required for the vector icons at runtime.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
./build/aurum --demo
```

`BUILD_TESTING=OFF` omits test programs. Runtime source is entirely C++. `tests/chain_probe.cpp` contains a deliberately public private key **only for testing** and is never linked into `aurum`.

Screenshots: `QT_QPA_PLATFORM=offscreen ./build/aurum --screenshots screenshots`.

### Optional Monero runtime

The ZIP includes Aurum's Monero integration source and the upstream Monero
license/provenance in `third_party/monero/`. It excludes the upstream
executables. Obtain `monero-wallet-rpc` (and optionally `monerod`) from the
official release identified in `third_party/monero/SOURCE.txt`, verify its
signed release checksums, and place the executables beside `build/aurum`.
Alternatively install them on `PATH`. A local Tor SOCKS proxy at
`127.0.0.1:9050` is required for the default onion endpoint.

The regular CTest run skips the upstream RPC lifecycle check unless
`AURUM_MONERO_WALLET_RPC` names an executable. To run the opt-in live test
separately from CTest's short lifecycle timeout:

```sh
QT_QPA_PLATFORM=offscreen \
  AURUM_MONERO_WALLET_RPC="$PWD/build/monero-wallet-rpc" \
  AURUM_TEST_LIVE_MONERO=1 \
  ./build/monero_tests liveTorSynchronization
```

The test creates a disposable wallet and uses no real funds. It requires Tor
and a responsive public node; it is not a test of a funded mainnet transfer.

## Validation and attribution

[TESTING.md](TESTING.md) records results, test scope, and limitations. The source includes deterministic reference vectors, negative validation tests, ordinary-process dump attempts and native UI tests. Optional JavaScript harnesses under `tests/integration` exercise reference nodes and Solana's VM; JavaScript is not used by the wallet.

Cryptographic libraries implement the primitives. Coin symbols are from [Cryptocurrency Icons](https://github.com/spothq/cryptocurrency-icons), CC0. UI icons are from [Lucide](https://github.com/lucide-icons/lucide), ISC. License texts are included. Names and logos identify their respective networks and do not imply endorsement.

## USD values and passwords

Asset lists display USD estimates with exact crypto quantities on hover. The Exodus asset view displays the coin quantity first, its ticker alongside, and the USD estimate beneath. Exodus pricing-service quotes refresh at most once a minute and expire after five minutes. [MARKET-DATA.md](MARKET-DATA.md) documents prices, partial totals, provider limits and privacy. The public wallet view stays open until manually locked or the app closes. Passwords are required for opening and signing each send.

The Exodus appearance follows the supplied asset-detail reference: the horizontal strip selects a coin, Send/Receive use that selected asset, and the history controls switch between 1D and 1Y. Moving over the chart shows the nearest historical price and time, with a crosshair and point marker. The magnifier opens a searchable asset list. It bundles the same open-source Roboto and Roboto Condensed faces verified in the official desktop package, so Linux font substitution does not change this theme. The app keeps its own name and mark. [EXODUS-REFERENCE.md](EXODUS-REFERENCE.md) records the sources.

At startup, a full-window password screen covers the wallet. Unlocking happens in the separate signer while the interface remains responsive. A second full-window screen tracks balances and prices for all eleven assets, then opens the wallet automatically when all are ready. **Open wallet** remains available while a node reconnects; unavailable balances and prices remain marked as unavailable. **Retry connections** respects provider request limits. No decrypted key is retained for this public-data session.
