// SPDX-License-Identifier: Apache-2.0
// Execute the actual installer module without opening a serial port.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import vm from 'node:vm';
import { webcrypto } from 'node:crypto';
const docs=path.resolve(process.argv[2]||'docs');
const source=await fs.readFile(path.join(docs,'webflash/installer.js'),'utf8');
const html=await fs.readFile(path.join(docs,'index.html'),'utf8');
const manifestPath=html.match(/id="flash-installer"[^>]*manifest="([^"]+)"/)?.[1];
assert.ok(manifestPath);
const manifest=JSON.parse(await fs.readFile(path.join(docs,manifestPath),'utf8'));
const origin='https://yiifanrecyclable.github.io/if-ink-OS/';
async function run({secure=true,serial=true,alter,corrupt=false,offline=false,componentFailure=false}={}) {
 const m=structuredClone(manifest);if(alter)alter(m);
 const blobs=new Map();let ports=0,imports=0,completed=false;
 const button={hidden:true,getAttribute:()=>manifestPath};const status={textContent:'loading'};
 class FakeURL extends URL {static createObjectURL(blob){const u='blob:test/'+blobs.size;blobs.set(u,blob);return u;}static revokeObjectURL(u){blobs.delete(u);}}
 const context=vm.createContext({document:{baseURI:origin,getElementById:id=>id==='flash-installer'?button:status},window:{isSecureContext:secure},navigator:serial?{serial:{requestPort(){++ports;throw Error('must not request a port during load');}}}:{},crypto:webcrypto,URL:FakeURL,Blob,TextDecoder,Uint8Array,DataView,console:{error(){}},customElements:{whenDefined:async()=>{}},fetch:async raw=>{
  if(offline)throw Error('offline');
  const u=new URL(raw,origin);
  if(u.pathname.endsWith('manifest.json'))return {ok:true,json:async()=>m};
  const data=await fs.readFile(path.join(docs,u.pathname.replace('/if-ink-OS/','')));
  if(corrupt&&u.pathname.endsWith('firmware.bin'))data[data.length-1]^=1;
  return {ok:true,arrayBuffer:async()=>data.buffer.slice(data.byteOffset,data.byteOffset+data.byteLength)};
 }});
 const module=new vm.SourceTextModule(source,{context,importModuleDynamically:async spec=>{
  ++imports;assert.equal(spec,'./vendor/esp-web-tools-if-v1/install-button.js');if(componentFailure)throw Error('component unavailable');
  const loaded=new vm.SyntheticModule([],()=>{},{context});await loaded.link(()=>{});await loaded.evaluate();return loaded;
 }});
 await module.link(()=>{throw Error('Unexpected static import');});await module.evaluate();
 for(let i=0;i<100&&status.textContent==='loading';++i)await new Promise(resolve=>setImmediate(resolve));
 assert.notEqual(status.textContent,'loading','loader must finish');assert.equal(ports,0);
 return {button,status,blobs,get imports(){return imports;},get ports(){return ports;}};
}
const good=await run();assert.equal(good.button.hidden,false,'valid current release must display install button');assert.equal(good.imports,1);
const bundle=await good.button.prepareManifest();assert.equal(good.blobs.size,5);
const snapshot=JSON.parse(await good.blobs.get(bundle.url).text());assert.equal(snapshot.version,manifest.version);
for(const part of snapshot.builds[0].parts)assert.ok(good.blobs.has(part.path));assert.equal(good.ports,0);bundle.dispose();assert.equal(good.blobs.size,0);
for(const options of [
 {alter:m=>m.version='0.1.7'},
 {alter:m=>m.builds[0].parts.find(p=>p.path==='firmware.bin').sha256='0'.repeat(64)},
 {alter:m=>m.builds[0].parts.find(p=>p.path==='firmware.bin').size++},
 {alter:m=>m.builds[0].parts[0].offset=0x1000},
 {alter:m=>m.builds[0].parts[1]=m.builds[0].parts[0]},
 {offline:true},{componentFailure:true},{secure:false},{serial:false}
]){const failed=await run(options);assert.equal(failed.button.hidden,true);assert.equal(failed.ports,0);}
const bad=await run({corrupt:true});await assert.rejects(()=>bad.button.prepareManifest());assert.equal(bad.blobs.size,0);assert.equal(bad.ports,0);
console.log('PASS actual installer execution: current manifest reveals button, verified 4-part immutable snapshot, no serial access on load; rejects stale version/hash/size/offset/duplicates, offline/import failures, unsupported contexts and corrupt firmware; failure disposes all blobs.');
