# SPDX-License-Identifier: Apache-2.0
"""Production manifests and recoverable commits in temporary TF/NVS storage.
No network, accounts, or original notes are used.
"""
from pathlib import Path
import hashlib,json,shutil,subprocess,tempfile,unittest,os,sys,copy,struct,zlib
sys.path.insert(0,str(Path(__file__).resolve().parent/'simulator'))
from server import ROOT,BUILD,CARD,STORES
BINARY=BUILD/'cloud-host'
REPOSITORY=ROOT
TEST_BUILD=BUILD
TARGET='/sdcard/markdown/unittest-sync.md'
class CloudHostTests(unittest.TestCase):
    def setUp(self):
        global ROOT,BUILD,CARD
        self.scratch=tempfile.TemporaryDirectory(dir=TEST_BUILD,prefix='cloud-isolated-')
        ROOT=Path(self.scratch.name);BUILD=ROOT/'build/simulator';CARD=ROOT/'card'
        BUILD.mkdir(parents=True);CARD.mkdir()
        target=ROOT/'plugins/obsidian-pico/tests/fixtures'
        shutil.copytree(REPOSITORY/'plugins/obsidian-pico/tests/fixtures',target)
    def restore(self):
        # 仅重置当前测试临时卡和NVS。/ Reset only this test's temporary card and NVS.
        shutil.rmtree(CARD);CARD.mkdir()
        nvs=BUILD/'nvs'
        if nvs.exists():shutil.rmtree(nvs)
    def tearDown(self):self.scratch.cleanup()
    def command(self,*args,faults=None):
        p=subprocess.run([str(BINARY),*map(str,args)],cwd=ROOT,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','PICO_SIM_CARD_ROOT':str(CARD),'PICO_TEST_FATFS_RENAME':'1',**(faults or {})},capture_output=True,text=True,timeout=30)
        self.assertEqual(p.returncode,0,p.stderr);self.assertNotIn('runtime error:',p.stderr);self.assertNotIn('AddressSanitizer',p.stderr)
        return json.loads(p.stdout)
    def plan(self,body,assets=()):
        plan={'body':{'sha256':hashlib.sha256(body).hexdigest(),'bytes':len(body)},'assets':[]}
        stage=CARD/'.pico-sync/current';(stage/'body').write_bytes(body)
        for name,data in assets:
            (stage/'attachments'/name).write_bytes(data)
            plan['assets'].append({'name':name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()})
        file=BUILD/'cloud-plan-test.json';file.write_text(json.dumps(plan));return file
    def baseline(self,body=b'# old\n'):
        self.assertEqual(self.command('prepare')['error'],0)
        (CARD/'markdown/unittest-sync.md').write_bytes(body)
        self.assertEqual(self.command('refresh',TARGET)['error'],0)
        self.assertEqual(self.command('progress-save',TARGET)['error'],0)
        return (CARD/'markdown/.index/unittest-sync.md.pico').read_text().splitlines()[1]
    def test_plugin_interchange_and_manifest_boundaries(self):
        for name in ('sample-manifest.json','sample-image-manifest.json'):
            file=ROOT/'plugins/obsidian-pico/tests/fixtures'/name;obj=json.loads(file.read_text())
            self.assertEqual(self.command('manifest',file,obj['article_id'])['error'],0)
        original=json.loads((ROOT/'plugins/obsidian-pico/tests/fixtures/sample-manifest.json').read_text())
        for modifier in (lambda o:o['body'].update(path='../escape'),lambda o:o.update(source_filename='../bad.md'),lambda o:o.update(created_at='2026-02-30T00:00:00Z'),lambda o:o['body'].update(bytes=262145)):
            changed=copy.deepcopy(original);modifier(changed);file=BUILD/'manifest-boundary-test.json';file.write_text(json.dumps(changed));self.assertNotEqual(self.command('manifest',file,original['article_id'])['error'],0)
        file.write_text(json.dumps(original)[:-1]+',"title":"duplicate"}')
        self.assertNotEqual(self.command('manifest',file,original['article_id'])['error'],0)
        original.pop('source_filename');file.write_text(json.dumps(original))
        self.assertEqual(self.command('manifest',file,original['article_id'])['error'],0x106)
    def test_each_interruption_preserves_complete_old_or_new_and_progress(self):
        old=b'# old\n';new=b'# new\n'
        for point in range(1,7):
            with self.subTest(point=point):
                self.restore();first=self.baseline(old);plan=self.plan(new)
                self.assertNotEqual(self.command('commit',TARGET,plan,point)['error'],0)
                self.assertEqual(self.command('recover')['error'],0)
                self.assertEqual((CARD/'markdown/unittest-sync.md').read_bytes(),old if point<5 else new)
                self.assertEqual(self.command('progress-load',TARGET)['present'],point<5)
                self.assertEqual((CARD/'markdown/.index/unittest-sync.md.pico').read_text().splitlines()[1],first)
                self.assertEqual(self.command('recover')['error'],0)
    def test_resource_transactions_recover_and_reject_wrong_directories(self):
        fixtures=[('pictures','壁纸.png',(REPOSITORY/'assets/banner/default-banner.png').read_bytes()),
                  ('fonts','字体.ttf',(REPOSITORY/'main/assets/builtin.ttf').read_bytes())]
        for folder,name,old in fixtures:
            for point in range(1,7):
                with self.subTest(folder=folder,point=point):
                    self.restore();self.assertEqual(self.command('prepare')['error'],0)
                    plan=self.plan(old);target='/sdcard/'+folder+'/'+name
                    self.assertEqual(self.command('commit',target,plan)['error'],0)
                    new=old+b'padding';self.command('prepare');plan=self.plan(new)
                    self.assertNotEqual(self.command('commit',target,plan,point)['error'],0)
                    self.assertEqual(self.command('recover')['error'],0)
                    self.assertEqual((CARD/folder/name).read_bytes(),old if point<5 else new)
            self.command('prepare');plan=self.plan(old)
            for wrong in ['/sdcard/books/'+name,'/sdcard/markdown/'+name,'/sdcard/'+folder+'/nested/'+name]:
                self.assertNotEqual(self.command('commit',wrong,plan)['error'],0)
    def test_plugin_degraded_images_install_with_real_attachment(self):
        # 插件安全降级正文仍通过生产C安装；代码图片保持字面文字。
        # Plugin-degraded prose installs through production C while code images remain literal.
        fixtures=ROOT/'plugins/obsidian-pico/tests/fixtures'
        manifest_path=fixtures/'sample-degraded-manifest.json'
        manifest=json.loads(manifest_path.read_text())
        self.assertEqual(self.command('manifest',manifest_path,manifest['article_id'])['error'],0)
        body=(fixtures/'sample-degraded-body.md').read_bytes()
        self.assertEqual(len(body),manifest['body']['bytes'])
        self.assertEqual(hashlib.sha256(body).hexdigest(),manifest['body']['sha256'])
        asset=manifest['assets'][0];data=(fixtures/'sample-image.png').read_bytes()
        self.assertEqual(hashlib.sha256(data).hexdigest(),asset['sha256'])
        name=asset['path'].split('/')[-1]
        self.command('prepare');plan=self.plan(body,[(name,data)])
        self.assertEqual(self.command('commit',TARGET,plan)['error'],0)
        self.assertEqual((CARD/'markdown/unittest-sync.md').read_bytes(),body)
        self.assertEqual((CARD/'markdown/attachments'/name).read_bytes(),data)
        self.assertIn('（图片或嵌入未导出：远程图片）'.encode(),body)
        self.assertIn(b'`![',body)
        self.assertEqual(self.command('refresh',TARGET)['error'],0)
    def test_first_import_interruption_never_exposes_incomplete_body(self):
        body=b'# first\n'
        for point in range(1,7):
            self.restore();self.command('prepare');plan=self.plan(body)
            self.assertNotEqual(self.command('commit',TARGET,plan,point)['error'],0)
            self.assertEqual(self.command('recover')['error'],0)
            target=CARD/'markdown/unittest-sync.md'
            self.assertEqual(target.exists(),point>=5)
            if target.exists():self.assertEqual(target.read_bytes(),body)
    def test_noop_keeps_progress_and_changed_body_resets_it(self):
        old=b'# old\n';first=self.baseline(old);plan=self.plan(old)
        self.assertTrue(self.command('commit',TARGET,plan)['unchanged']);self.assertTrue(self.command('progress-load',TARGET)['present'])
        self.command('prepare');plan=self.plan(b'# new\n');self.assertEqual(self.command('commit',TARGET,plan)['error'],0)
        self.assertFalse(self.command('progress-load',TARGET)['present']);self.assertEqual((CARD/'markdown/.index/unittest-sync.md.pico').read_text().splitlines()[1],first)
    def test_lost_or_corrupt_journal_preserves_only_backup(self):
        self.baseline();plan=self.plan(b'# new\n');self.command('commit',TARGET,plan,3)
        journal=CARD/'.pico-sync/transaction';journal.unlink();backup=CARD/'.pico-sync/current/previous'
        self.assertTrue(backup.exists());self.assertNotEqual(self.command('recover')['error'],0);self.assertNotEqual(self.command('prepare')['error'],0);self.assertTrue(backup.exists())
    def test_truncated_installed_body_remains_gated(self):
        self.baseline();plan=self.plan(b'# new version\n');self.command('commit',TARGET,plan,5)
        (CARD/'markdown/unittest-sync.md').write_bytes(b'# truncated\n')
        self.assertNotEqual(self.command('recover')['error'],0);self.assertTrue((CARD/'.pico-sync/current/previous').exists());self.assertTrue((CARD/'.pico-sync/transaction').exists())
    def test_extra_asset_is_rejected_without_install(self):
        self.baseline();data=(ROOT/'plugins/obsidian-pico/tests/fixtures/sample-image.png').read_bytes()
        name='unittest-extra-0123456789abcdef.png';plan=self.plan(b'# new\n',[(name,data)])
        self.assertNotEqual(self.command('commit',TARGET,plan)['error'],0);self.assertEqual((CARD/'markdown/unittest-sync.md').read_bytes(),b'# old\n');self.assertFalse((CARD/'markdown/attachments'/name).exists())
    def test_image_bundle_and_missing_asset_preserve_the_old_article(self):
        self.baseline();data=(ROOT/'plugins/obsidian-pico/tests/fixtures/sample-image.png').read_bytes();name='unittest-picture-0123456789abcdef.png'
        body=f'# new\n\n![](attachments/{name})\n'.encode();plan=self.plan(body,[(name,data)])
        (CARD/'.pico-sync/current/attachments'/name).unlink();self.assertNotEqual(self.command('commit',TARGET,plan)['error'],0)
        self.assertEqual((CARD/'markdown/unittest-sync.md').read_bytes(),b'# old\n')
        plan=self.plan(body,[(name,data)]);self.assertEqual(self.command('commit',TARGET,plan)['error'],0);self.assertTrue((CARD/'markdown/attachments'/name).exists())
    def test_journal_crc_and_orphan_partial_keep_recovery_gated(self):
        self.baseline();plan=self.plan(b'# new\n');self.command('commit',TARGET,plan,3)
        journal=CARD/'.pico-sync/transaction';data=bytearray(journal.read_bytes());data[-1]^=1;journal.write_bytes(data)
        self.assertNotEqual(self.command('recover')['error'],0);self.assertTrue((CARD/'.pico-sync/current/previous').exists())
        self.restore();self.command('prepare');(CARD/'.pico-sync/transaction.part').write_bytes(b'partial')
        self.assertNotEqual(self.command('prepare')['error'],0);self.assertTrue((CARD/'.pico-sync/transaction.part').exists())
    def test_filesystem_eio_is_not_treated_as_missing_prior_body(self):
        self.baseline();plan=self.plan(b'# new\n')
        result=self.command('commit',TARGET,plan,faults={'PICO_TEST_LSTAT_EIO':TARGET})
        self.assertNotEqual(result['error'],0);self.assertEqual((CARD/'markdown/unittest-sync.md').read_bytes(),b'# old\n')
        self.assertFalse((CARD/'.pico-sync/transaction').exists())
        self.command('commit',TARGET,plan,3)
        self.assertNotEqual(self.command('recover',faults={'PICO_TEST_LSTAT_EIO':'/sdcard/.pico-sync/current/previous'})['error'],0)
        self.assertTrue((CARD/'.pico-sync/current/previous').exists());self.assertEqual(self.command('recover')['error'],0)
    def test_nvs_failure_gates_reads_until_recovery_and_one_shot_error_recovers(self):
        self.baseline();plan=self.plan(b'# new\n')
        result=self.command('commit',TARGET,plan,faults={'PICO_TEST_NVS_COMMIT_FAIL':'always'})
        self.assertNotEqual(result['error'],0);self.assertTrue((CARD/'.pico-sync/transaction').exists());self.assertTrue((CARD/'.pico-sync/current/previous').exists())
        self.assertNotEqual(self.command('refresh',TARGET,faults={'PICO_TEST_NVS_COMMIT_FAIL':'always'})['error'],0)
        self.assertEqual(self.command('recover')['error'],0);self.assertFalse(self.command('progress-load',TARGET)['present'])
        self.restore();self.baseline();plan=self.plan(b'# new\n')
        self.assertEqual(self.command('commit',TARGET,plan,faults={'PICO_TEST_NVS_COMMIT_FAIL':'once'})['error'],0)
    def test_installed_corrupt_asset_preserves_backup_and_gate(self):
        self.baseline();data=(ROOT/'plugins/obsidian-pico/tests/fixtures/sample-image.png').read_bytes();name='unittest-picture-0123456789abcdef.png'
        plan=self.plan(f'# new\n![](attachments/{name})\n'.encode(),[(name,data)]);self.command('commit',TARGET,plan,5)
        (CARD/'markdown/attachments'/name).write_bytes(b'corrupt')
        self.assertNotEqual(self.command('recover')['error'],0);self.assertTrue((CARD/'.pico-sync/current/previous').exists());self.assertTrue((CARD/'.pico-sync/transaction').exists())
    def test_part_only_checkpoint_promotes_before_forward_recovery(self):
        self.baseline();plan=self.plan(b'# new\n');self.command('commit',TARGET,plan,5)
        journal=CARD/'.pico-sync/transaction';part=journal.with_name('transaction.part');journal.rename(part)
        blocked=self.command('recover',faults={'PICO_TEST_RENAME_FAIL_TO':'/sdcard/.pico-sync/transaction'})
        self.assertNotEqual(blocked['error'],0);self.assertTrue(part.exists());self.assertTrue((CARD/'.pico-sync/current/previous').exists())
        self.assertEqual(self.command('recover')['error'],0)
        self.assertEqual((CARD/'markdown/unittest-sync.md').read_bytes(),b'# new\n');self.assertFalse(part.exists())
    def test_newer_same_transaction_part_wins_and_different_payload_is_gated(self):
        self.baseline();plan=self.plan(b'# new\n');self.command('commit',TARGET,plan,5)
        journal=CARD/'.pico-sync/transaction';record=bytearray(journal.read_bytes())
        part=journal.with_name('transaction.part');part.write_bytes(record)
        struct.pack_into('<I',record,4,1);struct.pack_into('<I',record,12,0)
        struct.pack_into('<I',record,12,zlib.crc32(record));journal.write_bytes(record)
        self.assertEqual(self.command('recover')['error'],0);self.assertEqual((CARD/'markdown/unittest-sync.md').read_bytes(),b'# new\n')
        self.restore();self.baseline();plan=self.plan(b'# new\n');self.command('commit',TARGET,plan,5)
        record=bytearray(journal.read_bytes());body_hash=json.loads(plan.read_text())['body']['sha256'].encode()
        offset=record.index(body_hash);record[offset]=ord('0') if record[offset]!=ord('0') else ord('1')
        struct.pack_into('<I',record,12,0);struct.pack_into('<I',record,12,zlib.crc32(record));part.write_bytes(record)
        self.assertNotEqual(self.command('recover')['error'],0)
        self.assertTrue(journal.exists() and part.exists());self.assertTrue((CARD/'.pico-sync/current/previous').exists())
    def test_candidate_promotion_failure_never_reports_download_success(self):
        self.baseline();plan=self.plan(b'# new\n')
        result=self.command('commit',TARGET,plan,faults={'PICO_TEST_RENAME_FAIL_TO':'/sdcard/.pico-sync/transaction'})
        self.assertNotEqual(result['error'],0);self.assertEqual((CARD/'markdown/unittest-sync.md').read_bytes(),b'# old\n')
        self.assertTrue((CARD/'.pico-sync/transaction.part').exists())
        self.assertEqual(self.command('recover')['error'],0);self.assertEqual((CARD/'markdown/unittest-sync.md').read_bytes(),b'# old\n')
    def test_phase_two_checkpoint_failure_rolls_back_but_never_reports_success(self):
        self.baseline();plan=self.plan(b'# new\n')
        result=self.command('commit',TARGET,plan,faults={'PICO_TEST_JOURNAL_WRITE_FAIL_AT':'2'})
        self.assertNotEqual(result['error'],0);self.assertEqual((CARD/'markdown/unittest-sync.md').read_bytes(),b'# old\n')
        self.assertTrue(self.command('progress-load',TARGET)['present'])
        self.restore();self.command('prepare');plan=self.plan(b'# new\n')
        result=self.command('commit',TARGET,plan,faults={'PICO_TEST_JOURNAL_WRITE_FAIL_AT':'2'})
        self.assertNotEqual(result['error'],0);self.assertFalse((CARD/'markdown/unittest-sync.md').exists())
if __name__=='__main__':unittest.main()
