#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：实际C代码在独立进程间保存/恢复，覆盖内部存储失败、双槽损坏和旧备份。
# English: Real C persistence across fresh processes, covering NVS failure, slot corruption and legacy backups.
from pathlib import Path
import json,os,shutil,struct,subprocess,tempfile
R=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='progress-sd-',dir=R/'build') as tmp:
    base=Path(tmp);card=base/'card';card.mkdir();out=base/'build/simulator';out.mkdir(parents=True)
    binary=base/'progress'
    flags=['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function',
        '-fsanitize=address,undefined','-fno-omit-frame-pointer','-ffunction-sections','-fdata-sections',
        '-DPICO_SIM_CARD_PATH="unused"','-include','tools/simulator/fs.h']
    for inc in ('tools/simulator/include','main','main/book','main/ui') : flags+=['-I'+inc]
    sources=['tools/progress_sd_host_test.c','main/book/book_progress.c','main/book/book_history_backup.c',
        'main/book/content_library.c','tools/simulator/fs.c','tools/simulator/nvs.c']
    subprocess.run(flags+sources+['-Wl,-dead_strip','-o',str(binary)],cwd=R,check=True)
    env={**os.environ,'PICO_SIM_CARD_ROOT':str(card)}
    def run(*args,extra=None,ok=True):
        p=subprocess.run([str(binary),*map(str,args)],cwd=base,env={**env,**(extra or {})},capture_output=True,text=True)
        assert (p.returncode==0)==ok,(args,p.returncode,p.stdout,p.stderr)
        assert 'Sanitizer' not in p.stderr,p.stderr
        return p.stdout.split('err=')[0].strip()
    def key(path):
        h=2166136261
        for v in path.encode():h=((h^v)*16777619)&0xffffffff
        return f'b_{h:08x}'
    def fixture(path,pct,seq):
        ns=out/'nvs/rp_books';ns.mkdir(parents=True,exist_ok=True)
        data=bytearray(24);data[:4]=b'RPB\2';struct.pack_into('<I',data,4,1000)
        data[14]=48;data[15]=pct;struct.pack_into('<I',data,16,seq)
        b=path.encode()+b'\0';struct.pack_into('<H',data,22,len(b));(ns/key(path)).write_bytes(data+b)
        (ns/'seq').write_bytes(struct.pack('<I',seq));(ns/'last').write_bytes(b)
        return ns
    old='/sdcard/books/旧书.epub';new='/sdcard/markdown/新文档.md';book='/sdcard/books/新书.epub'
    ns=fixture(old,23,90)
    assert '23 90' in run('load',old,1000)
    assert old in run('list')
    # 旧NVS备份仍可导出/恢复；新增TF记录不要求内部commit成功。
    # Legacy NVS backups remain readable; new SD writes do not require NVS commits.
    run('backup');legacy_backup=(out/'history.bin').read_bytes()
    before={p.name:p.read_bytes() for p in ns.iterdir() if p.is_file()}
    failing={'PICO_TEST_NVS_COMMIT_FAIL':'always'}
    run('save',new,37,345,2000,extra=failing)
    run('save',book,62,567,3000,extra=failing)
    assert before=={p.name:p.read_bytes() for p in ns.iterdir() if p.is_file()}
    assert '37 91' in run('load',new,2000) and '62 92' in run('load',book,3000)
    assert run('last')==book
    assert not run('load',book,3001)
    assert len(run('list').splitlines())==3
    run('save',new,55,789,2000,extra=failing)
    assert '55 93' in run('load',new,2000) and run('last')==new
    # 短写损坏非最新槽，重启后保留上一个完整位置；下一次重试可修复。
    # A short inactive-slot write leaves the previous position intact across a restart; retries recover.
    run('save',new,77,999,2000,extra={'PROGRESS_FAULT':'write','PROGRESS_FAULT_AT':'2'},ok=False)
    assert '55 93' in run('load',new,2000)
    run('save',new,78,1000,2000);assert '78 95' in run('load',new,2000)
    run('save',new,79,1001,2000,extra={'PROGRESS_FAULT':'sync','PROGRESS_FAULT_AT':'2'},ok=False)
    assert '79 96' in run('load',new,2000) # 完整但未确认成功，允许读回；调用方仍应保留dirty。
    # Both damaged slots never fall back to stale legacy NVS; deletion mirrors tombstones.
    run('save',old,45,500,1000)
    store=card/'.readpico/progress';slots=[store/f'{key(old)}.{suffix}' for suffix in ('a','b')]
    for f in slots:f.write_bytes(b'torn')
    assert not run('load',old,1000) and old not in run('list')
    # Forget must not erase unknown/corrupt owner records without validation.
    run('forget',old,ok=False)
    for f in slots:f.unlink()
    run('forget',old)
    slots[0].write_bytes(b'torn')
    assert not run('load',old,1000) and old not in run('list')
    run('save',book,66,600,3000);run('set-last',new);run('forget',book)
    assert run('last')==new and not run('load',book,3000)
    # New backup contains authoritative SD values; old NVS last/sequence do not leak back.
    run('backup');run('save',new,88,1100,2000);run('restore')
    assert '79 96' in run('load',new,2000) and run('last')==new
    (out/'history.bin').write_bytes(legacy_backup);run('restore')
    assert '23 90' in run('load',old,1000) and run('last')==old
    assert new not in run('list') and book not in run('list')
    run('save',new,24,400,2000);assert '24 91' in run('load',new,2000)
    run('forget',new);assert not run('last')
    # A card write failure must be reported, not silently sent to full internal storage.
    (store/'seq.a').unlink(missing_ok=True);(store/'seq.b').unlink(missing_ok=True)
    fault={'PICO_TEST_FOPEN_WRITE_FAIL':'/sdcard/.readpico/progress/seq.a'}
    run('save',book,12,100,3000,extra=fault,ok=False)
    print('PASS: fresh-process SD resume/recency; NVS unwritable; old-NVS migration; size guards; short-write/sync faults; CRC/tombstones; old/new backup replacement; no silent fallback.')
