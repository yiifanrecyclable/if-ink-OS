/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 执行实际网页脚本，验证目录操作与分类上传。/ Exercise the real page script for browsing, editing, and typed uploads.
 */
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const {webcrypto}=require('node:crypto');
const html = fs.readFileSync('components/read_pico_transfer/upload.html', 'utf8');
const source = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const flush = () => new Promise(resolve => setImmediate(resolve));

class Element {
  constructor() {
    this.children = []; this.dataset = {}; this.value = ''; this.files = [];
    this.classList = {toggle() {}};
  }
  set textContent(text) { this.text = String(text); this.children = []; }
  get textContent() { return this.text || ''; }
  set innerHTML(_) { throw Error('Do not parse file names as HTML'); }
  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.children = children; }
  showModal() { this.open = true; }
  close() { this.open = false; }
  focus() {}
  setAttribute(key,value) {this[key]=value;}
  getContext() { return {setTransform() {}, clearRect() {}, fillRect() {}, drawImage() {}, fillText() {},strokeText() {},measureText(text){return {width:text.length*24}},getImageData(){return {data:new Uint8ClampedArray(684*1216*4)}},putImageData:()=>{this.pixelWrites=(this.pixelWrites||0)+1},createImageData(w,h){return {data:new Uint8ClampedArray(w*h*4)}}}; }
  getBoundingClientRect() { return {width: 260}; }
  setPointerCapture() {}
  toBlob(resolve,type='image/png') { assert.equal(this.width, 684); assert.ok(this.height===1216||this.height===300); resolve({size: 120000,type}); }
}

async function setup({responses = [], confirmations = [true], mode = 'ap'} = {}) {
  const nodes = new Map();
  const sections = ['book', 'font', 'picture', 'file', 'document'].map(kind => {
    const section = new Element();
    section.dataset.kind = kind;
    const input = new Element(), result = new Element(), send = new Element();
    section.querySelector = selector => selector === 'input' ? input : selector === '.result' ? result : send;
    section.input = input; section.result = result; section.send = send;
    return section;
  });
  const document = {
    getElementById(id) { if (!nodes.has(id)) nodes.set(id, new Element()); return nodes.get(id); },
    createElement() { return new Element(); },
    createElementNS() {return new Element();},
    querySelector(selector) {return sections.find(s=>s.dataset.kind==='file');},
    querySelectorAll(selector) { return selector === '.upload' ? sections : selector==='[data-protected]' ? [...nodes.values()].flatMap(n=>n.children).flatMap(n=>n.children).flatMap(n=>n.children).filter(n=>n.dataset?.protected==='1') : [...nodes.values(), ...sections.flatMap(s => [s.input, s.send])]; },
  };
  const initial={ok:true,revision:'rev-1',banner:{background:false,image:'',show_date:true,date_y:20,show_text:true,text:'自由阅读',font:'',size:96,align:2,valign:0,text_y:80},wallpaper:'',date:'2026-10-28',date_size:28,date_y:20,text_y:80,fonts:[]};
  const state = {gets: [], mutations: [], sent: [],appearance:initial,appearancePosts:[]};
  const fetch = async (url, options = {}) => {
    if(url==='/appearance')return {status:200,json:async()=>structuredClone(state.appearance)};
    if(url.startsWith('/appearance?op=')){
      const body=JSON.parse(options.body),op=new URL('http://pico'+url).searchParams.get('op');state.appearancePosts.push({op,body});
      if(op==='font-release')return {status:200,json:async()=>({ok:true})};
      if(op==='font-match')return {status:200,json:async()=>({path:''})};
      if(op==='preview'){const response={status:200,arrayBuffer:async()=>new Uint8Array(102600).buffer};if(state.holdPreview)return new Promise(resolve=>state.resolvePreview=()=>resolve(response));return response;}
      if(op==='wallpaper')state.appearance.wallpaper=body.path;
      else {const {revision,sync_font,font_name,...banner}=body;if(sync_font)banner.font='/sdcard/fonts/'+font_name;state.appearance.banner=banner;}
      state.appearance.revision='rev-'+(state.appearancePosts.length+1);
      return {status:200,json:async()=>structuredClone(state.appearance)};
    }
    if(url.startsWith('/file?'))return {ok:true,status:200,blob:async()=>({size:100})};
    if (url === '/info') return {status: 200, json: async () => ({
      mode, is_flash: false, free_bytes: 100000000, file_limit: 0,
    })};
    if (url.startsWith('/files?')) {
      state.gets.push(url);
      const path = new URL('http://pico' + url).searchParams.get('path') || '';
      const items = path ? [{name: '封面.jpg', directory: false, size: 1200000}]
        : [{name: 'books', directory: true, size: 0}, {name: 'readme.txt', directory: false, size: 12}];
      return {status: 200, json: async () => ({path, total: items.length, pages: 1, items})};
    }
    if (url === '/files' && options.method === 'POST') {
      state.mutations.push(JSON.parse(options.body));
      return {status: 200, json: async () => ({ok: true})};
    }
    throw Error('Unexpected fetch: ' + url);
  };
  class XHR {
    constructor() { this.upload = {}; }
    open(method, url) { assert.equal(method, 'PUT'); this.url = url; }
    send(file) {
      state.sent.push({url: this.url, file});
      this.upload.onprogress({loaded: file.size, total: file.size});
      const response = responses.shift() || {status: 200, body: {ok: true}};
      queueMicrotask(() => {
        this.status = response.status; this.responseText = response.raw ?? JSON.stringify(response.body);
        this.onload();
      });
    }
  }
  class ImageMock {
    naturalWidth = 1200;
    naturalHeight = 1600;
    set src(value) { this.url = value; queueMicrotask(() => this.onload()); }
  }
  const context = vm.createContext({document, fetch, XMLHttpRequest: XHR, TextEncoder, URL,
    Image: ImageMock, crypto:webcrypto, setTimeout:()=>1, clearTimeout:()=>{}, confirm: () => confirmations.length ? confirmations.shift() : true});
  context.URL = {createObjectURL: () => 'blob:test', revokeObjectURL() {}};
  vm.runInContext(source, context);
  await flush(); await flush();
  return {nodes, sections, state, context};
}

(async () => {
  assert.match(html, /TF 卡文件/);
  assert.doesNotMatch(html, /图片对比|编辑书架名称/);
  let t = await setup();
  assert.ok(t.nodes.get('bannerZoom').disabled);assert.ok(t.nodes.get('bannerResetCrop').disabled);
  t.nodes.get('bannerDate').checked=false;t.nodes.get('bannerDate').oninput();assert.ok(t.nodes.get('bannerDateY').disabled);
  t.nodes.get('bannerDate').checked=true;t.nodes.get('bannerDate').oninput();assert.equal(t.nodes.get('bannerDateY').disabled,false);
  t.nodes.get('bannerDateY').value='140';t.nodes.get('bannerDateY').onchange();await flush();await flush();
  assert.equal(t.state.appearancePosts.at(-1).body.date_y,140);
  assert.match(html,/背景图缩放/);assert.match(html,/日期上下位置/);assert.match(html,/文字上下位置/);
  assert.match(t.nodes.get('storage').textContent, /TF 卡已就绪/);
  assert.equal(t.nodes.get('entries').children.length, 2);
  const folder = t.nodes.get('entries').children[0];
  assert.equal(folder.children[0].children[1].textContent,'默认书籍文件夹（不可更改）');assert.ok(folder.children[1].children.every(b=>b.disabled));
  folder.children[0].children[0].onclick();
  await flush(); await flush();
  assert.equal(t.nodes.get('path').textContent, '/books');
  assert.match(t.state.gets.at(-1), /path=books/);
  const item = t.nodes.get('entries').children[0];
  const rename = item.children[1].children.find(b=>b.textContent==='重命名');
  rename.onclick();
  t.nodes.get('editorName').value = '新封面.jpg';
  await t.nodes.get('editorConfirm').onclick();
  assert.deepEqual(t.state.mutations[0], {action: 'rename', path: 'books/封面.jpg', target: 'books/新封面.jpg'});
  t.nodes.get('newFolder').onclick();
  t.nodes.get('editorName').value = '新目录';
  await t.nodes.get('editorConfirm').onclick();
  assert.deepEqual(t.state.mutations[1], {action: 'mkdir', path: 'books', target: 'books/新目录'});
  item.children[1].children.find(b=>b.textContent==='复制').onclick();
  t.nodes.get('editorName').value = '封面副本.jpg';
  await t.nodes.get('editorConfirm').onclick();
  assert.deepEqual(t.state.mutations[2], {action: 'copy', path: 'books/封面.jpg', target: 'books/封面副本.jpg'});
  item.children[1].children.find(b=>b.textContent==='删除').onclick();
  await t.nodes.get('editorConfirm').onclick();
  assert.deepEqual(t.state.mutations[3], {action: 'delete', path: 'books/封面.jpg'});
  for (const [kind, name, endpoint] of [
    ['book', '海边的信.epub', '/upload'],
    ['font', '思源宋体.otf', '/font-upload'],
    ['picture', '封面.jpeg', '/image-upload'],
  ]) {
    const section = t.sections.find(s => s.dataset.kind === kind);
    section.input.files = [{name, size: 1200000}];
    await section.send.onclick();
    assert.match(t.state.sent.at(-1).url, new RegExp('^' + endpoint));
    assert.match(section.result.textContent, /已保存/);
  }
  assert.match(html, /data-kind="document"[\s\S]*?accept="\.md"/);
  let documents = t.sections.find(s => s.dataset.kind === 'document');
  documents.input.files = [{name: '日记.md', size: 1200}];
  await documents.send.onclick();
  assert.equal(t.state.sent.at(-1).url, '/document-upload?name='+encodeURIComponent('日记.md'));
  assert.match(documents.result.textContent, /已保存/);
  const sent = t.state.sent.length;
  for (const file of [{name:'Wrong.txt',size:10},{name:'Large.md',size:262145},{name:'Empty.md',size:0}]) {
    documents.input.files = [file]; await documents.send.onclick();
    assert.equal(t.state.sent.length, sent);
    assert.doesNotMatch(documents.result.textContent, /已保存/);
  }
  t = await setup({responses:[{status:409,body:{error:'同名文档'}},{status:200,body:{ok:true}}]});
  documents = t.sections.find(s=>s.dataset.kind==='document');
  documents.input.files=[{name:'日记.md',size:32}]; await documents.send.onclick();
  assert.match(t.state.sent[1].url,/document-upload.*overwrite=1/);
  // 浏览文档目录的旧入口也走文档事务；损坏错误响应仍给出可理解的原因。
  // The directory browser uses document transactions too; malformed errors remain understandable.
  t = await setup();
  vm.runInContext("path='markdown'", t.context);
  const browsedDocs = t.sections.find(s=>s.dataset.kind==='file');
  browsedDocs.input.files=[{name:'已有入口.md',size:32}]; await browsedDocs.send.onclick();
  assert.equal(t.state.sent.at(-1).url,'/document-upload?name='+encodeURIComponent('已有入口.md'));
  assert.match(browsedDocs.result.textContent,/已保存/);
  t = await setup({responses:[{status:403,raw:'Forbidden'}]});
  documents=t.sections.find(s=>s.dataset.kind==='document');
  documents.input.files=[{name:'拒绝.md',size:32}]; await documents.send.onclick();
  assert.match(documents.result.textContent,/此目录受保护/);
  assert.doesNotMatch(documents.result.textContent,/响应无法解析|已保存/);
  t = await setup({responses: [{status: 409, body: {error: '同名文件已存在'}}, {status: 200, body: {ok: true}}]});
  t.sections[2].input.files = [{name: '同名.png', size: 100}];
  await t.sections[2].send.onclick();
  assert.match(t.state.sent[1].url, /overwrite=1/);
  // 热点/已有 WiFi 共用入口，目录目标与多文件串行上传保持一致。
  // AP/STA share the same entry, directory target and serial multi-file uploads.
  for (const mode of ['ap', 'sta']) {
    t = await setup({mode});
    const generic = t.sections.find(s => s.dataset.kind === 'file');
    generic.input.files = [{name: 'README', size: 0}, {name: '升级 包%+#.bin', size: 32}, {name: '.env', size: 17}];
    await generic.send.onclick();
    assert.equal(t.state.sent.length, 3);
    assert.deepEqual(t.state.sent.map(s => new URL('http://pico' + s.url).searchParams.get('path')), ['README', '升级 包%+#.bin', '.env']);
    assert.match(generic.result.textContent, /已保存 3 个文件到 \/$/);
    t.nodes.get('entries').children[0].children[0].children[0].onclick();
    await flush(); await flush();
    generic.input.files = [{name: '文件 没有扩展名', size: 100}];
    await generic.send.onclick();
    assert.equal(new URL('http://pico' + t.state.sent.at(-1).url).searchParams.get('path'), 'books/文件 没有扩展名');
    assert.match(generic.result.textContent, /已保存 \/books\/文件 没有扩展名/);
  }
  t = await setup({responses: [{status: 409, body: {error: '同名文件已存在'}}, {status: 200, body: {ok: true}}]});
  const generic = t.sections.find(s => s.dataset.kind === 'file');
  generic.input.files = [{name: '升级.bin', size: 32}];
  await generic.send.onclick();
  assert.match(t.state.sent[1].url, /^\/file-upload\?path=.+&overwrite=1$/);
  t = await setup({mode: 'sta'});
  assert.equal(t.nodes.get('entries').children.length, 2);
  t.nodes.get('entries').children[1].children[1].children.find(b=>b.textContent==='复制').onclick();
  t.nodes.get('editorName').value = 'readme 副本.txt';
  await t.nodes.get('editorConfirm').onclick();
  assert.deepEqual(t.state.mutations[0], {action: 'copy', path: 'readme.txt', target: 'readme 副本.txt'});
  t.nodes.get('wallpaperFile').files = [{name: 'photo.heic', size: 3000000}];
  t.nodes.get('wallpaperFile').onchange();
  await flush();
  assert.equal(t.nodes.get('wallpaperSend').disabled, false);
  t.nodes.get('cropZoom').value = '1.5';
  t.nodes.get('cropZoom').oninput();
  const beforeDrag = vm.runInContext('[cropX,cropY]', t.context);
  t.nodes.get('cropPreview').onpointerdown({pointerId: 1, clientX: 100, clientY: 100});
  t.nodes.get('cropPreview').onpointermove({pointerId: 1, clientX: 120, clientY: 90});
  t.nodes.get('cropPreview').onpointerup();
  const afterDrag = vm.runInContext('[cropX,cropY]', t.context);
  assert.notDeepEqual(afterDrag, beforeDrag);
  await t.nodes.get('wallpaperSend').onclick();
  assert.equal(t.state.sent.at(-1).url, '/image-upload?name=photo.jpg');
  assert.match(t.nodes.get('wallpaperResult').textContent, /已保存为锁屏壁纸/);
  assert.equal(t.state.appearancePosts.at(-1).op,'wallpaper');
  t=await setup();t.nodes.get('bannerContent').value='网页编辑';
  await vm.runInContext('livePreview()',t.context);assert.equal(t.state.appearancePosts.at(-1).op,'preview');
  assert.match(t.nodes.get('previewLabel').textContent,/实时设备排版/);assert.equal(t.state.appearance.banner.text,'自由阅读');
  await t.nodes.get('bannerSave').onclick();assert.equal(t.state.appearance.banner.text,'网页编辑');assert.match(t.nodes.get('bannerResult').textContent,/已保存到设备/);
  t.nodes.get('bannerContent').value='好'.repeat(64);const posts=t.state.appearancePosts.length;await t.nodes.get('bannerSave').onclick();assert.equal(t.state.appearancePosts.length,posts);
  vm.runInContext("path='markdown';loadDirectory()",t.context);await flush();await flush();
  const protectedButtons=t.nodes.get('entries').children[0].children[1].children.filter(x=>x.dataset.protected==='1');assert.equal(protectedButtons.length,3);
  vm.runInContext('lock(true);lock(false)',t.context);assert.ok(protectedButtons.every(x=>x.disabled));
  assert.ok(t.nodes.get('entries').children[0].children[1].children.some(x=>x.textContent==='下载'));
  t=await setup();t.nodes.get('bannerLocalFont').files=[{name:'电脑字体.ttf',size:1024}];await t.nodes.get('bannerLocalFont').onchange();
  assert.match(t.state.sent.at(-1).url,/^\/font-stage\?name=[a-f0-9]{32}\.ttf$/);
  assert.match(t.nodes.get('bannerFont').value,/^\/sdcard\/\.web-fonts\//);
  await vm.runInContext('livePreview()',t.context);assert.equal(t.state.appearancePosts.at(-1).op,'preview');
  await t.nodes.get('bannerSave').onclick();assert.ok(t.state.appearance.banner.font.startsWith('/sdcard/fonts/'));assert.ok(t.state.appearancePosts.some(p=>p.op==='font-release'));
  t=await setup({confirmations:[false]});t.nodes.get('bannerLocalFont').files=[{name:'另一字体.ttf',size:1024}];await t.nodes.get('bannerLocalFont').onchange();
  await t.nodes.get('bannerSave').onclick();assert.equal(t.state.appearance.banner.font,'');
  await t.nodes.get('bannerCancel').onclick();assert.ok(t.state.appearancePosts.some(p=>p.op==='font-release'));
  t=await setup();t.state.holdPreview=true;
  const pending=vm.runInContext('livePreview()',t.context);await flush();
  const writes=t.nodes.get('bannerPreview').pixelWrites||0;
  t.nodes.get('bannerContent').value='最新草稿';t.nodes.get('bannerContent').oninput();
  await vm.runInContext('livePreview()',t.context);assert.equal(t.state.appearancePosts.length,1);
  t.state.resolvePreview();await pending;
  assert.equal(t.nodes.get('bannerPreview').pixelWrites||0,writes,'stale response must not paint');
  t.state.holdPreview=false;await vm.runInContext('livePreview()',t.context);
  assert.equal(t.nodes.get('bannerPreview').pixelWrites,writes+1);
  assert.equal(t.state.appearancePosts.at(-1).body.text,'最新草稿');
  assert.equal(t.state.appearance.banner.text,'自由阅读','preview never persists settings');
  assert.doesNotMatch(html,/id="bannerConfirm"/);assert.doesNotMatch(source,/ctx.font=px/);
  // 中文：不依赖本地MIME识别；验证原名、真实输出格式、重名序号和失败保留。
  // English: Verify picker extensions, original names, actual output MIME, numbered conflicts and failure preservation.
  const wallpaperPicker=html.match(/<input[^>]+id="wallpaperFile"[^>]+>/)[0];
  assert.match(wallpaperPicker,/accept="[^"]*\.jpg[^"]*\.jpeg[^"]*\.png/);
  for(const [name,type] of [['雪山.jpg','image/jpeg'],['旅行照片.JPEG','image/jpeg'],['桌面壁纸.PNG','image/png']]){
    t=await setup();t.nodes.get('wallpaperFile').files=[{name,size:1000}];
    t.nodes.get('wallpaperFile').onchange();await flush();await t.nodes.get('wallpaperSend').onclick();
    assert.equal(new URL('http://pico'+t.state.sent[0].url).searchParams.get('name'),name);
    assert.equal(t.state.sent[0].file.type,type);
    assert.equal(t.state.appearance.wallpaper,'/sdcard/pictures/'+name);
  }
  t=await setup({responses:[{status:409,body:{error:'同名文件已存在'}},{status:409,body:{error:'同名文件已存在'}},{status:200,body:{ok:true}}]});
  t.nodes.get('wallpaperFile').files=[{name:'雪山.jpg',size:1000}];t.nodes.get('wallpaperFile').onchange();await flush();await t.nodes.get('wallpaperSend').onclick();
  assert.deepEqual(t.state.sent.map(s=>new URL('http://pico'+s.url).searchParams.get('name')),['雪山.jpg','雪山 (1).jpg','雪山 (2).jpg']);
  assert.ok(t.state.sent.every(s=>!s.url.includes('overwrite')));
  assert.equal(t.state.appearance.wallpaper,'/sdcard/pictures/雪山 (2).jpg');
  t=await setup({responses:[{status:507,body:{error:'存储空间不足'}}]});
  t.nodes.get('wallpaperFile').files=[{name:'原名.png',size:1000}];t.nodes.get('wallpaperFile').onchange();await flush();await t.nodes.get('wallpaperSend').onclick();
  assert.equal(t.state.sent.length,1);assert.equal(t.state.appearance.wallpaper,'');
  assert.ok(!t.state.appearancePosts.some(p=>p.op==='wallpaper'));
  t=await setup();await vm.runInContext("useDeviceImage('wallpaper','pictures/设备原图.jpg')",t.context);await t.nodes.get('wallpaperSend').onclick();
  assert.equal(new URL('http://pico'+t.state.sent[0].url).searchParams.get('name'),'设备原图.jpg');
  assert.equal(t.state.appearance.wallpaper,'/sdcard/pictures/设备原图.jpg');
  console.log('transfer web: uploads/file actions, native preview/save, wallpaper names/MIME/numbered conflicts, failure preservation, text bounds, download and protected actions passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
