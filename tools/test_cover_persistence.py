#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real cover store + SHA-256 + isolated FatFs-like I/O; decoder faults are deterministic substitutes."""
from pathlib import Path
import subprocess,os,json,tempfile,struct,hashlib
R=Path(__file__).resolve().parents[1]
UNIT=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book_cover.h"
#include "book_index_cache.h"
#include "book_epub.h"
static unsigned epoch,extracts,decodes,generated;
uint32_t book_index_cache_file_epoch(uint64_t size,int64_t modified){(void)size;(void)modified;return epoch;}
bool book_title_from_path(const char *p,char *out,size_t cap){const char *name=strrchr(p,'/');name=name?name+1:p;snprintf(out,cap,"%s",name);char *dot=strrchr(out,'.');if(dot)*dot=0;return *out;}
bool ui_font_has_text(const char *p){return !strstr(p,"external");}
esp_err_t book_epub_cover(const char *p,uint8_t **out,size_t *n,bool *png){
 ++extracts;if(getenv("COVER_CHANGE_EPOCH"))++epoch;*out=NULL;*n=0;*png=false;
 FILE *f=fopen(p,"rb");if(!f)return ESP_FAIL;uint8_t bytes[32];size_t size=fread(bytes,1,sizeof(bytes),f);fclose(f);
 if(size>=4&&!memcmp(bytes,"NONE",4))return ESP_ERR_NOT_FOUND;
 if(getenv("COVER_EXTRACT_OOM"))return ESP_ERR_NO_MEM;
 if(getenv("COVER_EXTRACT_ERROR"))return ESP_FAIL;
 *out=malloc(size);assert(*out);memcpy(*out,bytes,size);*n=size;*png=true;return ESP_OK;
}
bool book_image_dimensions(const uint8_t *d,size_t n,bool png,unsigned *w,unsigned *h){(void)png;if(!n)return false;*w=120+d[0]%10;*h=240;return true;}
bool book_image_grayscale(const uint8_t *d,size_t n,bool png,unsigned w,unsigned h,uint8_t *out){(void)n;(void)png;++decodes;if(getenv("COVER_DECODE_FAIL"))return false;memset(out,d[0],(size_t)w*h);return true;}
bool book_cover_thumbnail(const uint8_t *d,size_t n,bool png,uint8_t *out){return book_image_grayscale(d,n,png,BOOK_COVER_W,BOOK_COVER_H,out);}
bool book_auto_cover_render(const char *path,const char *title,const char *author,unsigned w,unsigned h,uint8_t *out){(void)path;(void)title;(void)author;++generated;memset(out,222,(size_t)w*h);return true;}
int main(int argc,char **argv){
 assert(argc==6);epoch=strtoul(argv[1],NULL,10);bool fitted=atoi(argv[3]);
 uint8_t out[BOOK_COVER_W*BOOK_COVER_H];memset(out,0xaa,sizeof(out));unsigned w=0,h=0;bool pending=false;
 bool probe=fitted?book_cover_load_fitted_gray(argv[2],"ignored",argv[4],out,false,&pending,&w,&h):book_cover_load_gray(argv[2],"ignored",argv[4],out,false,&pending);
 assert(!extracts&&!decodes&&!generated); /* No source extraction, hashing, or decoding during list/cache-only probes. */
 bool ok=fitted?book_cover_load_fitted_gray(argv[2],"ignored",argv[4],out,true,&pending,&w,&h):book_cover_load_gray(argv[2],"ignored",argv[4],out,true,&pending);
 if(!fitted){w=BOOK_COVER_W;h=BOOK_COVER_H;}
 unsigned first=ok?out[0]:0;unsigned e=extracts,d=decodes,g=generated;
 if(ok&&!getenv("COVER_EXTRACT_OOM")){
  for(size_t i=0;i<(size_t)w*h;++i)assert(out[i]==first);
  bool again=fitted?book_cover_load_fitted_gray(argv[2],"ignored",argv[4],out,false,&pending,&w,&h):book_cover_load_gray(argv[2],"ignored",argv[4],out,false,&pending);
  assert(again&&extracts==e&&decodes==d&&generated==g);
 }
 printf("{\"ok\":%d,\"probe\":%d,\"extract\":%u,\"decode\":%u,\"auto\":%u,\"first\":%u,\"width\":%u,\"height\":%u}\n",ok,probe,e,d,g,first,w,h);
}
'''
with tempfile.TemporaryDirectory(prefix='cover-persist-',dir=R/'build') as tmp:
 p=Path(tmp);(p/'test.c').write_text(UNIT);binary=p/'check'
 flags=['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-DPICO_SIM_CARD_PATH="unused"','-DPICO_HOST_SIMULATOR','-fsanitize=address,undefined','-g','-include',str(R/'tools/simulator/fs.h')]
 for inc in ['tools/simulator/include','main','main/book','main/font']:flags+=['-I'+str(R/inc)]
 subprocess.run(flags+[str(p/'test.c'),str(R/'main/book/book_cover_store.c'),str(R/'tools/simulator/fs.c'),'-o',str(binary)],check=True)
 card=p/'card';(card/'books').mkdir(parents=True);path='/sdcard/books/A.epub';source=card/'books/A.epub';source.write_bytes(b'A-image-source!!')
 env={**os.environ,'PICO_SIM_CARD_ROOT':str(card),'PICO_TEST_FATFS_RENAME':'1','ASAN_OPTIONS':'detect_leaks=0'}
 epoch=0;cases=0
 def run(path=path,fitted=True,author='Author',**fault):
  global epoch,cases
  epoch+=1;cases+=1
  v=subprocess.run([str(binary),str(epoch),path,str(int(fitted)),author,'unused'],env={**env,**fault},capture_output=True,text=True)
  assert v.returncode==0,v.stderr+v.stdout
  return json.loads(v.stdout)
 cache=card/'.readpico/covers'
 target=None
 def persistent():return target if target is not None else next(cache.glob('*-fit3.bin'))
 def assert_decode(v,first=65):assert v['ok'] and not v['probe'] and v['extract']==1 and v['decode']==1 and v['first']==first,v
 def assert_reuse(v,first=65):assert v['ok'] and not v['probe'] and v['extract']==1 and v['decode']==0 and v['auto']==0 and v['first']==first,v
 assert_decode(run());target=persistent();blob=persistent().read_bytes();assert_reuse(run());assert persistent().read_bytes()==blob
 # Cross-process restart, same size and mtime replacement: content must win.
 st=source.stat();source.write_bytes(b'B-image-source!!');os.utime(source,ns=(st.st_atime_ns,st.st_mtime_ns));assert_decode(run(),66);assert_reuse(run(),66)
 source.write_bytes(b'A-image-source!!');os.utime(source,ns=(st.st_atime_ns,st.st_mtime_ns));assert_decode(run());assert_reuse(run())
 # Corruption in dimensions, source/path/digest/payload, truncation and trailing garbage.
 for mutate in [lambda b:b[:7],lambda b:b[:-1],lambda b:b+b'x',lambda b:b[:12]+struct.pack('<I',177)+b[16:],lambda b:b[:12]+struct.pack('<II',120,240)+b[20:],lambda b:b[:52]+bytes([b[52]^1])+b[53:],lambda b:b[:24]+bytes([b[24]^1])+b[25:],lambda b:b[:-1]+bytes([b[-1]^1])]:
  persistent().write_bytes(mutate(blob));assert_decode(run());assert_reuse(run())
 # Copy a valid cache to another path: full path and SHA identity prevent collisions/misbinding.
 other=card/'books/Other.epub';other.write_bytes(source.read_bytes());assert_decode(run('/sdcard/books/Other.epub'))
 other_cache=[x for x in cache.glob('*-fit3.bin') if x.name!=persistent().name][0]
 other_cache.write_bytes(blob);assert_decode(run('/sdcard/books/Other.epub'))
 # Fitted and cropped layouts never share a persistent entry.
 assert_decode(run(fitted=False));assert_reuse(run(fitted=False));assert len(list(cache.glob('*-crop3.bin')))==1
 # Author changes invalidate template/source identity rather than accepting a hash/path match.
 assert_decode(run(author='New author'));assert_reuse(run(author='New author'));assert_decode(run())
 # Missing source never displays old cache or invokes decoder.
 source.unlink();v=run();assert not v['ok'] and not v['extract'];source.write_bytes(b'A-image-source!!');assert_reuse(run())
 # Read errors fall back; source extraction/OOM never creates a persistent generated-cover result.
 saved=persistent().read_bytes();v=run(COVER_EXTRACT_OOM='1');assert v['ok'] and v['auto']==1 and persistent().read_bytes()==saved
 v=run(COVER_EXTRACT_ERROR='1');assert v['ok'] and v['auto']==1 and persistent().read_bytes()==saved
 # Bad image cannot permanently cache a generated fallback under the real image's identity.
 persistent().unlink();v=run(COVER_DECODE_FAIL='1');assert v['ok'] and v['decode']==1 and v['auto']==1 and not persistent().exists()
 assert_decode(run());assert_reuse(run())
 # Mid-read managed replacement invalidates this result, not just the next boot.
 saved=persistent().read_bytes();v=run(COVER_CHANGE_EPOCH='1');assert not v['ok'] and v['extract']==1 and not v['decode'] and persistent().read_bytes()==saved
 # A failed durable write must not prevent display; retries on a new process recover.
 persistent().unlink();assert_decode(run(PICO_TEST_FSYNC_FAIL='1'));assert not persistent().exists();assert_decode(run());assert_reuse(run())
 before=persistent().read_bytes();tmp_path='/sdcard/.readpico/covers/'+persistent().name+'.tmp'
 source.write_bytes(b'C-image-source!!');assert_decode(run(PICO_TEST_FOPEN_WRITE_FAIL=tmp_path),67);assert persistent().read_bytes()==before
 assert_decode(run(),67);assert_reuse(run(),67)
 source.write_bytes(b'D-image-source!!');dest='/sdcard/.readpico/covers/'+persistent().name
 assert_decode(run(PICO_TEST_RENAME_FAIL_TO=dest),68);assert not persistent().exists();assert_decode(run(),68);assert_reuse(run(),68)
 assert not list(cache.glob('*.tmp'))
 # Built-in-only generated covers persist. Unknown external glyphs conservatively retry per session.
 text=card/'books/Plain.txt';text.write_text('text body')
 v=run('/sdcard/books/Plain.txt');assert v['ok'] and v['auto']==1 and v['extract']==0
 v=run('/sdcard/books/Plain.txt');assert v['ok'] and v['auto']==0 and v['extract']==0 and v['first']==222
 text.write_text('a completely different body');v=run('/sdcard/books/Plain.txt');assert v['ok'] and v['auto']==0 # Filename/author template is unchanged.
 for _ in range(2):
  v=run('/sdcard/books/Plain.txt',author='external glyph');assert v['ok'] and v['auto']==1
 missing=card/'books/NoCover.epub';missing.write_bytes(b'NONE-image-data!')
 v=run('/sdcard/books/NoCover.epub');assert v['ok'] and v['auto']==1 and v['extract']==1
 v=run('/sdcard/books/NoCover.epub');assert v['ok'] and v['auto']==0 and v['extract']==1
 missing.write_bytes(b'A-image-source!!');assert_decode(run('/sdcard/books/NoCover.epub'));assert_reuse(run('/sdcard/books/NoCover.epub'))
 # Cached raw data binds template/format, and old epoch cache cannot act as a cross-session proof.
 assert all(not x.name.endswith('.tmp') for x in cache.iterdir())
 assert 'No full-book digest and no hashing on scan/allow_decode=false paths' in (R/'main/book/book_cover_store.c').read_text()
 print(f'PASS {cases} fresh-process cover cases: same-size/mtime replacement, current raw image SHA, crop/fit isolation, full path/author, corruption and truncation, missing source, decoder/OOM fallback, fsync/open/rename failures, epoch race, generated art and conservative external-font handling; no heavy cache-only probes.')
