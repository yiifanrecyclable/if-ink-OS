/* SPDX-License-Identifier: Apache-2.0
 * 中文：独立进程测试实际TF进度库，注入短写/同步失败，备份保持原格式。
 * English: Exercise real SD progress across processes, injecting short writes/sync failures and legacy backups.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "book_progress.h"
#include "book_history_backup.h"

static unsigned write_call,sync_call;
static bool fault_at(const char *kind,unsigned n) {
    const char *fault=getenv("PROGRESS_FAULT"),*at=getenv("PROGRESS_FAULT_AT");
    return fault && !strcmp(fault,kind) && n==(at?(unsigned)strtoul(at,NULL,10):1);
}
static size_t progress_write(const void *p,size_t size,size_t n,FILE *f) {
    if(fault_at("write",++write_call)) return fwrite(p,1,3,f)/size;
    return fwrite(p,size,n,f);
}
static int progress_sync(int fd) {
    if(fault_at("sync",++sync_call)) return -1;
    return fsync(fd);
}
#define fwrite progress_write
#define fsync progress_sync
#include "../main/book/book_progress_kv.c"
#undef fwrite
#undef fsync

static bool show(const char *path,const book_progress_t *p,void *ctx) {
    (void)ctx; printf("%s %u %u %u %u\n",path,p->pct,p->last_open_s,p->chapter,p->byte_off);return true;
}
int main(int argc,char **argv) {
    assert(argc>=2); esp_err_t err=ESP_OK;
    if(!strcmp(argv[1],"save")) {
        assert(argc==6);
        book_progress_t p={.file_size=(uint32_t)strtoul(argv[5],NULL,10),.px=48,
            .pct=(uint8_t)atoi(argv[3]),.byte_off=(uint32_t)atoi(argv[4]),.chapter=2};
        err=book_progress_save(argv[2],&p);
        if(err==ESP_OK) err=book_progress_set_last_path(argv[2]);
    } else if(!strcmp(argv[1],"load")) {
        assert(argc==4); book_progress_t p={0};
        if(book_progress_load(argv[2],(uint32_t)strtoul(argv[3],NULL,10),&p)) show(argv[2],&p,NULL);
    } else if(!strcmp(argv[1],"list")) err=book_progress_list(show,NULL);
    else if(!strcmp(argv[1],"set-last")) err=book_progress_set_last_path(argv[2]);
    else if(!strcmp(argv[1],"last")) {char path[288];if(book_progress_last_path(path,sizeof(path)))puts(path);}
    else if(!strcmp(argv[1],"forget")) err=book_progress_forget(argv[2]);
    else if(!strcmp(argv[1],"clear")) err=book_progress_clear(argv[2]);
    else if(!strcmp(argv[1],"backup") || !strcmp(argv[1],"restore")) {
        bool restore=!strcmp(argv[1],"restore");
        FILE *f=fopen("build/simulator/history.bin",restore?"rb":"wb");assert(f);
        err=restore?book_history_backup_restore(f):book_history_backup_write(f);assert(!fclose(f));
    } else return 2;
    printf("err=%d\n",err);return err==ESP_OK?0:1;
}
