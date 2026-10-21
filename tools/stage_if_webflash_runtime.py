#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Stage the verified-bundle installer for an existing IF release; never build or publish firmware."""
from pathlib import Path
import argparse,hashlib,json,re,shutil
ROOT=Path(__file__).resolve().parents[1]
def stage(site,version):
    folder=site/'webflash'/version
    manifest=json.loads((folder/'manifest.json').read_text())
    if manifest['name']!='if-ink OS' or manifest['version']!=version:
        raise ValueError('IF manifest identity mismatch')
    build=manifest['builds'][0]
    expected={'bootloader.bin':0,'partition-table.bin':0x8000,'firmware.bin':0x10000,'ota-data-initial.bin':0xd10000}
    if len(manifest['builds'])!=1 or build['chipFamily']!='ESP32-S3' or len(build['parts'])!=4:
        raise ValueError('Unsupported bundle layout')
    seen=set()
    for part in build['parts']:
        name=part['path']
        if name not in expected or name in seen or part['offset']!=expected[name]:raise ValueError('Invalid part')
        seen.add(name)
        data=(folder/name).read_bytes()
        if len(data)!=part['size'] or hashlib.sha256(data).hexdigest()!=part['sha256']:raise ValueError('Part integrity mismatch')
        if name=='firmware.bin':
            if data[48:80].split(b'\0')[0].decode()!=version or data[80:112].split(b'\0')[0]!=b'Read_Pico_IF':raise ValueError('App identity mismatch')
            app=part
    source=(ROOT/'tools/if_webflash/installer.js').read_text()
    source=re.sub(r"const expectedVersion = '[^']+';",f"const expectedVersion = '{version}';",source)
    source=re.sub(r"part.size !== \d+",f"part.size !== {app['size']}",source)
    source=re.sub(r"part.sha256 !== '[a-f0-9]{64}'",f"part.sha256 !== '{app['sha256']}'",source)
    (site/'webflash/installer.js').write_text(source)
    shutil.copyfile(ROOT/'tools/if_webflash/install-button.js',site/'webflash/vendor/esp-web-tools-if-v1/install-button.js')
    print('Staged verified IF installer runtime:',version,'(no firmware build/publication)')
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('site',type=Path);parser.add_argument('version')
    args=parser.parse_args();stage(args.site,args.version)
