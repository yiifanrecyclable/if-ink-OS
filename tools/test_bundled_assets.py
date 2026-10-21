#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：隔离TF验证内置素材释放、重复执行与损坏重试，不操作用户配置。
English: Verify bundled extraction, idempotence and corrupt retries on isolated media/preferences.
"""
from pathlib import Path
import os,shutil,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(dir=ROOT/'build',prefix='bundled-assets-') as d:
    work=Path(d);(work/'build/simulator').mkdir(parents=True)
    shutil.copyfile(ROOT/'main/bundled_assets.c',work/'bundled_assets.c')
    (work/'assets').mkdir();header=(ROOT/'main/assets/bundled_banner.h').read_text();(work/'assets/bundled_banner.h').write_text(header)
    (work/'test.c').write_text('''#include "bundled_assets.h"
#include <stdio.h>
int main(void){printf("%d\\n",bundled_assets_install());return 0;}
''')
    def build():
        subprocess.run(['cc','-std=c11','-D_DEFAULT_SOURCE','-DPICO_HOST_SIMULATOR=1','-DPICO_SIM_CARD_PATH="/unavailable"','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g','-include',str(ROOT/'tools/simulator/fs.h'),'-I'+str(ROOT/'tools/simulator/include'),'-I'+str(ROOT/'main'),str(work/'test.c'),str(work/'bundled_assets.c'),str(ROOT/'tools/simulator/fs.c'),'-lz','-o',str(work/'test')],check=True)
    def run():
        r=subprocess.run([str(work/'test')],cwd=work,env={**os.environ,'PICO_SIM_CARD_ROOT':str(work/'card'),'PICO_TEST_FATFS_RENAME':'1','ASAN_OPTIONS':'detect_leaks=0'},capture_output=True,text=True,check=True)
        assert not r.stderr,r.stderr
        return int(r.stdout)
    build();assert run()!=0 and not (work/'card').exists()
    card=work/'card';card.mkdir();prefs=card/'preferences.backup';prefs.write_bytes(b'unchanged Banner preferences')
    target=card/'banner pic/思考大猩猩.png';partial=card/'banner pic/.思考大猩猩.png.part'
    assert run()==0 and target.read_bytes()==(ROOT/'assets/banner/default-banner.png').read_bytes()
    before=target.stat().st_mtime_ns;assert run()==0 and target.stat().st_mtime_ns==before
    target.write_bytes(b'user artwork');assert run()==0 and target.read_bytes()==b'user artwork'
    target.unlink();partial.write_bytes(b'interrupted');assert run()==0 and not partial.exists()
    target.unlink();target.mkdir();assert run()!=0 and target.is_dir();target.rmdir()
    # 使用同一生产模块，唯独替换为损坏的内置资源，确认不安装残片。/ Same production module with a corrupt resource must not expose a partial image.
    (work/'assets/bundled_banner.h').write_text(header.replace('0x78','0x79',1));build()
    assert run()!=0 and not target.exists() and not partial.exists()
    (work/'assets/bundled_banner.h').write_text(header);build();assert run()==0
    assert prefs.read_bytes()==b'unchanged Banner preferences'
print('PASS: exact PNG extraction, no-card deferral, no settings changes, existing-file preservation, interrupted retry and corrupt archive rejection')
