# Source snapshot status (2026-10-25)

The sections below record historical validation, not a fresh test result for
the source snapshot. Runtime logs and generated test results are intentionally
excluded from the source ZIP. Monero public-node/Tor synchronization remains
an unresolved acceptance issue: a prior temporary wallet reached a synced
state, but the final implementation has not completed a live sync/reopen test.
The optional live check is `monero_tests liveTorSynchronization` with
`AURUM_TEST_LIVE_MONERO=1` and `AURUM_MONERO_WALLET_RPC` set; run it directly,
outside CTest's shorter lifecycle timeout. Do not treat a passing regular
suite or a skipped live check as proof that Monero is ready for real funds.

## Historical validation — 11 October 2026

Platform: Fedora 44 x86-64, GCC 16.2.1, Qt 6.11.2. Test data and local chains only. No customer funds or live mainnet transfers.

## C++ and UI checks

`ctest --test-dir build --output-on-failure` runs:

- **signer_tests:** actual helper process rejects external process memory reads; isolated create/reopen matches the ten Aurum vault addresses; all ten Aurum-signed assets produce the same signed bytes as the C++ reference path. Monero is intentionally owned by its upstream wallet engine. Wrong passwords, damaged ciphertext, changed wallet keys, chain ID mismatches, token approvals, wrong SPL mint and modified Solana messages are rejected. Password buffers clear on successful and failed calls.
- **security_tests:** a parent can read a normal child; a protected child rejects process_vm_readv, /proc/pid/mem and ptrace attachment. Secret pages have Linux locked (`lo`) and dump-excluded (`dd`) flags. Core limits and password cleanup are verified. A sandboxed child rejects file opens, network sockets, fork, exec and restoring dumpability while OS randomness still works.
- **core_tests:** exact integer amounts and overflow, address checksums/network mismatches, published BIP-143 signing vector, RFC 8032 Ed25519 vector, published EIP-155 transaction vector, EIP-55 addresses, ERC-20 call data, SPL address derivation/messages, vault create/restore/password/tamper/permissions, fee conservation, all ten Aurum-vault mocked balance paths, endpoint validation and wrong-chain rejection.
- **ui_tests:** Copper defaults, all eleven appearances selected through the real picker, persistent selection after reopening, eleven different rendered appearances, embedded Roboto family resolution, native controls, create encrypted wallet, all ten vault receive addresses plus the XMR placeholder path, coin icons, QR presence, and locking before receive. Covers full-window startup, wrong-password rejection, field cleanup, partial connection failures, and automatic opening once all eleven balances and quotes are loaded. Uses temporary directories and loopback endpoints.
- **monero_tests:** launches the signed upstream `monero-wallet-rpc`, creates a temporary encrypted XMR wallet, obtains a native 95-character address, verifies owner-only wallet files, rejects a wrong transfer password, and shuts the RPC child down cleanly. No daemon or funds are used.

The runtime-read test establishes a specific OS permission boundary. It is not a test against privileged physical memory acquisition or malware already executing within the wallet process.

The current build runs seven CTest entries. The Monero RPC lifecycle test is skipped unless `AURUM_MONERO_WALLET_RPC` points to the verified binary shipped in `third_party/monero`.

## Reference-node and VM checks

Saved machine-readable results are in `test-results/`.

| Assets | Independent check | Outcome |
|---|---|---|
| Bitcoin | Bitcoin Core 29.3 regtest: fund wallet script, sign with C++, testmempoolaccept, broadcast and confirm | Passed |
| Litecoin | Litecoin Core 0.21.4 regtest: same receive/send cycle | Passed |
| ETH, POL, BNB | Ethers decodes signature/sender/chain ID; Ganache accepts C++ transaction on IDs 1, 137, 56 and verifies recipient balance | Passed |
| USDT, USDC on Ethereum | C++ transfer accepted against local ERC-20 fixtures; recipient token balance checked | Passed |
| SOL | C++ signed transaction executes in LiteSVM with signature and blockhash verification; recipient balance checked | Passed |
| USDT, USDC on Solana | C++ message matches official SDK instruction semantics; LiteSVM executes SPL transfer, creates recipient ATA, then repeats with existing ATA | Passed |

Token fixtures verify encoding and transfer behavior; they do not reproduce every production token's administrative controls, freezes, upgrades or congestion. Mainnet public endpoints were contacted read-only for chain/height checks, not transaction submission. Default provider availability is not guaranteed.

The Bitcoin archive SHA-256 matched its published checksum: `3b7fbb5cf5f9009c578b0bbe8512e67309ec2266f69facf402bd59238322ed07`. Litecoin: `857fc41091f2bae65c3bf0fd4d388fca915fc93a03f16dd2578ac3cc92898390`. Checksums were fetched over HTTPS; release signer identities were not independently verified.

## Limits

Passing tests do not establish that every mainnet transfer succeeds or that the wallet is safe for real custody. Independent code/security review, extended adversarial testing and hardware signer support remain necessary before calling this production-ready. Offline design captures use sample holdings and sample history. The live wallet uses provider quotes and historical data and labels missing data; it does not fabricate prices.

## Final instrumented pass

Version 0.4.0 passed all six CTest suites in both the regular build and a separate AddressSanitizer/UndefinedBehaviorSanitizer build. `UBSAN_OPTIONS=halt_on_error=1` was used. LeakSanitizer was disabled (`ASAN_OPTIONS=detect_leaks=0`), so this is not a leak-check result. The secret allocator also invokes Linux's mlock syscall directly, since diagnostic runtimes can intercept the library mlock call as a no-op; the locked-page assertion passed in both builds. Logs are included under `test-results/`.

## 0.2.0 isolation change

The new signer test compares exact transaction bytes against the already reference-tested encoders for all ten assets. Reference-node and VM results above remain encoding checks from 0.1.0; this change does not alter the wire encoders. The GUI’s production source has no Vault decryption or private-key access calls. The isolated helper is bundled beside the GUI and verified in a relocated package.

## 0.2.1 connection-status fix

Added a regression test for locked, initial connection, HTTP 503, recovery, failed refresh after a successful sync, and paused states. This test remains in the 0.3.0 suite. No signing or vault logic changed in 0.2.1. Read-only provider investigation is recorded in `CONNECTION-TROUBLESHOOTING.md`.

## 0.3.0 Electrum and appearance update

The regular build runs five suites. `electrum_tests` exercises the full C++ network path against a local protocol server: fragmented responses, notifications, mainnet checks, balances including pending spends, raw previous-transaction verification, incorrect input amounts, history, single broadcast, cancellation, URL restrictions and malformed raw transactions. Live provider checks use only a public test script and perform no broadcast. The GUI picker now covers eleven themes; screenshots include Circuit and Exodus at both 1440×960 and 1100×900.

The 0.3.0 implementation passed five suites; current logs are described below. Reference-node/VM results remain the historical encoder checks described above; these are not claims of a mainnet transfer or an audit.

## 0.4.0 USD display and reference layout

Six suites passed in regular and AddressSanitizer/UndefinedBehaviorSanitizer builds. `market_tests` covers explicit quote IDs, both chains sharing token prices, non-$1 stablecoins, stale/invalid data, HTTP query privacy, request throttling, historical point validation, the actual history network path and shared token history cache. `ui_tests` checks all ten USD entries and exact coin/fiat tooltips, a native hover event, selected-asset receive routing, all six chart range controls, and all eleven theme switches. Live read-only Qt quote checks returned fresh prices for all ten entries. The history endpoint was also checked read-only. No real wallet was opened or transferred. Logs are included in `test-results/`.

## 0.5.0 startup and reference typography

The UI test exercises the actual asynchronous isolated-signer startup path. It creates a temporary encrypted wallet, verifies that the overlay fills the window, waits for the loading view, and checks that unavailable providers leave an explicit continuation path. A separate local HTTP fixture supplies correct chain identifiers, zero balances and fresh quotes for all ten entries; the wallet opens automatically only once they are ready. A wrong password leaves the wallet covered and the input empty. Receive controls resolve to the same public addresses as the encrypted test wallet.

The chart test sends actual mouse and tooltip events at two positions, verifies changing historical date/price text and the crosshair cursor, and collapses/reopens the chart. The selected Litecoin view exposes both its coin quantity and USD estimate. Font assertions check that embedded Roboto and Roboto Condensed resolve to their actual families rather than a system fallback. Visual captures cover all eleven themes and the unlock, loading, Litecoin and chart-hover views. No actual user wallet is opened by these checks.

All six suites passed in the regular build and under AddressSanitizer/UndefinedBehaviorSanitizer (`detect_leaks=0`, `halt_on_error=1`). The final typography build also runs the complete UI suite and a capture harness that checks the actual window and theme font family. Logs are stored as `0.5.0-regular.log`, `0.5.0-asan-ubsan.log` and `0.5.0-ui-fonts.log` in `test-results/`. These results do not constitute an independent security audit or a real-funds transfer test.

## 0.6.0 Exodus pricing integration

The market suite verifies the Exodus JSON formats, distinct native-chain identifiers, shared stablecoin quotes, stale or invalid data rejection, Unix-second conversion, all-asset history caching, calendar range selection, current-quote completion, HTTP GMT date handling, request privacy, refresh throttling, shared `Retry-After` backoff and cancellation on lock. Loopback responses exercise the real asynchronous Qt network path.

All six suites passed in the regular build and with AddressSanitizer/UndefinedBehaviorSanitizer (`detect_leaks=0`, `halt_on_error=1`). A separate C++ probe used the production service without credentials and verified current prices, 24-hour changes and hourly/daily history for all ten wallet assets. The native capture harness used live pricing with a temporary zero-balance wallet and loopback blockchain providers. The captured Litecoin 1D chart includes its actual hover date and price. No user vault or mainnet transfer was involved.

Final logs: `test-results/0.6.0-regular.log`, `0.6.0-asan-ubsan.log`, `0.6.0-live-pricing.log` and `0.6.0-live-chart-ui.log`. Tests establish the behavior checked here; they are not an independent wallet security audit.

## 0.7.2 Monero and wallet profiles

The verified Monero CLI archive was downloaded from the official Monero release site, checked against the signed `hashes.txt` list using binaryFate's published signing key, and bundled as `monero-wallet-rpc`. A local Qt test exercised wallet creation, address retrieval, encrypted wallet file permissions, wrong-password rejection before transfer, and child shutdown. The RPC process is loopback-bound, launched with a random local port, and loaded with Aurum's small preload hardener for core-dump and dumpability restrictions. The test uses a temporary wallet and no daemon, so it does not claim a mainnet sync or transfer.

The full build currently passes all eight CTest entries in about 40 seconds in the regular Fedora 44 build. This is functional validation, not an independent Monero or wallet security audit.

The runtime default is the configured public restricted onion Monero node through the local
Tor SOCKS proxy at `127.0.0.1:9050`; Aurum does not silently fall back to
clearnet when Tor is unavailable. The bundled `monerod` is retained for the
explicit loopback mode (`http://127.0.0.1:18081`) and uses pruned synchronization.

The remote path was exercised manually with the packaged wallet RPC, Mullvad
connected, Tor at `127.0.0.1:9050`, and the public onion node. Wallet creation
returned a native address through that path. A public HTTPS node was rejected
when its certificate was not accepted by the upstream wallet RPC; Aurum does
not bypass that validation and uses the onion route by default.

The wallet profile suite creates two independent profiles in a temporary data
directory, verifies separate encrypted vault and Monero paths, reopens the
registry, rejects a traversal identifier, and checks that group and other users
have no file permissions. The primary profile continues to use the legacy
`wallet.aurum` and `monero/` paths so an upgrade does not move or rewrite an
existing wallet. The Wallets control switches one profile at a time, stopping
the previous network and Monero sessions before showing the selected profile's
password screen.
