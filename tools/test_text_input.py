# SPDX-License-Identifier: Apache-2.0
"""Production input engine under sanitizers; dictionary tests use only a temporary file."""
from pathlib import Path
import subprocess, tempfile, struct
ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(dir=ROOT/'build', prefix='input-unit-') as tmp:
    work=Path(tmp); dictionary=work/'phrases.ifdict'; binary=work/'test'
    command=['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',
        f'-DUI_PINYIN_DICTIONARY_PATH="{dictionary}"', '-I'+str(ROOT/'tools/simulator/include'),
        '-I'+str(ROOT/'main'),'-I'+str(ROOT/'main/app'),'-I'+str(ROOT/'main/ui'),'-I'+str(ROOT/'main/font'),
        '-I'+str(ROOT/'components/read_pico_search/include'),str(ROOT/'main/ui/ui_i18n.c'),str(ROOT/'tools/text_input_host_test.c'),
        str(ROOT/'components/read_pico_search/read_pico_search.c'),'-o',str(binary)]
    subprocess.run(command,check=True)
    subprocess.run([str(binary)],check=True)
    # 缺失及损坏词库不能破坏单字后备。/ Missing or corrupt packs cannot disable built-in character input.
    dictionary.write_bytes(b'IFPY001\0'+struct.pack('<II',0xffffffff,0))
    subprocess.run([str(binary)],check=True)
    rows=[('ceshi',word) for word in ('测试','测式','测事','测世','测试','测市','策士','策试','策事','策市','册式','册市')]+[('yuedu','阅读'),('yuedu','月度')]
    dictionary.write_bytes(b'IFPY001\0'+struct.pack('<II',len(rows),0)+b''.join(key.encode().ljust(33,b'\0')+word.encode().ljust(31,b'\0') for key,word in rows))
    subprocess.run([str(binary),'phrases'],check=True)
    dictionary.write_bytes(dictionary.read_bytes()[:-1])
    subprocess.run([str(binary)],check=True)
    print('No live TF files or credentials were accessed.')

# 直接抽取生产锁屏/退出回调，与真实键盘组合；不接触设备存储。
# Exercise the production lock/exit callbacks with the real keyboard, without device storage.
source=(ROOT/'main/apps/app_device_settings.c').read_text()
def callback(name):
    start=source.index('static void '+name+'(');opening=source.index('{',start)
    depth=1;end=opening+1
    while depth:
        if source[end]=='{':depth+=1
        elif source[end]=='}':depth-=1
        end+=1
    return source[start:end]
with tempfile.TemporaryDirectory(dir=ROOT/'build',prefix='input-lock-') as tmp:
    work=Path(tmp);test=work/'lock.c';binary=work/'lock'
    test.write_text('#define main keyboard_tests\n#include "'+str(ROOT/'tools/text_input_host_test.c')+'"\n#undef main\n'+
        'static ui_text_input_t s_text_input; static char s_editor[257]; static bool s_quote_dragging;\n'
        'enum { SETTINGS_TEXT_EDIT=19 }; static int s_page=SETTINGS_TEXT_EDIT;\n'
        'static void quote_preview_clear(void) {}\nstatic void online_close(void) {}\n'+callback('settings_exit')+'\n'+callback('on_before_lock')+r"""
int main(void) {
    for(int secret=0;secret<2;++secret) {
        strcpy(s_editor,secret ? "dummy-secret" : "普通草稿");
        ui_text_input_open(&s_text_input,s_editor,sizeof(s_editor),!secret,secret,UI_INPUT_CHINESE);
        if(!secret)spell(&s_text_input,"wo");else s_text_input.revealed=true;
        for(int i=0;i<3;++i) {
            on_before_lock(NULL);ui_text_input_draw(NULL,&s_text_input,"测试","",NULL,NULL,NULL);
            if(secret)assert(!s_editor[0]&&!s_text_input.cursor&&!s_text_input.revealed);
            else assert(!strcmp(s_editor,"普通草稿")&&!strcmp(s_text_input.pinyin,"wo"));
        }
        if(secret){assert(ui_text_input_insert(&s_text_input,"again"));assert(!strcmp(s_editor,"again"));}
        else {tap(&s_text_input,CANDIDATE);assert(strlen(s_editor)>strlen("普通草稿"));}
        settings_exit(NULL);assert(!s_editor[0]&&!s_text_input.cursor&&!s_text_input.candidate_count);
    }
    puts("PASS: lock keeps ordinary text/composition; secrets clear safely and remain editable after wake.");
    return 0;
}
""")
    command=['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',
        '-I'+str(ROOT/'tools/simulator/include'),'-I'+str(ROOT/'main'),'-I'+str(ROOT/'main/app'),
        '-I'+str(ROOT/'main/ui'),'-I'+str(ROOT/'main/font'),'-I'+str(ROOT/'components/read_pico_search/include'),
        str(ROOT/'main/ui/ui_i18n.c'),str(test),str(ROOT/'components/read_pico_search/read_pico_search.c'),'-o',str(binary)]
    subprocess.run(command,check=True);subprocess.run([str(binary)],check=True)
