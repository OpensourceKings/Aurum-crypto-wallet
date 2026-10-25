import {execFileSync, spawn} from 'node:child_process';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import ganache from 'ganache';
import {BrowserProvider, Wallet, ContractFactory, Transaction} from 'ethers';
import solc from 'solc';
import {Keypair, PublicKey, Transaction as SolTransaction, Message} from '@solana/web3.js';
import {getAssociatedTokenAddressSync, createAssociatedTokenAccountIdempotentInstruction, createTransferCheckedInstruction} from '@solana/spl-token';
const root=path.resolve(import.meta.dirname,'../..');
const output=process.env.AURUM_RESULTS??path.join(root,'build/integration-results');
fs.mkdirSync(output,{recursive:true});
const binary=(process.env.AURUM_PROBE??path.join(root,'build/chain_probe'));
const probe=(...args)=>JSON.parse(execFileSync(binary,args.map(String),{encoding:'utf8'}));
const addresses=probe('addresses');
const findings=[];
const seed=Buffer.alloc(32);seed[31]=1;
const solKey=Keypair.fromSeed(seed);
assert.equal(solKey.publicKey.toBase58(),addresses['2']);
for(const mintAddress of ['Es9vMFrzaCERmJfrF4H2FYD4KCoNkY11McCe8BenwNYB','EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v']) {
 const mint=new PublicKey(mintAddress),recipient=new PublicKey(Buffer.alloc(32,0x23));
 const source=getAssociatedTokenAddressSync(mint,solKey.publicKey),dest=getAssociatedTokenAddressSync(mint,recipient,true);
 const blockhash=new PublicKey(Buffer.alloc(32,0x44)).toBase58();
 const cpp=probe('spl',recipient.toBase58(),source.toBase58(),mintAddress,blockhash,1000000);
 assert.equal(cpp.ata,dest.toBase58());
 const tx=new SolTransaction({feePayer:solKey.publicKey,recentBlockhash:blockhash});
 tx.add(createAssociatedTokenAccountIdempotentInstruction(solKey.publicKey,dest,recipient,mint));
 tx.add(createTransferCheckedInstruction(source,mint,dest,solKey.publicKey,1000000n,6));
 const normalize=t=>t.instructions.map(i=>({program:i.programId.toBase58(),keys:i.keys.map(k=>({pubkey:k.pubkey.toBase58(),signer:k.isSigner,writable:k.isWritable})),data:i.data.toString("hex")}));
 assert.deepEqual(normalize(SolTransaction.populate(Message.from(Buffer.from(cpp.message,"base64")))),normalize(SolTransaction.populate(tx.compileMessage())));
 findings.push({test:'SPL message and ATA match official SDK',mint:mintAddress,passed:true});
}
const key='0x'+'0'.repeat(63)+'1';
const recipient='0x'+'22'.repeat(20);
const sourceCode='pragma solidity ^0.8.20; contract TestToken { mapping(address=>uint) public balanceOf; uint8 public constant decimals=6; event Transfer(address indexed from,address indexed to,uint amount); constructor(){balanceOf[msg.sender]=1000000000;} function transfer(address to,uint amount) public returns(bool){require(balanceOf[msg.sender]>=amount);balanceOf[msg.sender]-=amount;balanceOf[to]+=amount;emit Transfer(msg.sender,to,amount);return true;} }';
const compiled=JSON.parse(solc.compile(JSON.stringify({language:'Solidity',sources:{'Token.sol':{content:sourceCode}},settings:{evmVersion:'shanghai',outputSelection:{'*':{'*':['abi','evm.bytecode']}}}}))).contracts['Token.sol'].TestToken;
for(const chain of [1,137,56]) {
 const rpc=ganache.provider({logging:{quiet:true},chain:{chainId:chain,hardfork:'shanghai'},wallet:{accounts:[{secretKey:key,balance:'0x56bc75e2d63100000'}]}});
 const request=(method,params=[])=>rpc.request({method,params});
 const price=BigInt(await request('eth_gasPrice'));
 const native=probe('evm',chain,0,price,21000,recipient,'1000000000000000000');
 assert.equal(Transaction.from(native.raw).from,addresses['3']);
 assert.equal(Transaction.from(native.raw).chainId,BigInt(chain));
 const hash=await request('eth_sendRawTransaction',[native.raw]);
 const receipt=await request('eth_getTransactionReceipt',[hash]);
 assert.equal(receipt.status,'0x1');
 assert.equal(BigInt(await request('eth_getBalance',[recipient,'latest'])),1000000000000000000n);
 findings.push({test:'Native receive / C++ signed transfer accepted',chain,txid:hash,passed:true});
 if(chain===1){
  const provider=new BrowserProvider(rpc);const wallet=new Wallet(key,provider);
  for(const symbol of ['USDT fixture','USDC fixture']){
   const factory=new ContractFactory(compiled.abi,compiled.evm.bytecode.object,wallet);
   const contract=await factory.deploy({nonce:Number(BigInt(await request('eth_getTransactionCount',[addresses['3'],'pending'])))});await contract.waitForDeployment();
   const token=await contract.getAddress();const data=contract.interface.encodeFunctionData('transfer',[recipient,1234567n]);
   const nonce=BigInt(await request('eth_getTransactionCount',[addresses['3'],'pending']));
   const gas=BigInt(await request('eth_estimateGas',[{from:addresses['3'],to:token,data}]));
   const signed=probe('evm',chain,nonce,price,(gas*12n+9n)/10n,token,'0',data.slice(2));
   const tokenHash=await request('eth_sendRawTransaction',[signed.raw]);
   assert.equal((await request('eth_getTransactionReceipt',[tokenHash])).status,'0x1');
   assert.equal(await contract.balanceOf(recipient),1234567n);
   findings.push({test:'ERC20 balance / C++ signed transfer accepted',asset:symbol,chain,txid:tokenHash,passed:true});
  }
 }
 await rpc.disconnect();
}
fs.writeFileSync(path.join(output,'account-integration-results.json'),JSON.stringify(findings,null,2));
console.log(JSON.stringify(findings,null,2));
