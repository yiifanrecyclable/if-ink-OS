/* SPDX-License-Identifier: Apache-2.0
 * 中文：文档版本、日期、附件归属、大纲和长图的宿主集成验证。
 * English: Host integration checks for document revisions, dates, assets, outlines and long images.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "book_markdown.h"
#include "document_store.h"
#include "document_image.h"
#include "book_progress.h"
#include "book_title.h"
#include "record_store.h"
static void write_text(const char *p,const char *s){FILE *f=fopen(p,"wb");assert(f);assert(fwrite(s,1,strlen(s),f)==strlen(s));assert(!fclose(f));}
static void copy(const char *a,const char *b){FILE *f=fopen(a,"rb"),*g=fopen(b,"wb");assert(f&&g);char bytes[4096];size_t n;while((n=fread(bytes,1,sizeof(bytes),f)))assert(fwrite(bytes,1,n,g)==n);assert(!ferror(f));fclose(f);assert(!fclose(g));}
static long size(const char *p){struct stat st;assert(!stat(p,&st));return st.st_size;}
static void date(char out[80]){FILE *f=fopen(DOCUMENT_ROOT"/.index/unittest-owned.md.pico","rb");assert(f);assert(fgets(out,80,f));assert(fgets(out,80,f));fclose(f);}
static document_entry_t entry(const char *path){document_entry_t *items=NULL,result={0};size_t n=0;assert(document_store_scan(&items,&n)==ESP_OK);for(size_t i=0;i<n;i++)if(!strcmp(items[i].path,path))result=items[i];free(items);assert(result.path[0]);return result;}
static void mark_key(const char *path,char key[11]){
    uint32_t hash=UINT32_C(2166136261);
    for(const unsigned char *p=(const unsigned char *)path;*p;++p)hash=(hash^*p)*UINT32_C(16777619);
    snprintf(key,11,"m_%08lx",(unsigned long)hash);
}
static void check_local_move(const char *source,const char *stage){
    assert(!mkdir(DOCUMENT_ROOT "/archive",0700));
    const char *target=DOCUMENT_ROOT "/archive/web.md";
    document_entry_t first=entry(source);
    book_progress_t progress={.file_size=first.size,.byte_off=8,.px=48,.pct=47};
    assert(book_progress_save(source,&progress)==ESP_OK&&book_progress_set_last_path(source)==ESP_OK);
    assert(book_progress_load(source,first.size,&progress));uint32_t order=progress.last_open_s;
    assert(book_title_set(source,"Local display name")==ESP_OK);
    char from[11],to[11];mark_key(source,from);mark_key(target,to);
    uint8_t marks[12+288+24*12]={0};
    uint32_t magic=UINT32_C(0x52504d32);memcpy(marks,&magic,sizeof(magic));memcpy(marks+4,&first.size,sizeof(first.size));
    marks[8]=1;memcpy(marks+12,source,strlen(source)+1);marks[304]=8;
    records_handle_t h;assert(records_open("rp_marks",NVS_READWRITE,&h)==ESP_OK);
    assert(records_set_blob(h,from,marks,sizeof(marks))==ESP_OK&&records_commit(h)==ESP_OK);records_close(h);
    bool moved=false;assert(document_store_move(source,target,&moved)==ESP_OK&&moved);
    assert(book_progress_load(target,first.size,&progress)&&progress.pct==47&&progress.last_open_s==order);
    char name[288];assert(book_title_get(target,name,sizeof(name))&&!strcmp(name,"Local display name"));
    assert(book_progress_last_path(name,sizeof(name))&&!strcmp(name,target));
    assert(records_open("rp_marks",NVS_READONLY,&h)==ESP_OK);
    size_t len=sizeof(marks);assert(records_get_blob(h,to,marks,&len)==ESP_OK&&len==sizeof(marks));
    assert(!strcmp((char *)marks+12,target)&&marks[304]==8);
    assert(records_get_blob(h,from,marks,&len)==ESP_ERR_NVS_NOT_FOUND);records_close(h);
    document_entry_t next=entry(target);assert(next.pinned&&!strcmp(first.date,next.date));
    write_text(stage,"Reimported into the flat sync root.");
    assert(document_store_import_local(stage,source)==ESP_OK);
    assert(!entry(source).pinned&&entry(target).pinned);
    assert(book_progress_load(target,first.size,&progress)&&progress.last_open_s==order);
    assert(document_store_move(target,source,&moved)==ESP_ERR_INVALID_STATE&&!moved);
    assert(document_store_delete(source)==ESP_OK);
    assert(document_store_move(target,source,&moved)==ESP_OK&&moved);
    assert(entry(source).pinned&&book_progress_load(source,first.size,&progress)&&progress.last_open_s==order);
}
int main(void){
    record_store_media(true);
    (void)mkdir(DOCUMENT_ROOT,0700);(void)mkdir(DOCUMENT_ROOT"/attachments",0700);
    const char *path=DOCUMENT_ROOT"/unittest-owned.md";
    write_text(path,"## A\n\nbody\n\n### duplicate\n\nsub\n\n#### nested\n\nleaf\n\n## B\n\n### duplicate\n\nend\n");
    assert(book_markdown_open(path)==ESP_OK);assert(book_markdown_navigation_count()==2);
    assert(book_markdown_navigation_toggle(0));assert(book_markdown_navigation_count()==3);
    assert(book_markdown_navigation_depth(1)==1);size_t first=book_markdown_navigation_offset(1);
    assert(book_markdown_navigation_toggle(1));assert(book_markdown_navigation_count()==4);
    assert(book_markdown_navigation_toggle(3));assert(book_markdown_navigation_count()==5);
    assert(book_markdown_navigation_offset(4)>first);assert(book_markdown_navigation_toggle(0));assert(book_markdown_navigation_count()==3);
    char resolved[512];assert(markdown_attachment_path(path,"attachments/%E9%95%BF%E5%9B%BE%E7%A4%BA%E4%BE%8B-test.png",resolved,sizeof(resolved)));
    assert(!markdown_attachment_path(path,"attachments/../escape.png",resolved,sizeof(resolved)));assert(!markdown_attachment_path(path,"https://x/image.png",resolved,sizeof(resolved)));assert(!markdown_attachment_path(path,"attachments/%00bad.png",resolved,sizeof(resolved)));book_markdown_close();
    bool changed;assert(document_store_refresh(path,&changed)==ESP_OK&&!changed);char before[80],after[80];date(before);
    book_progress_t progress={.file_size=(uint32_t)size(path),.px=48,.pct=33,.byte_off=7};assert(book_progress_save(path,&progress)==ESP_OK);
    assert(document_store_set_flags(path,true,true)==ESP_OK);document_entry_t flags=entry(path);assert(flags.pinned&&flags.starred);
    assert(book_progress_load(path,progress.file_size,&progress));date(after);assert(!strcmp(before,after));
    assert(document_store_refresh(path,&changed)==ESP_OK&&!changed);assert(book_progress_load(path,progress.file_size,&progress));
    // Equal-sized replacement is a revision even though the old progress size still matches.
    write_text(path,"## C\n\nbody\n\n### duplicate\n\nsub\n\n#### nested\n\nleaf\n\n## B\n\n### duplicate\n\nend\n");assert(size(path)==progress.file_size);
    assert(entry(path).size==progress.file_size&&book_progress_load(path,progress.file_size,&progress));
    assert(document_store_refresh(path,&changed)==ESP_OK&&changed);assert(!book_progress_load(path,progress.file_size,&progress));date(after);assert(!strcmp(before,after));
    flags=entry(path);assert(flags.pinned&&flags.starred);assert(document_store_set_flags(path,false,true)==ESP_OK);flags=entry(path);assert(!flags.pinned&&flags.starred);
    assert(document_store_set_flags(DOCUMENT_ROOT"/missing.md",true,true)==ESP_ERR_NOT_FOUND);
    assert(document_store_set_flags("/sdcard/books/escape.md",true,true)==ESP_ERR_INVALID_ARG);
    const char *old=DOCUMENT_ROOT"/attachments/unittest-old.png",*next=DOCUMENT_ROOT"/attachments/unittest-next.png";
    copy("plugins/obsidian-pico/tests/fixtures/sample-image.png",old);copy(old,next);
    write_text(path,"# owned\n\n![a](attachments/unittest-old.png)\n");assert(document_store_refresh(path,&changed)==ESP_OK);
    document_image_t old_image;assert(document_image_open(old,&old_image));assert(!access(old_image.cache,F_OK));
    write_text(path,"# owned\n\n![a](attachments/unittest-next.png)\n");assert(document_store_refresh(path,&changed)==ESP_OK);assert(access(old,F_OK));assert(access(old_image.cache,F_OK));assert(!access(next,F_OK));date(after);assert(!strcmp(before,after));
    // A missing new asset leaves the old ownership available for retry.
    write_text(path,"# owned\n\n![a](attachments/unittest-missing.png)\n");assert(document_store_refresh(path,&changed)!=ESP_OK);assert(!access(next,F_OK));
    write_text(path,"# owned\n\n![a](attachments/unittest-next.png)\n");assert(document_store_refresh(path,&changed)==ESP_OK);
    write_text(DOCUMENT_ROOT"/unittest-shared.md","![shared](attachments/unittest-next.png)");assert(document_store_refresh(DOCUMENT_ROOT"/unittest-shared.md",&changed)==ESP_OK);
    assert(document_store_delete(path)==ESP_OK);assert(access(DOCUMENT_ROOT"/.index/unittest-owned.md.prefs",F_OK));assert(!access(next,F_OK));assert(document_store_delete(DOCUMENT_ROOT"/unittest-shared.md")==ESP_OK);assert(access(next,F_OK));
    const char *a_path=DOCUMENT_ROOT"/unittest-A.md",*z_path=DOCUMENT_ROOT"/unittest-Z.md";write_text(a_path,"# A");write_text(z_path,"# Z");assert(document_store_set_flags(z_path,true,false)==ESP_OK);
    document_entry_t *items=NULL;size_t n=0,ai=SIZE_MAX,zi=SIZE_MAX;assert(document_store_scan(&items,&n)==ESP_OK);for(size_t i=0;i<n;i++){if(!strcmp(items[i].path,a_path))ai=i;if(!strcmp(items[i].path,z_path))zi=i;}assert(zi<ai);free(items);
    assert(access(DOCUMENT_ROOT"/.index/unittest-A.md.pico",F_OK));
    assert(!unlink(z_path));assert(document_store_scan(&items,&n)==ESP_OK);
    for(size_t i=0;i<n;++i)assert(strcmp(items[i].path,z_path));
    free(items);
    // 列表不替代删除操作或正文校验。/ Listing does not run deletion or body validation.
    assert(!access(DOCUMENT_ROOT"/.index/unittest-Z.md.prefs",F_OK));
    assert(document_store_delete(z_path)==ESP_OK&&access(DOCUMENT_ROOT"/.index/unittest-Z.md.prefs",F_OK));
    write_text(a_path,"![missing](attachments/unittest-missing.png)");
    assert(!entry(a_path).date[0]&&access(DOCUMENT_ROOT"/.index/unittest-A.md.pico",F_OK));
    assert(document_store_refresh(a_path,&changed)!=ESP_OK);
    assert(document_store_delete(a_path)==ESP_OK);
    uint32_t probe_w=0,probe_h=0;unsigned probe_decodes=document_image_decode_count();
    assert(document_image_dimensions(DOCUMENT_ROOT"/attachments/长图示例-test.png",&probe_w,&probe_h));
    assert(probe_w==684&&probe_h==5000&&document_image_decode_count()==probe_decodes);
    document_image_t image;assert(document_image_open(DOCUMENT_ROOT"/attachments/长图示例-test.png",&image));assert(image.width==684&&image.height==5000);
    uint8_t *a=malloc(684*1216),*b=malloc(684*1216);assert(a&&b);assert(document_image_sample(&image,0,1216,684,1216,a));assert(document_image_sample(&image,1000,1216,684,1216,b));assert(memcmp(a,b,684*1216));free(a);free(b);
    for(unsigned filter=0;filter<5;filter++){char p[288];snprintf(p,sizeof(p),DOCUMENT_ROOT"/attachments/unittest-filter%u.png",filter);assert(document_image_open(p,&image));uint8_t out[17*25];assert(document_image_sample(&image,0,25,17,25,out));for(unsigned y=0;y<25;y++)for(unsigned x=0;x<17;x++)assert(out[y*17+x]==((x*3+y*5)%16)*17);}
    // 真实FatFS拒绝覆盖语义：再次打开应命中缓存；坏缓存可重建且不能影响正文。
    // Under FAT rename semantics, reopen must hit cache; corrupt cache is rebuilt without touching text.
    const char *cached_source=DOCUMENT_ROOT"/attachments/unittest-filter0.png";
    assert(document_image_open(cached_source,&image));unsigned decodes=document_image_decode_count();
    char cache_path[512];snprintf(cache_path,sizeof(cache_path),"%s",image.cache);
    assert(document_image_open(cached_source,&image));assert(document_image_decode_count()==decodes);
    FILE *broken=fopen(cache_path,"r+b");assert(broken);assert(!fseek(broken,-1,SEEK_END));int last=fgetc(broken);assert(last!=EOF);assert(!fseek(broken,-1,SEEK_END));fputc(last^1,broken);fclose(broken);
    assert(document_image_open(cached_source,&image));assert(document_image_decode_count()==decodes+1);
    uint8_t restored[17*25];assert(document_image_sample(&image,0,25,17,25,restored));
    for(unsigned y=0;y<25;y++)for(unsigned x=0;x<17;x++)assert(restored[y*17+x]==((x*3+y*5)%16)*17);
    // 同路径不同内容不能错误复用旧缓存，即使原图大小或日期相同。
    // Changing bytes at the same path must never reuse a prior content cache.
    copy("plugins/obsidian-pico/tests/fixtures/sample-image.png",cached_source);
    assert(document_image_open(cached_source,&image));assert(strcmp(image.cache,cache_path));
    copy("plugins/obsidian-pico/tests/fixtures/sample-image.png",old);FILE *f=fopen(old,"r+b");assert(f);assert(!fseek(f,29,SEEK_SET));int v=fgetc(f);assert(v!=EOF);assert(!fseek(f,29,SEEK_SET));fputc(v^1,f);fclose(f);assert(!document_image_dimensions(old,&probe_w,&probe_h));assert(!document_image_open(old,&image));unlink(old);
    // 网页 MD 入库保留日期/置顶，无变化保留进度，错误不覆盖旧正文。
    // Web imports preserve date/pin and no-op web_progress; rejected input leaves the old body intact.
    const char *dest=DOCUMENT_ROOT "/web.md",*stage=DOCUMENT_ROOT "/web-input.tmp";
    write_text(stage,"# Web\n\nOriginal.");
    assert(document_store_import_local(stage,dest)==ESP_OK);
    document_entry_t web_first=entry(dest);assert(document_store_set_flags(dest,true,false)==ESP_OK);
    book_progress_t web_progress={.file_size=web_first.size,.byte_off=8,.px=48,.pct=40};
    assert(book_progress_save(dest,&web_progress)==ESP_OK);
    assert(document_store_import_local(stage,dest)==ESP_OK);
    assert(book_progress_load(dest,web_first.size,&web_progress)&&web_progress.pct==40);
    write_text(stage,"![missing](attachments/missing.png)");
    assert(document_store_import_local(stage,dest)!=ESP_OK);
    assert(entry(dest).size==web_first.size && book_progress_load(dest,web_first.size,&web_progress));
    write_text(stage,"# Replacement\n\nMore text.");assert(document_store_import_local(stage,dest)==ESP_OK);
    document_entry_t second=entry(dest);assert(second.pinned&&!strcmp(second.date,web_first.date));
    assert(!book_progress_load(dest,second.size,&web_progress));
    copy("plugins/obsidian-pico/tests/fixtures/sample-image.png",old);
    write_text(stage,"![ok](attachments/unittest-old.png)");assert(document_store_import_local(stage,dest)==ESP_OK);
    assert(!access(old,F_OK));write_text(stage,"Plain text.");assert(document_store_import_local(stage,dest)==ESP_OK);
    assert(access(old,F_OK));
    // 索引替换中断留下备份时，扫描先恢复日期与置顶，而非当成新文档。
    // Scanning restores backup-only metadata/pins rather than treating the document as a new import.
    const char *prefs=DOCUMENT_ROOT "/.index/web.md.prefs",*meta=DOCUMENT_ROOT "/.index/web.md.pico";
    assert(!rename(prefs,DOCUMENT_ROOT "/.index/web.md.prefs.before"));
    assert(entry(dest).pinned && !access(prefs,F_OK));
    assert(!rename(meta,DOCUMENT_ROOT "/.index/web.md.pico.before"));
    assert(!strcmp(entry(dest).date,web_first.date) && !access(meta,F_OK));
    check_local_move(dest,stage);
    assert(document_store_delete(dest)==ESP_OK);assert(!remove(stage));
    puts("Document revisions, moves, metadata and shared assets, PNG filters/CRC and long viewport passed");return 0;
}
