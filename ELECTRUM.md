# Litecoin connection — 0.3.0

Aurum now has a native C++ Electrum 1.4 client. It uses `ssl://electrum1.cipig.net:20063` for Litecoin by default. The former default, `https://litecoinspace.org/api`, is migrated in memory on startup; custom endpoints are retained. HTTPS Esplora endpoints remain supported.

## Source review

Reviewed [Electrum-LTC's server list](https://github.com/pooler/electrum-ltc/blob/f9cbfaca5f8b2ab3eef89758bf78b7e4c2c554a0/electrum_ltc/servers.json), [interface validation](https://github.com/pooler/electrum-ltc/blob/f9cbfaca5f8b2ab3eef89758bf78b7e4c2c554a0/electrum_ltc/interface.py), and [network/broadcast handling](https://github.com/pooler/electrum-ltc/blob/f9cbfaca5f8b2ab3eef89758bf78b7e4c2c554a0/electrum_ltc/network.py). The Python application is not included or used. Aurum's implementation is original C++.

Relevant design choices: negotiate the protocol, identify mainnet, look up SHA-256 script hashes, validate returned records, retrieve raw previous transactions, and check the returned broadcast ID. Server error messages are not displayed as instructions or clickable content.

## Connection checks on 10 October 2026

The prior Esplora provider returned the correct genesis hash and current block height, but its address endpoint timed out even after 40 seconds. Electrum-LTC's listed `electrum1.cipig.net:20063` accepted normal certificate/hostname verification and returned Litecoin's genesis hash, a balance, and unspent outputs for a public test script. Other listed servers had certificate, DNS, refusal or timeout failures. No certificate checks were disabled.

A public server is shared infrastructure. Availability, latency and absence of rate limits are not guaranteed. The wallet spaces requests, limits response size and queue length, and refreshes every 30 seconds. Only public addresses/script hashes and signed transactions cross the network. A persistent TLS connection avoids a new TLS handshake for each refresh.

## Validation boundaries

The client rejects incorrect network IDs, malformed results and unexpected request IDs. Before signing, it recomputes each previous transaction's ID from raw bytes, stripping witness data for the txid, and checks the selected output amount and script against the wallet address. Broadcast is sent once; an ambiguous connection failure must be checked in an explorer before retrying.

This is an indexer client, not a full Litecoin node or Electrum-LTC's full SPV implementation. It does not independently verify proof of work, headers or Merkle inclusion. Confirmation status, completeness of history and spent status depend on the server. These checks and passing tests do not constitute an independent security audit.

No customer wallet data was used for development checks and no mainnet transaction was submitted.
