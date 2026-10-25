# Desktop appearance references

The 0.5.0 layout was compared with the user's asset-detail image and these official Exodus pages and screenshots on 10 October 2026:

- [Desktop wallet](https://www.exodus.com/desktop): toolbar, horizontal asset selector, coin-first balance, smaller USD value, outlined actions and inset chart.
- [Official asset-detail screenshot](https://www.exodus.com/_next/static/media/showcase-transact.02180869.jpg): proportions, spacing, colors and chart controls.
- [Trezor wallet](https://www.exodus.com/trezor-wallet/) and its [Ethereum screenshot](https://www.exodus.com/trezor/img/carousel/image02.png): ticker sizing and balance hierarchy for different assets.
- [Litecoin wallet](https://www.exodus.com/litecoin-wallet): asset-specific treatment.
- [Reset a wallet password](https://www.exodus.com/support/en/articles/8598660-how-do-i-reset-my-password): full-window unlock composition, centered password field and blue-purple faceted backdrop.
- [Customize Exodus Desktop](https://www.exodus.com/support/en/articles/8598944-how-do-i-customize-exodus-desktop): desktop appearance examples.

These are visual references, not included application assets. Aurum keeps its original name and mark. Its actions operate the implemented wallet functions; the UI does not advertise exchange, staking or third-party products that it does not implement. All screenshot deliverables are captures of the native C++/Qt application with temporary or explicit demo data.

## Typography verification

The official [26.10.7 Linux package](https://downloads.exodus.com/releases/exodus-linux-x64-26.10.7.zip) was inspected as an archive, without running Exodus. Its SHA-256 matched the [published release hashes](https://downloads.exodus.com/releases/hashes-exodus-26.10.7.txt): `abea449ac3e2e16f44f3c76e358953be49bd85b9279890d08cb5b38bedef3f5d`.

Its desktop stylesheet specifies Roboto for the primary interface, Roboto Light for the main coin balance, and Roboto Condensed for the asset strip, ticker and unlock heading. The balance uses slightly tightened tracking. Aurum now bundles those exact open-source Roboto WOFF faces and loads them through Qt's application font database. They no longer depend on fonts installed on the computer. The Roboto fonts are redistributed under the Apache 2.0 license; see `licenses/Roboto-Apache-2.0.txt` and `assets/fonts/NOTICE.txt`.

Only the open-source font files are redistributed. The Exodus stylesheet, application code, logos and illustrations are not included. The independently implemented Qt layout uses the supplied screenshot as its visual target; different window sizes and operating-system text rasterization can still change pixels.
