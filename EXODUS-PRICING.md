# Exodus chart protocol — verified 10 October 2026

Aurum 0.6.0 uses the same anonymous pricing service observed in Exodus Desktop 26.10.7: `https://pricing.a.exodus.io`.

## Evidence

The official [Linux release archive](https://downloads.exodus.com/releases/exodus-linux-x64-26.10.7.zip) was inspected without executing it. Its SHA-256 matched the [published release hash](https://downloads.exodus.com/releases/hashes-exodus-26.10.7.txt):

`abea449ac3e2e16f44f3c76e358953be49bd85b9279890d08cb5b38bedef3f5d`

The bundled desktop client specifies the production pricing URL above, constructs `current-price`, `ticker` and `historical-price` requests, and maps the chart's 1D/1W controls to hourly history and 1M/3M/6M/1Y controls to daily history. The bundled history defaults request 168 hourly periods and 366 daily periods. The chart range calculation uses calendar months and appends the current quote at the current hour.

The corresponding public implementation is documented in [Exodus pricing](https://docs.exodus.com/open-source/hydra/features/pricing), [market history](https://docs.exodus.com/open-source/hydra/features/market-history) and [price-api](https://docs.exodus.com/open-source/hydra/libraries/price-api). The public [pricing client source](https://github.com/ExodusOSS/hydra/blob/master/features/pricing/module/index.js) confirms the request construction. Older documentation uses a staging URL; Aurum uses the production URL verified in the desktop package.

Exodus's [pricing explanation](https://www.exodus.com/support/en/articles/8598723-what-is-the-total-value-of-my-exodus-wallet) describes aggregated market sources including CoinMarketCap, CoinGecko, CryptoCompare and GeckoTerminal. Aurum calls Exodus's aggregated feed; it does not claim to reproduce the server's private aggregation algorithm or use a third-party embedded chart widget.

## Requests

| Route | Parameters | Purpose |
| --- | --- | --- |
| `current-price` | `from=<symbols>&to=USD&ignoreInvalidSymbols=true` | Unit prices |
| `ticker` | Same asset and currency parameters | `c24h` percentage changes |
| `historical-price` | Same parameters plus `granularity=hour&limit=168&timestamp=<previous-hour-start>` | 1D and 1W |
| `historical-price` | Same parameters plus `granularity=day&limit=366&timestamp=<previous-day-start>` | 1M, 3M, 6M and 1Y |

History is returned as `{ "LTC": { "USD": [{ "time": 1791644400, "close": 63.77, "open": 63.77 }] } }`. Timestamps are Unix seconds; Qt uses milliseconds. The service may return `limit + 1` samples, inclusive of the boundary. Aurum validates ordered, finite, positive prices, time alignment and the last completed period before caching a series.

| Wallet asset | Pricing identifier |
| --- | --- |
| Bitcoin | BTC |
| Litecoin | LTC |
| Solana | SOL |
| Ethereum | ETH |
| Polygon native POL | MATICNATIVE |
| BNB Smart Chain | BNBBSC |
| USDT on Ethereum or Solana | USDT |
| USDC on Ethereum or Solana | USDC |

`MATICNATIVE` and `BNBBSC` are the desktop service's native-chain identifiers, not labels displayed to users. Both native identifiers were tested anonymously. POL is not sent as an unsupported symbol. Stablecoins retain their market price rather than an assumed $1 peg.

## Aurum implementation

The client and chart are independently written C++/Qt. No Exodus JavaScript, binary, cookies, credentials, API keys, logos or chart assets are included. The existing open-source Roboto font license is documented separately.

Quotes are batched for all assets once per minute. History is batched once per requested granularity and cached through that completed period, so switching assets and shorter ranges reuses data. Requests are bounded in time and size, verify TLS normally, reject cross-origin redirects and do not store or transmit service cookies. HTTP 429 responses cause shared backoff, including `Retry-After`. Locking cancels pending requests and clears displayed data.

The only market-request inputs are the fixed asset symbols, USD, interval and timestamps. Wallet addresses, balances, passwords and transaction data are not included. The service can observe the client's IP address and requested assets. Anonymous access was verified on the date above; future availability and rate limits are controlled by Exodus. Failures show unavailable data rather than fabricated prices.
