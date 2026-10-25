# Litecoin connection

Version 0.3.0 replaces the failing default Esplora connection with a certificate-verified Electrum TLS connection. Existing custom endpoints are retained. The old default is migrated automatically when the app restarts.

Close and reopen Aurum when no transfer is pending. In Connections, the default Litecoin endpoint should be `ssl://electrum1.cipig.net:20063`. Unlock the wallet and allow a refresh. A successful request displays “Synced · Electrum TLS.” If it fails, the asset row and network panel show the actual error. Never disable certificate or mainnet checks to force a connection.

Read [ELECTRUM.md](ELECTRUM.md) for source review, tests and the trust boundary. Public services can still fail or rate limit. If a transaction submission loses its connection, check its ID in an explorer before retrying. No mainnet transfer was sent to verify this update; the software remains experimental and unaudited.

For Monero, start Tor with a local SOCKS listener on `127.0.0.1:9050` before
unlocking. If you want the layered route, connect Mullvad first using its
official client or Aurum's Connections dialog, then start Tor. Aurum keeps XMR
unavailable when Tor cannot be reached; it does not fall back to a direct
clearnet connection. The public node remains a trust boundary even when the
transport path is VPN → Tor.

Installation changes program files and the app icon only. It does not replace, decrypt or reset the encrypted wallet.
