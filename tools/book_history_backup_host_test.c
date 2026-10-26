/* SPDX-License-Identifier: Apache-2.0 */
/* Round-trip the real streaming history format through an in-memory NVS. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "nvs.h"
#include "../main/book/book_history_backup.c"

typedef struct { int ns; char key[16]; nvs_type_t type; size_t len; uint8_t data[1024]; } record_t;
static record_t records[2][32];
static int counts[2], active;
struct nvs_iterator_opaque { int ns, index; };

static int namespace_index(const char *name) {
    for (unsigned i = 0; i < sizeof(namespaces) / sizeof(namespaces[0]); ++i)
        if (!strcmp(name, namespaces[i])) return (int)i;
    return -1;
}
static record_t *find_record(int ns, const char *key) {
    for (int i = 0; i < counts[active]; ++i)
        if (records[active][i].ns == ns && !strcmp(records[active][i].key, key)) return &records[active][i];
    return NULL;
}
static esp_err_t put(int ns, const char *key, nvs_type_t type, const void *data, size_t len) {
    if (ns < 0 || len > 1024) return ESP_ERR_INVALID_ARG;
    record_t *r = find_record(ns, key);
    if (!r) { assert(counts[active] < 32); r = &records[active][counts[active]++]; }
    memset(r, 0, sizeof(*r)); r->ns = ns; strcpy(r->key, key); r->type = type; r->len = len;
    memcpy(r->data, data, len); return ESP_OK;
}
static esp_err_t get(int ns, const char *key, nvs_type_t type, void *data, size_t *len) {
    record_t *r = find_record(ns, key);
    if (!r || r->type != type) return ESP_ERR_NVS_NOT_FOUND;
    if (data && *len < r->len) return ESP_ERR_INVALID_SIZE;
    if (data) memcpy(data, r->data, r->len);
    *len = r->len; return ESP_OK;
}
esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *h) {
    int id = namespace_index(ns);
    if (id < 0) return ESP_ERR_NVS_NOT_FOUND;
    if (mode == NVS_READONLY) {
        bool found = false;
        for (int i = 0; i < counts[active]; ++i) if (records[active][i].ns == id) found = true;
        if (!found) return ESP_ERR_NVS_NOT_FOUND;
    }
    *h = id + 1; return ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
esp_err_t nvs_erase_all(nvs_handle_t h) {
    int ns = h - 1;
    for (int i = 0; i < counts[active];)
        if (records[active][i].ns == ns) {
            memmove(&records[active][i], &records[active][i + 1],
                    (size_t)(--counts[active] - i) * sizeof(records[active][i]));
        } else ++i;
    return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h,const char *key){
    record_t *r=find_record(h-1,key);if(!r)return ESP_ERR_NVS_NOT_FOUND;
    int i=(int)(r-records[active]);memmove(r,r+1,(size_t)(counts[active]-i-1)*sizeof(*r));--counts[active];return ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *v) { size_t n=1; return get(h-1,key,NVS_TYPE_U8,v,&n); }
esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t v) { return put(h-1,key,NVS_TYPE_U8,&v,1); }
esp_err_t nvs_get_u32(nvs_handle_t h, const char *key, uint32_t *v) { size_t n=4; return get(h-1,key,NVS_TYPE_U32,v,&n); }
esp_err_t nvs_set_u32(nvs_handle_t h, const char *key, uint32_t v) { return put(h-1,key,NVS_TYPE_U32,&v,4); }
esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *v, size_t *n) { return get(h-1,key,NVS_TYPE_STR,v,n); }
esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *v) { return put(h-1,key,NVS_TYPE_STR,v,strlen(v)+1); }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *v, size_t *n) { return get(h-1,key,NVS_TYPE_BLOB,v,n); }
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *v, size_t n) { return put(h-1,key,NVS_TYPE_BLOB,v,n); }
esp_err_t nvs_entry_find_in_handle(nvs_handle_t h, nvs_type_t type, nvs_iterator_t *it) {
    assert(type == NVS_TYPE_ANY);
    for (int i = 0; i < counts[active]; ++i) if (records[active][i].ns == h - 1) {
        *it = malloc(sizeof(**it)); assert(*it); **it = (struct nvs_iterator_opaque){h-1,i}; return ESP_OK;
    }
    *it = NULL; return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_entry_next(nvs_iterator_t *it) {
    for (int i = (*it)->index + 1; i < counts[active]; ++i)
        if (records[active][i].ns == (*it)->ns) { (*it)->index = i; return ESP_OK; }
    free(*it); *it = NULL; return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_entry_info(nvs_iterator_t it, nvs_entry_info_t *info) {
    record_t *r = &records[active][it->index];
    memset(info,0,sizeof(*info)); strcpy(info->key,r->key); info->type=r->type; return ESP_OK;
}
void nvs_release_iterator(nvs_iterator_t it) { free(it); }

int main(void) {
    assert(content_reading_target_minutes()==90);
    for(unsigned goal=30;goal<=180;goal+=30){assert(content_reading_target_valid(goal));assert(content_reading_target_set(goal)==ESP_OK);assert(content_reading_target_minutes()==goal);}
    assert(!content_reading_target_valid(0)&&!content_reading_target_valid(31)&&!content_reading_target_valid(181));
    assert(content_reading_target_set(91)==ESP_ERR_INVALID_ARG&&content_reading_target_minutes()==180);
    uint8_t bad_goal=91;assert(!valid_record(6,NVS_TYPE_U8,"read_target",&bad_goal,1));

    const char *path = "/sdcard/books/one.epub";
    uint32_t seq = 9;
    assert(put(0,"seq",NVS_TYPE_U32,&seq,4)==ESP_OK);
    assert(put(0,"last",NVS_TYPE_STR,path,strlen(path)+1)==ESP_OK);
    uint8_t progress[64] = {'R','P','B',2};
    size_t path_len = strlen(path)+1;
    progress[14]=48; progress[15]=42; progress[22]=(uint8_t)path_len;
    memcpy(progress+24,path,path_len);
    char key[11]; uint32_t path_hash = hash_bytes(UINT32_C(2166136261),path,strlen(path));
    snprintf(key,sizeof(key),"b_%08x",path_hash);
    assert(put(0,key,NVS_TYPE_BLOB,progress,24+path_len)==ESP_OK);
    const char *other = "/sdcard/books/two.txt";
    size_t other_len = strlen(other)+1;
    progress[10]=19; progress[15]=73; progress[22]=(uint8_t)other_len;
    memcpy(progress+24,other,other_len);
    snprintf(key,sizeof(key),"b_%08x",hash_bytes(UINT32_C(2166136261),other,strlen(other)));
    assert(put(0,key,NVS_TYPE_BLOB,progress,24+other_len)==ESP_OK);
    uint8_t stats[32]={0}; uint32_t stats_magic=UINT32_C(0x52505431); memcpy(stats,&stats_magic,4); stats[8]=77;
    assert(put(1,"stats",NVS_TYPE_BLOB,stats,sizeof(stats))==ESP_OK);
    uint8_t heat[244]={0}; uint32_t heat_magic=UINT32_C(0x52504831); memcpy(heat,&heat_magic,4); heat[4]=7;
    assert(put(1,"heatmap",NVS_TYPE_BLOB,heat,sizeof(heat))==ESP_OK);
    uint8_t marks[584]={0};
    snprintf(key,sizeof(key),"m_%08x",path_hash);
    assert(put(2,key,NVS_TYPE_BLOB,marks,sizeof(marks))==ESP_OK);
    uint8_t favorite=1; snprintf(key,sizeof(key),"f_%08x",path_hash);
    assert(put(3,key,NVS_TYPE_U8,&favorite,1)==ESP_OK);
    uint8_t title[64]={0}; memcpy(title,path,path_len); strcpy((char *)title+path_len,"我的书");
    snprintf(key,sizeof(key),"t_%08x",path_hash);
    assert(put(4,key,NVS_TYPE_BLOB,title,path_len+strlen("我的书")+1)==ESP_OK);
    assert(content_options_save(false,(content_options_t){.mode=CONTENT_PROGRESS,.ascending=true})==ESP_OK);
    assert(content_options_save(true,(content_options_t){.mode=CONTENT_ADDED,.marked_first=true,.marked_only=true})==ESP_OK);
    uint8_t legacy_book_options=29;assert(put(6,"books",NVS_TYPE_U8,&legacy_book_options,1)==ESP_OK);
    uint8_t legacy_doc_options=26;assert(put(6,"docs",NVS_TYPE_U8,&legacy_doc_options,1)==ESP_OK);
    uint32_t added=content_first_added(path);assert(added>0&&content_first_added(path)==added);
    assert(content_name_compare("Book2","Book10")<0&&content_name_compare("Alpha","alpha")==0);
    assert(!content_book_pinned(path));assert(content_book_pin_set(path,true)==ESP_OK&&content_book_pinned(path));
    assert(content_book_pin_set(path,false)==ESP_OK&&!content_book_pinned(path));
    assert(content_book_pin_set(path,true)==ESP_OK);
    char pin_record_key[16];snprintf(pin_record_key,sizeof(pin_record_key),"p_%08x",path_hash);
    assert(!valid_record(6,NVS_TYPE_STR,pin_record_key,(const uint8_t *)other,other_len));
    assert(!valid_record(6,NVS_TYPE_STR,pin_record_key,(const uint8_t *)path,path_len-1));
    assert(put(6,pin_record_key,NVS_TYPE_STR,other,other_len)==ESP_OK);
    assert(!content_book_pinned(path)&&content_book_pin_set(path,true)==ESP_ERR_INVALID_RESPONSE);
    assert(content_book_pin_set(path,false)==ESP_ERR_INVALID_RESPONSE);
    assert(put(6,pin_record_key,NVS_TYPE_STR,path,path_len)==ESP_OK);
    assert(counts[0]==14);
    assert(content_home_quote_valid("阅读每一天") && content_home_quote_valid(""));
    assert(content_home_quote_valid("one\ntwo\nthree\nfour")&&!content_home_quote_valid("one\ntwo\nthree\nfour\nfive"));
    assert(!content_home_quote_valid("bad\r") && !content_home_quote_valid("\xc0\xaf"));
    assert(!content_home_quote_font_valid("/sdcard/fonts/bad\n.ttf"));
    assert(content_home_quote_font_valid("/sdcard/fonts/Test.ttf") && content_home_quote_font_valid(""));
    assert(!content_home_quote_font_valid("/sdcard/../secret.ttf") && !content_home_quote_font_valid("/etc/font.ttf"));
    assert(content_home_quote_size_valid(48)&&content_home_quote_size_valid(96)&&content_home_quote_size_valid(120));
    assert(!content_home_quote_size_valid(47)&&!content_home_quote_size_valid(97)&&!content_home_quote_size_valid(121));
    assert(content_home_quote_alignment()==CONTENT_QUOTE_CENTER);
    for(unsigned align=0;align<4;++align)assert(content_home_quote_alignment_valid(align));
    assert(!content_home_quote_alignment_valid(4)&&!content_home_quote_alignment_valid(255));
    uint8_t quote_align=CONTENT_QUOTE_JUSTIFY;assert(put(6,"quote_align",NVS_TYPE_U8,&quote_align,1)==ESP_OK);
    uint8_t bad_align=4;assert(!valid_record(6,NVS_TYPE_U8,"quote_align",&bad_align,1));
    assert(!valid_record(6,NVS_TYPE_U32,"quote_align",&quote_align,1));
    uint8_t md_sizes[4];content_markdown_sizes(md_sizes);assert(md_sizes[0]==48&&md_sizes[1]==72&&md_sizes[2]==64&&md_sizes[3]==56);
    uint8_t wanted[4]={40,80,68,60};assert(content_markdown_sizes_set(wanted)==ESP_OK);
    content_markdown_sizes(md_sizes);assert(!memcmp(md_sizes,wanted,4));
    assert(!valid_record(6,NVS_TYPE_BLOB,"md_sizes",wanted,3));
    uint8_t invalid_sizes[4]={0,80,68,60};assert(!content_markdown_sizes_valid(invalid_sizes));
    assert(!valid_record(6,NVS_TYPE_BLOB,"md_sizes",invalid_sizes,4));
    assert(content_home_quote_valign()==CONTENT_QUOTE_VTOP);
    assert(content_home_quote_valign_set(CONTENT_QUOTE_VBOTTOM)==ESP_OK);
    assert(content_home_quote_valign()==CONTENT_QUOTE_VBOTTOM);
    uint8_t vertical=CONTENT_QUOTE_VBOTTOM,invalid_vertical=3;
    assert(valid_record(6,NVS_TYPE_U8,"quote_valign",&vertical,1));
    assert(!valid_record(6,NVS_TYPE_U8,"quote_valign",&invalid_vertical,1));
    assert(!valid_record(6,NVS_TYPE_U32,"quote_valign",&vertical,1));
    uint8_t quote_px=84;assert(put(6,"quote_px",NVS_TYPE_U8,&quote_px,1)==ESP_OK);
    const char *quote_font="/sdcard/fonts/Test.ttf";
    assert(content_markdown_font_valid("builtin")&&!content_markdown_font_valid("")&&!content_markdown_font_valid("/sdcard/../secret.ttf"));
    assert(content_markdown_font_set(quote_font)==ESP_OK);
    assert(!valid_record(6,NVS_TYPE_STR,"md_font",(const uint8_t *)quote_font,strlen(quote_font)));
    assert(!valid_record(6,NVS_TYPE_STR,"md_font",(const uint8_t *)"",1));
    assert(put(6,"quote_font",NVS_TYPE_STR,quote_font,strlen(quote_font)+1)==ESP_OK);
    const char *quote="固定格言";
    assert(put(6,"home_quote",NVS_TYPE_STR,quote,strlen(quote)+1)==ESP_OK);
    assert(!valid_record(6,NVS_TYPE_STR,"home_quote",(const uint8_t *)quote,strlen(quote)));
    uint8_t invalid_option=32;
    assert(!valid_record(6,NVS_TYPE_U8,"docs",&invalid_option,1));
    char added_record_key[16];snprintf(added_record_key,sizeof(added_record_key),"a_%08x",path_hash);
    record_t *added_record=find_record(6,added_record_key);assert(added_record);
    assert(!valid_record(6,NVS_TYPE_BLOB,added_record_key,added_record->data,added_record->len-1));

    content_banner_config_t banner;content_banner_load(&banner);assert(banner.version==3&&banner.show_text==1&&banner.mode==0&&banner.quote_px==84);
    banner.mode=1;banner.date_y=210;assert(content_banner_save(&banner)==ESP_OK);
    assert(valid_record(6,NVS_TYPE_BLOB,"banner_v1",(const uint8_t *)&banner,sizeof(banner)));
    content_banner_config_t invalid_banner=banner;invalid_banner.mode=2;
    assert(!content_banner_valid(&invalid_banner)&&!valid_record(6,NVS_TYPE_BLOB,"banner_v1",(const uint8_t *)&invalid_banner,sizeof(invalid_banner)));
    invalid_banner=banner;snprintf(invalid_banner.image,sizeof(invalid_banner.image),"/sdcard/../private.png");assert(!content_banner_valid(&invalid_banner));
    invalid_banner=banner;invalid_banner.date_y=261;assert(!content_banner_valid(&invalid_banner));
    invalid_banner=banner;invalid_banner.show_text=2;assert(!content_banner_valid(&invalid_banner));
    content_banner_config_t legacy_banner=banner;legacy_banner.version=1;legacy_banner.show_text=0;assert(valid_record(6,NVS_TYPE_BLOB,"banner_v1",(const uint8_t *)&legacy_banner,652));
    invalid_banner=banner;invalid_banner.text_y=297;assert(!content_banner_valid(&invalid_banner));
    content_banner_config_t decoded;assert(content_banner_decode(&legacy_banner,652,&decoded)&&decoded.version==3&&decoded.text_y==CONTENT_BANNER_TEXT_AUTO);
    FILE *f=tmpfile(); assert(f);
    assert(book_history_backup_write(f)==ESP_OK);
    rewind(f); assert(book_history_backup_validate(f));
    active=1; rewind(f);
    const char *stale = "/sdcard/books/stale.epub";
    assert(put(0,"last",NVS_TYPE_STR,stale,strlen(stale)+1)==ESP_OK);
    assert(put(3,"f_deadbeef",NVS_TYPE_U8,&favorite,1)==ESP_OK);
    // 旧备份不含移出状态，恢复时清除机器上后加的隐藏记录。/ Legacy restore clears later removals.
    snprintf(key,sizeof(key),"h_%08x",path_hash);
    assert(put(5,key,NVS_TYPE_STR,path,path_len)==ESP_OK);
    assert(book_history_backup_restore(f)==ESP_OK);
    assert(content_reading_target_minutes()==180);
    content_markdown_sizes(md_sizes);assert(!memcmp(md_sizes,wanted,4));
    assert(counts[1]==counts[0]);
    assert(!find_record(3,"f_deadbeef"));
    assert(!find_record(5,key));
    content_options_t restored_books=content_options_load(false,false),restored_docs=content_options_load(true,false);
    assert(restored_books.mode==CONTENT_PROGRESS&&restored_books.ascending&&!restored_books.marked_first&&!restored_books.marked_only);
    assert(restored_docs.mode==CONTENT_ADDED&&!restored_docs.ascending&&!restored_docs.marked_first&&!restored_docs.marked_only);
    assert(content_first_added(path)==added&&content_book_pinned(path));
    for (int i=0;i<counts[0];++i) {
        record_t *a=&records[0][i], *b=find_record(a->ns,a->key);
        assert(b && b->type==a->type && b->len==a->len && !memcmp(a->data,b->data,a->len));
    }
    fclose(f);
    // 新备份完整恢复隐藏记录，并继续保留进度和收藏。/ New backups restore removal and reading data.
    active=0;
    assert(put(5,key,NVS_TYPE_STR,path,path_len)==ESP_OK);
    f=tmpfile(); assert(f);
    assert(book_history_backup_write(f)==ESP_OK);
    rewind(f); assert(book_history_backup_validate(f));
    active=1; rewind(f);
    assert(book_history_backup_restore(f)==ESP_OK && counts[1]==counts[0]);
    content_banner_config_t restored_banner;content_banner_load(&restored_banner);assert(!memcmp(&restored_banner,&banner,sizeof(banner)));
    assert(content_home_quote_valign()==CONTENT_QUOTE_VBOTTOM);
    assert(!strcmp(content_markdown_font(),quote_font));
    for (int i=0;i<counts[0];++i) {
        record_t *a=&records[0][i], *b=find_record(a->ns,a->key);
        assert(b && b->type==a->type && b->len==a->len && !memcmp(a->data,b->data,a->len));
    }
    assert(!valid_record(5,NVS_TYPE_STR,key,(const uint8_t*)other,other_len));
    assert(!valid_record(5,NVS_TYPE_STR,key,(const uint8_t*)path,path_len-1));
    assert(!valid_record(5,NVS_TYPE_BLOB,key,(const uint8_t*)path,path_len));
    assert(fseek(f,-1,SEEK_END)==0); assert(fputc(0xff,f)!=EOF); fflush(f);
    rewind(f); assert(!book_history_backup_validate(f));
    fclose(f);
    puts("book history backup: progress, time, bookmarks, favorites, titles, shelf removals and checksum passed");
    return 0;
}
