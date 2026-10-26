/* SPDX-License-Identifier: Apache-2.0
 * 中文：校验IF安装清单，加载同源刷机工具；只有用户点击后才请求串口。
 * English: validate the IF manifest and load local flashing tools; request a port only after a user click.
 */
const button = document.getElementById('flash-installer');
const status = document.getElementById('flash-status');
const expectedVersion = '0.1.6';
const expectedParts = new Map([
  ['bootloader.bin', 0], ['partition-table.bin', 0x8000],
  ['firmware.bin', 0x10000], ['ota-data-initial.bin', 0xd10000],
]);
const regionEnds = new Map([
  ['bootloader.bin', 0x8000], ['partition-table.bin', 0x9000],
  ['firmware.bin', 0x410000], ['ota-data-initial.bin', 0xd12000],
]);
const unavailable = message => { button.hidden = true; status.textContent = message; };

// 校验实际文件后只向安装器提供不可变快照，失败不打开串口或擦除。
// Give the installer immutable verified bytes; failures never open or erase the device.
async function prepareBundleSnapshot(manifest, manifestURL) {
  const snapshot = JSON.parse(JSON.stringify(manifest));
  const urls = [];
  const dispose = () => { while (urls.length) URL.revokeObjectURL(urls.pop()); };
  try {
    const results = await Promise.allSettled(snapshot.builds[0].parts.map(async part => {
      const target = new URL(part.path, manifestURL);
      if (target.origin !== new URL(manifestURL).origin) throw Error('刷机文件来源不匹配');
      target.searchParams.set('sha256', part.sha256);
      const response = await fetch(target.href, { cache: 'no-store' });
      if (!response.ok) throw Error('刷机文件下载失败，尚未写入设备');
      const data = await response.arrayBuffer();
      const hash = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', data)))
        .map(value => value.toString(16).padStart(2, '0')).join('');
      if (data.byteLength !== part.size || hash !== part.sha256)
        throw Error('刷机文件校验失败，请刷新重试；尚未写入设备');
      if (part.path === 'firmware.bin') {
        const bytes = new Uint8Array(data);
        const field = offset => new TextDecoder().decode(bytes.slice(offset, offset + 32)).split('\0')[0];
        if (bytes.length < 112 || bytes[0] !== 0xe9 ||
            new DataView(data).getUint32(32, true) !== 0xabcd5432 ||
            field(48) !== snapshot.version || field(80) !== 'Read_Pico_IF')
          throw Error('固件型号或版本不匹配，尚未写入设备');
      }
      const url = URL.createObjectURL(new Blob([data], { type: 'application/octet-stream' }));
      urls.push(url); part.path = url;
    }));
    const failed = results.find(result => result.status === 'rejected');
    if (failed) throw failed.reason;
    const url = URL.createObjectURL(new Blob([JSON.stringify(snapshot)], { type: 'application/json' }));
    urls.push(url);
    return { url, dispose };
  } catch (error) { dispose(); throw error; }
}

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
      if (part.path === 'firmware.bin' && (part.size !== 3717088 ||
          part.sha256 !== '3309df71c9c130c261fa5933084d7d3deb91f6af7f7a715b8abd570df478f7df')) throw new Error('image');
      seen.add(part.path);
    }
    button.prepareManifest = () => prepareBundleSnapshot(manifest, new URL(button.getAttribute('manifest'), document.baseURI).href);
    await import('./vendor/esp-web-tools-if-v1/install-button.js');
    await customElements.whenDefined('esp-web-install-button');
    button.hidden = false;
    status.textContent = '连接后确认安装，完成前保持数据线连接。';
  } catch {
    unavailable('刷机工具未能加载，请刷新重试，或使用下方手动安装包。');
  }
}
void start();
