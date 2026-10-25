# Optional independent transaction checks

These scripts are test-only. The wallet does not use Node.js or any of these packages at runtime. All keys here are public test vectors; never send real funds to their addresses.

Build `chain_probe` with CMake, install the pinned development packages with `pnpm install --frozen-lockfile`, then run `node integration.mjs` and `node solana-vm.mjs`. The first uses Ethers/Ganache/Solidity fixtures and the official Solana SDK; the second executes signed transactions inside LiteSVM. JavaScript native acceleration is optional for the first script.

`AURUM_PROBE` overrides the default `../../build/chain_probe` binary. `AURUM_RESULTS` overrides the output directory. Ganache checks Ethereum/Polygon/BNB transaction formats with local chain IDs 1/137/56. ERC-20 and SPL token tests use local token fixtures, not production token state.

For `regtest.mjs`, obtain the official Bitcoin Core 29.3 and Litecoin Core 0.21.4 executables and verify their release checksums. Put their extracted directories under `../../build/nodes` or set `AURUM_NODES`. Create fresh data directories at `../../build/bitcoin-regtest` and `../../build/litecoin-regtest`, or set their common parent using `AURUM_DATA`. Start each daemon in regtest mode, disable outbound connections, and expose only local RPC on 18445 (BTC) and 19445 (LTC):

```
bitcoind -regtest -daemon -datadir=/absolute/path/bitcoin-regtest -rpcport=18445 -port=18446 -connect=0 -listen=0
litecoind -regtest -daemon -datadir=/absolute/path/litecoin-regtest -rpcport=19445 -port=19446 -connect=0 -listen=0
node regtest.mjs
```

The script creates/loads a wallet named `test`, mines test coins, funds the C++ test address, validates/broadcasts a C++-signed transfer and mines confirmation. It stops both daemons on success. **Use dedicated empty regtest directories**, not directories containing your real wallet. No mainnet funds are used, even though an output may have a mainnet-formatted address script: the transaction runs exclusively in regtest.
