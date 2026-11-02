# SPDX-License-Identifier: Apache-2.0
"""真实配置存储故障回归，不访问卡或凭据。 / Real storage faults without cards or credentials."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='pico-aux-settings-') as temp:
    out=Path(temp)
    source=(ROOT/'main/cloud/cloud_sync.c').read_text()
    section=source[source.index('bool cloud_config_valid('):source.index('static void notice(')]
    (out/'cloud_config_under_test.inc').write_text(section)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-g',
        '-I'+str(out),'-Itools/settings_backup_stubs','-Imain','-Imain/book',
        'tools/settings_aux_storage_host_test.c','main/ui/ui_i18n.c','-o',str(out/'check')],cwd=ROOT,check=True)
    subprocess.run([str(out/'check')],cwd=ROOT,check=True)
