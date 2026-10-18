/* SPDX-License-Identifier: Apache-2.0
 * 中文：验证真实网页入口/擦除策略与镜像，使用假串口而非实际设备。
 * English: verify production web entry, erase policy and images with fake serial, never a physical device.
 */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const crypto = require('node:crypto');
const root = path.resolve(__dirname, '..');
const site = process.argv[2] || path.join(root, 'public/if-edition');
const version = process.argv[3] || '0.1.2';
const folder = path.join(site, 'webflash', version);
const manifest = JSON.parse(fs.readFileSync(path.join(folder, 'manifest.json')));
const dialog = fs.readFileSync(path.join(site, 'webflash/vendor/esp-web-tools-if-v1/install-dialog-im156JnI.js'), 'utf8');
const original = fs.readFileSync(path.join(root, 'flash/vendor/web/install-dialog-im156JnI.js'), 'utf8');
const originalBranch = 'this._manifest.new_install_prompt_erase?this._state="ASK_ERASE":this._startInstall(!0)';
assert.equal(original.split(originalBranch).length - 1, 2);
assert.equal(dialog, original.replaceAll(originalBranch, 'this._startInstall(!1)').replace(
  '_startInstall(e){this._state="INSTALL",this._installErase=e,this._installConfirmed=!1}',
  '_startInstall(e){this._state="INSTALL",this._installErase=!1,this._installConfirmed=!1}'));
const method = dialog.match(/_startInstall\(e\)\{([^}]*)\}/)[1];
for (const erase of [true, false]) {
  const instance = {};
  new Function('e', method).call(instance, erase);
  assert.equal(instance._installErase, false);
  assert.equal(instance._state, 'INSTALL');
  assert.equal(instance._installConfirmed, false);
}
const expected = [[0, 0x8000], [0x8000, 0x9000], [0x10000, 0x410000], [0xd10000, 0xd12000]];
for (const [i, part] of manifest.builds[0].parts.entries()) {
  const bytes = fs.readFileSync(path.join(folder, part.path));
  assert.equal(bytes.length, part.size);
  assert.equal(crypto.createHash('sha256').update(bytes).digest('hex'), part.sha256);
  assert.equal(part.offset, expected[i][0]);
  assert.ok(Math.ceil((part.offset + bytes.length) / 4096) * 4096 <= expected[i][1]);
  if (part.path === 'bootloader.bin' || part.path === 'firmware.bin') {
    assert.equal(bytes[0], 0xe9); assert.equal(bytes[2], 2); assert.equal(bytes[3], 0x4f);
    assert.equal(bytes.readUInt16LE(12), 9);
  }
  if (part.path === 'firmware.bin') {
    assert.equal(bytes.subarray(48, 80).toString().split('\0')[0], manifest.version);
    assert.equal(bytes.subarray(80, 112).toString().split('\0')[0], 'Read_Pico_IF');
  }
}
const source = fs.readFileSync(path.join(site, 'webflash/installer.js'), 'utf8');
async function entry(change = {}) {
  let imported = 0, serialRequested = 0;
  const button = { hidden: true, getAttribute: () => `./webflash/${version}/manifest.json` };
  const status = { textContent: '' };
  const config = structuredClone(manifest);
  if (change.mutate) change.mutate(config);
  const context = {
    document: { getElementById: id => id === 'flash-installer' ? button : status },
    window: { isSecureContext: change.secure !== false },
    navigator: change.serial === false ? {} : { serial: { requestPort: () => { serialRequested++; } } },
    fetch: async () => ({ ok: change.http !== false, json: async () => config }),
    customElements: { whenDefined: async () => { if (change.moduleFailure) throw Error('module'); } },
    loadTool: async () => { imported++; },
  };
  // 中文：仅替换模块加载器，保留实际入口校验与状态分支。/ English: replace only the module loader, retaining production validation/state branches.
  const result = new vm.Script(source.replace("import('./vendor/esp-web-tools-if-v1/install-button.js')", 'loadTool()').replace('void start();', 'start();')).runInNewContext(context);
  await result;
  assert.equal(serialRequested, 0, 'Page loading must never request a serial port');
  return { button, status, imported };
}
(async () => {
  assert.equal((await entry()).button.hidden, false);
  for (const change of [
    { secure: false }, { serial: false }, { http: false }, { moduleFailure: true },
    { mutate: m => { m.version = 'old'; } },
    { mutate: m => { m.builds[0].chipFamily = 'ESP32'; } },
    { mutate: m => { m.builds[0].parts[2].offset = 0x9000; } },
    { mutate: m => { m.builds[0].parts[2].size = 0x800000; } },
    { mutate: m => { m.builds[0].parts[2].sha256 = '0'.repeat(64); } },
    { mutate: m => { m.builds[0].parts[3] = m.builds[0].parts[0]; } },
  ]) assert.equal((await entry(change)).button.hidden, true);
  console.log('PASS: images/chip/flash ranges, no erase on either install path, 11 browser entry cases, no automatic serial access');
})().catch(error => { console.error(error); process.exitCode = 1; });
