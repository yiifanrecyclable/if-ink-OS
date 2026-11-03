#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""真实逻辑存储的安全迁移回归；条目预算为故障模型，并非ESP闪存物理仿真。
Real logical-storage migration; the entry budget is a fault model, not ESP flash simulation.
"""
from pathlib import Path
import tempfile,subprocess,os,sys
R=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='records-capacity-',dir=R/'build') as tmp:
 p=Path(tmp);binary=p/'check'
 flags=['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-misleading-indentation','-DPICO_SIM_CARD_PATH="unused"','-DPICO_HOST_SIMULATOR','-fsanitize=address,undefined','-g','-ffunction-sections','-fdata-sections','-include',str(R/'tools/simulator/fs.h')]
 for inc in ['tools/simulator/include','main','main/book','main/ui','main/font','main/cloud','components/read_pico_transfer/include']:flags+=['-I'+str(R/inc)]
 sources=['tools/records_capacity_host_test.c','tools/simulator/nvs.c','tools/simulator/fs.c','main/book/record_store.c','main/book/storage_maintenance.c','main/book/book_progress_kv.c','main/book/book_progress.c','main/book/book_history_backup.c','main/book/content_library.c','main/book/book_title.c','main/book/reader_input.c','main/ui/ui_i18n.c','main/settings.c','main/wallpaper_preferences.c']
 subprocess.run(flags+[str(R/x) for x in sources]+(['-Wl,-dead_strip'] if sys.platform=='darwin' else ['-Wl,--gc-sections','-lpthread'])+['-o',str(binary)],check=True)
 def setup(name):
  d=p/name;(d/'build/simulator').mkdir(parents=True);card=d/'card';card.mkdir();return d,{**os.environ,'PICO_SIM_CARD_ROOT':str(card),'ASAN_OPTIONS':'detect_leaks=0'}
 def run(d,env,mode,**fault):
  subprocess.run([str(binary),mode],cwd=d,env={**env,**fault},check=True)
  for file in (d/'card').rglob('*'):
   if file.is_file():assert b'private-do-not-copy' not in file.read_bytes(),file
 d,env=setup('main')
 for case in ['seed','no-card','recover','read','backup','read','growth-limit','read']:run(d,env,case)
 # 分别让恢复副本、目标写入、NVS删除和提交失败，然后在新进程重试。/ Fail recovery, target, source erase and commit, retrying each in a fresh process.
 faults=[{'PICO_TEST_FOPEN_WRITE_FAIL':'/sdcard/.readpico/records/rp_books/recovery-last.bin.tmp'},
         {'PICO_TEST_FOPEN_WRITE_FAIL':'/sdcard/.readpico/progress/last.a'},
         {'PICO_TEST_FOPEN_READ_FAIL':'/sdcard/.readpico/records/rp_books/recovery-last.bin.tmp'},
         {'PICO_TEST_FSYNC_FAIL':'1'}, {'PICO_TEST_NVS_ERASE_FAIL':'last'}, {'PICO_TEST_NVS_COMMIT_FAIL':'1'},
         {'PICO_TEST_NVS_GET_FAIL':'last'}, {'PICO_TEST_RENAME_FAIL_TO':'/sdcard/.readpico/records/rp_books/recovery-last.bin'}]
 for i,fault in enumerate(faults):
  d,env=setup('fault'+str(i));run(d,env,'seed');run(d,env,'fault',**fault);run(d,env,'recover');run(d,env,'read')
 d,env=setup('changed');run(d,env,'seed');run(d,env,'source-changed',PICO_TEST_NVS_MUTATE_SOURCE='last')
 d,env=setup('bad-domain');run(d,env,'seed');run(d,env,'bad-domain')
 d,env=setup('corrupt');run(d,env,'seed');run(d,env,'corrupt')
print('PASS: content retirement under full-entry model, eight fault stages/retries, source mutation, corrupt slots, no-card, growth ceiling, backup/restore, fresh-process durability')
