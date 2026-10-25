import {execFileSync} from 'node:child_process';
import assert from 'node:assert/strict';
import path from 'node:path';
import fs from 'node:fs';
const root=path.resolve(import.meta.dirname,'../..');
const output=process.env.AURUM_RESULTS??path.join(root,'build/integration-results');
fs.mkdirSync(output,{recursive:true});
const probe=(...args)=>JSON.parse(execFileSync((process.env.AURUM_PROBE??path.join(root,'build/chain_probe')),args.map(String),{encoding:'utf8'}));
const addresses=probe('addresses');
const results=[];
for(const [symbol,dir,version,port,hrp,index] of [['BTC','bitcoin','29.3',18445,'bcrt','0'],['LTC','litecoin','0.21.4',19445,'rltc','1']]) {
 const bin=path.join(process.env.AURUM_NODES??path.join(root,'build/nodes'),`${dir}-${version}/bin/${dir}-cli`);
 const cli=(...args)=>{const raw=execFileSync(bin,[`-datadir=${process.env.AURUM_DATA??path.join(root,'build')}/${dir}-regtest`,'-regtest',`-rpcport=${port}`,...args.map(a=>typeof a==='object'?JSON.stringify(a):String(a))],{encoding:'utf8'}).trim();try{return JSON.parse(raw);}catch{return raw;}};
 if(!cli('listwallets').includes('test')){try{cli('loadwallet','test');}catch{cli('createwallet','test');}}
 cli('settxfee',0.0001);
 const miner=cli('getnewaddress');
 cli('generatetoaddress',101,miner);
 const funded=cli('sendtoaddress',addresses[hrp],1);
 const block=cli('generatetoaddress',1,miner)[0];
 const funding=cli('getrawtransaction',funded,true,block);
 const output=funding.vout.find(o=>o.scriptPubKey.address===addresses[hrp]||(o.scriptPubKey.addresses??[]).includes(addresses[hrp]));
 assert.ok(output);
 const signed=probe('utxo',symbol,funded,output.n,100000000,addresses[index],50000000,2);
 const acceptance=cli('testmempoolaccept',[signed.raw]);
 assert.equal(acceptance[0].allowed,true,JSON.stringify(acceptance));
 assert.equal(cli('sendrawtransaction',signed.raw),signed.id);
  const mined=cli('generatetoaddress',1,miner)[0];
 assert.ok(cli('getrawtransaction',signed.id,true,mined).confirmations>=1);
 results.push({chain:symbol,test:'Receive, sign, node validation, broadcast and confirmation',passed:true,transaction:signed.id,fee:signed.fee});
 cli('stop');
}
fs.writeFileSync(path.join(output,'utxo-integration-results.json'),JSON.stringify(results,null,2));
console.log(JSON.stringify(results,null,2));
