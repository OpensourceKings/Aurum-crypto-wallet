import {LiteSVM,FailedTransactionMetadata} from 'litesvm';
import {Keypair,PublicKey,Transaction} from '@solana/web3.js';
import {AccountLayout,MintLayout,TOKEN_PROGRAM_ID,getAssociatedTokenAddressSync} from '@solana/spl-token';
import {execFileSync} from 'node:child_process';
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';
const root=path.resolve(import.meta.dirname,'../..');
const output=process.env.AURUM_RESULTS??path.join(root,'build/integration-results');
fs.mkdirSync(output,{recursive:true});
const probe=(...args)=>JSON.parse(execFileSync((process.env.AURUM_PROBE??path.join(root,'build/chain_probe')),args.map(String),{encoding:'utf8'}));
const seed=Buffer.alloc(32);seed[31]=1;const payer=Keypair.fromSeed(seed),receiver=Keypair.fromSeed(Buffer.alloc(32,0x33)).publicKey;
const svm=new LiteSVM().withSigverify(true).withBlockhashCheck(true);
svm.airdrop(payer.publicKey.toBase58(),10000000000n);
const check=(raw)=>{const result=svm.inner.sendLegacyTransaction(Buffer.from(raw,'base64'));if(result instanceof FailedTransactionMetadata)throw new Error(result.toString()+JSON.stringify(result.err()));return result;};
const native=probe('sol',receiver.toBase58(),svm.latestBlockhash(),10000000);
check(native.raw);assert.equal(svm.getBalance(receiver.toBase58()),10000000n);
const results=[{asset:'SOL',test:'C++ signed transfer executed in SVM; recipient balance verified',passed:true,transaction:native.id}];
for(const [symbol,mintAddress] of [['USDT','Es9vMFrzaCERmJfrF4H2FYD4KCoNkY11McCe8BenwNYB'],['USDC','EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v']]){
 const mint=new PublicKey(mintAddress);const source=getAssociatedTokenAddressSync(mint,payer.publicKey);const destination=getAssociatedTokenAddressSync(mint,receiver);
 const mintData=Buffer.alloc(MintLayout.span);MintLayout.encode({mintAuthorityOption:1,mintAuthority:payer.publicKey,supply:1000000000n,decimals:6,isInitialized:true,freezeAuthorityOption:0,freezeAuthority:payer.publicKey},mintData);
 svm.setAccount({address:mintAddress,lamports:10000000n,data:mintData,programAddress:TOKEN_PROGRAM_ID.toBase58(),executable:false});
 const data=Buffer.alloc(AccountLayout.span);AccountLayout.encode({mint,owner:payer.publicKey,amount:1000000000n,delegateOption:0,delegate:PublicKey.default,state:1,isNativeOption:0,isNative:0n,delegatedAmount:0n,closeAuthorityOption:0,closeAuthority:PublicKey.default},data);
 svm.setAccount({address:source.toBase58(),lamports:10000000n,data,programAddress:TOKEN_PROGRAM_ID.toBase58(),executable:false});
 assert.equal(svm.getAccount(destination.toBase58()).exists,false);
 const tx=probe('spl',receiver.toBase58(),source.toBase58(),mintAddress,svm.latestBlockhash(),1000000);
 check(tx.raw);
 assert.equal(AccountLayout.decode(Buffer.from(svm.getAccount(destination.toBase58()).data)).amount,1000000n);
 assert.equal(AccountLayout.decode(Buffer.from(svm.getAccount(source.toBase58()).data)).amount,999000000n);
 // Reuse the same destination: idempotent account creation must still succeed.
 const second=probe('spl',receiver.toBase58(),source.toBase58(),mintAddress,svm.latestBlockhash(),500000);
 check(second.raw);
 assert.equal(AccountLayout.decode(Buffer.from(svm.getAccount(destination.toBase58()).data)).amount,1500000n);
 results.push({asset:symbol+' (Solana fixture)',test:'C++ signed SPL transfer, new ATA creation and existing ATA reuse executed in SVM',passed:true});
}
console.log(JSON.stringify(results,null,2));fs.writeFileSync(path.join(output,'solana-integration-results.json'),JSON.stringify(results,null,2));
