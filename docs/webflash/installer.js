/* SPDX-License-Identifier: Apache-2.0
 * 中文：校验IF安装清单，加载同源刷机工具；只有用户点击后才请求串口。
 * English: validate the IF manifest and load local flashing tools; request a port only after a user click.
 */
const button = document.getElementById('flash-installer');
const status = document.getElementById('flash-status');
const expectedVersion = '0.1.2';
const expectedParts = new Map([
  ['bootloader.bin', 0], ['partition-table.bin', 0x8000],
  ['firmware.bin', 0x10000], ['ota-data-initial.bin', 0xd10000],
]);
const regionEnds = new Map([
  ['bootloader.bin', 0x8000], ['partition-table.bin', 0x9000],
  ['firmware.bin', 0x410000], ['ota-data-initial.bin', 0xd12000],
]);
const unavailable = message => { button.hidden = true; status.textContent = message; };

async function start() {
  if (!window.isSecureContext) return unavailable('请打开 HTTPS 正式刷机页。');
  if (!navigator.serial || typeof navigator.serial.requestPort !== 'function') return unavailable('在线安装请使用电脑端 Chrome 或 Edge；其他浏览器可使用下方手动安装包。');
  try {
    const response = await fetch(button.getAttribute('manifest'), { cache: 'no-store' });
    if (!response.ok) throw new Error('manifest');
    const manifest = await response.json();
    const build = manifest.builds?.[0];
    if (manifest.name !== 'if-ink OS' || manifest.version !== expectedVersion ||
        manifest.builds.length !== 1 || build.chipFamily !== 'ESP32-S3' ||
        manifest.new_install_prompt_erase !== true || manifest.new_install_improv_wait_time !== 0 ||
        build.improv !== false || build.parts.length !== 4) throw new Error('target');
    const seen = new Set();
    for (const part of build.parts) {
      if (!expectedParts.has(part.path) || part.offset !== expectedParts.get(part.path) ||
          seen.has(part.path) || !Number.isInteger(part.size) || part.size <= 0 ||
          !/^[a-f0-9]{64}$/.test(part.sha256) ||
          Math.ceil((part.offset + part.size) / 4096) * 4096 > regionEnds.get(part.path)) throw new Error('parts');
      if (part.path === 'firmware.bin' && (part.size !== 3567472 ||
          part.sha256 !== '861a597b21797bdb5f52f722d45dd0f0be565bb0b31ee03a07c4dcd115a966dd')) throw new Error('image');
      seen.add(part.path);
    }
    await import('./vendor/esp-web-tools-if-v1/install-button.js');
    await customElements.whenDefined('esp-web-install-button');
    button.hidden = false;
    status.textContent = '连接后确认安装，完成前保持数据线连接。';
  } catch {
    unavailable('刷机工具未能加载，请刷新重试，或使用下方手动安装包。');
  }
}
void start();
