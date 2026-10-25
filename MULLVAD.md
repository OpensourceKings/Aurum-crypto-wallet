# Mullvad transport

Aurum delegates VPN state and WireGuard keys to the official Mullvad Linux
client. The Connections dialog accepts a 16-digit account number only to call
`mullvad account login`; it immediately clears the field and does not save the
number in Aurum settings, logs, environment variables or wallet files. The
Connect button then calls `mullvad connect --wait`.

The intended XMR route is:

```text
Aurum → Tor SOCKS 127.0.0.1:9050 → Mullvad VPN tunnel → public Monero node
```

This requires the Tor service to be running locally. If Mullvad is connected,
Tor's outbound connection is carried inside that VPN tunnel. Aurum does not
bundle a second VPN implementation, change Mullvad's kill switch, or silently
use clearnet when Tor is unavailable. The Mullvad daemon remains responsible
for routing, keys and account storage.

The integration was checked against the official open-source client at
`https://github.com/mullvad/mullvadvpn-app` (commit `4931987`). The client is
not copied into the Aurum runtime; the host's signed Mullvad package is used.
