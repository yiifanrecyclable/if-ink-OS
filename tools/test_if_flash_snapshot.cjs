/* SPDX-License-Identifier: Apache-2.0
 * 中文：实际四段下载、哈希/身份、晚到请求回收及串口前失败，绝不接真实设备。
 * English: Real bundle hashes/identity, late request cleanup and pre-port failure; no real device.
 */
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm'),crypto=require('node:crypto');
const path=require('node:path');
const site=process.argv[2]?path.resolve(process.argv[2],'webflash'):path.resolve(__dirname,'../public/if-edition/webflash');
const version=fs.readFileSync(path.join(site,'installer.js'),'utf8').match(/const expectedVersion = '([^']+)'/)[1];
const manifest=JSON.parse(fs.readFileSync(path.join(site,version,'manifest.json')));
const source=fs.readFileSync(path.join(site,'installer.js'),'utf8');
async function scenario(change){
 const config=structuredClone(manifest), blobs=new Map();let released=0,fetches=0;
 class MockURL extends URL {static createObjectURL(blob){const key=`blob:test-${blobs.size}`;blobs.set(key,blob);return key;}static revokeObjectURL(key){assert(blobs.delete(key));++released;}}
 const data=new Map(config.builds[0].parts.map(p=>[p.path,new Uint8Array(fs.readFileSync(path.join(site,version,p.path)))]));
 if(change)change(config,data);
 const context={URL:MockURL,Blob,TextDecoder,DataView,Uint8Array,crypto:crypto.webcrypto,
 document:{getElementById:()=>({})},fetch:async(url,opts)=>{
  assert.equal(opts.cache,'no-store');const u=new URL(url);assert(u.searchParams.has('sha256'));++fetches;
  const name=u.pathname.split('/').pop();const bytes=data.get(name);
  await new Promise(resolve=>setTimeout(resolve,name==='firmware.bin'?1:4));
  return {ok:true,arrayBuffer:async()=>bytes.slice().buffer};
 }};
 const prepare=new vm.Script(source.replace('void start();','')+'\nprepareBundleSnapshot;').runInNewContext(context);
 return {config,blobs,get released(){return released;},get fetches(){return fetches;},run:()=>prepare(config,'https://example.test/webflash/0.1.3/manifest.json')};
}
(async()=>{
 let s=await scenario();const snap=await s.run();assert.equal(s.fetches,4);assert.equal(s.blobs.size,5);
 const frozen=JSON.parse(await s.blobs.get(snap.url).text());
 assert.equal(frozen.version,version);assert(frozen.builds[0].parts.every(p=>p.path.startsWith('blob:')));
 assert.equal(s.config.builds[0].parts[2].path,'firmware.bin');snap.dispose();snap.dispose();assert.equal(s.blobs.size,0);
 for(const name of manifest.builds[0].parts.map(p=>p.path)){
  s=await scenario((c,d)=>{d.get(name)[0]^=1;});await assert.rejects(s.run());assert.equal(s.blobs.size,0);assert.equal(s.fetches,4);
 }
 for(const offset of [48,80]){
  s=await scenario((c,d)=>{const b=d.get('firmware.bin');b[offset]^=1;c.builds[0].parts[2].sha256=crypto.createHash('sha256').update(b).digest('hex');});
  await assert.rejects(s.run());assert.equal(s.blobs.size,0);
 }
 s=await scenario((c,d)=>{d.set('bootloader.bin',d.get('bootloader.bin').slice(0,-1));});await assert.rejects(s.run());assert.equal(s.blobs.size,0);
 // 真实接线：校验拒绝不能打开串口；成功关闭释放快照。/ Real adapter: rejection cannot open a port; close releases snapshots.
 const adapter=fs.readFileSync(path.join(site,'vendor/esp-web-tools-if-v1/install-button.js'),'utf8').split(';class t ')[0].replace('await import("./install-dialog-im156JnI.js")','await loadDialog()');
 for(const fail of [false,true]){
  let opened=0,disposed=0,closed=0,dialog;const button={prepareManifest:async()=>{if(fail)throw Error('bad hash');return {url:'blob:verified',dispose:()=>disposed++};}};
  const ctx={navigator:{serial:{requestPort:async()=>({open:async()=>{opened++;},close:async()=>{closed++;}})}},loadDialog:async()=>{},alert:()=>{},
   document:{createElement:()=>dialog={addEventListener:(name,fn)=>{dialog.close=fn;}},body:{appendChild:()=>{}}}};
  const activate=new vm.Script(adapter+';e;').runInNewContext(ctx);await activate(button);
  if(fail)assert.equal(opened,0);else{assert.equal(opened,1);assert.equal(dialog.manifestPath,'blob:verified');dialog.close();assert.equal(disposed,1);assert.equal(closed,1);}
 }
 console.log('PASS: 4 real images, SHA256/length/app identity, immutable snapshot, late cleanup, no port open on validation failure');
})().catch(e=>{console.error(e);process.exitCode=1;});
