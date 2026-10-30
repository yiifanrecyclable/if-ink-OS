/* SPDX-License-Identifier: Apache-2.0
 * 中文：逐行压缩并提交拼豆PNG；内存只保留行、压缩块和PSRAM压缩状态。
 * English: Compress and commit bead PNG rows with bounded buffers and PSRAM compression state.
 */
#include "beads_png.h"
#include "ui_bitmap.h"
#include "esp_heap_caps.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
static void be32(uint8_t *out,uint32_t n) { out[0]=n>>24;out[1]=n>>16;out[2]=n>>8;out[3]=n; }
static bool chunk(FILE *f,const char *type,const uint8_t *data,size_t n) {
    uint8_t word[4];be32(word,(uint32_t)n);
    if(fwrite(word,1,4,f)!=4||fwrite(type,1,4,f)!=4||(n&&fwrite(data,1,n,f)!=n))return false;
    uLong sum=crc32(0,(const Bytef*)type,4);if(n)sum=crc32(sum,data,(uInt)n);be32(word,(uint32_t)sum);
    return fwrite(word,1,4,f)==4;
}
static void *zalloc_psram(void *opaque,uInt n,uInt size) { (void)opaque;return heap_caps_calloc(n,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT); }
static void zfree_psram(void *opaque,void *p) { (void)opaque;heap_caps_free(p); }
static bool encode(FILE *f,const uint8_t *fb) {
    static const uint8_t signature[]={137,80,78,71,13,10,26,10};
    uint8_t header[13]={0};be32(header,684);be32(header+4,1216);header[8]=4;
    if(fwrite(signature,1,8,f)!=8||!chunk(f,"IHDR",header,13))return false;
    // 压缩输出和行都在堆上，避免占满主循环栈。/ Heap buffers avoid exhausting the application-loop stack.
    uint8_t *memory=heap_caps_malloc(4096+343,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!memory)return false;
    uint8_t *out=memory,*row=memory+4096;z_stream z={.zalloc=zalloc_psram,.zfree=zfree_psram};
    bool ok=deflateInit(&z,6)==Z_OK;
    if(!ok){free(memory);return false;}
    for(int y=0;y<1216&&ok;++y) {
        row[0]=0;ui_bitmap_capture(fb,(EpdRect){0,y,684,1},row+1);
        // epdiy低半字节在前，PNG高半字节在前。/ Swap epdiy's low-first pixels to PNG's high-first order.
        for(int i=1;i<343;++i)row[i]=(uint8_t)((row[i]<<4)|(row[i]>>4));
        z.next_in=row;z.avail_in=343;
        while(z.avail_in&&ok) {
            z.next_out=out;z.avail_out=4096;
            ok=deflate(&z,Z_NO_FLUSH)==Z_OK;
            size_t n=4096-z.avail_out;if(ok&&n)ok=chunk(f,"IDAT",out,n);
        }
    }
    int status=Z_OK;
    while(ok&&status!=Z_STREAM_END) {
        z.next_out=out;z.avail_out=4096;status=deflate(&z,Z_FINISH);
        ok=status==Z_OK||status==Z_STREAM_END;
        size_t n=4096-z.avail_out;if(ok&&n)ok=chunk(f,"IDAT",out,n);
    }
    deflateEnd(&z);free(memory);
    return ok&&chunk(f,"IEND",NULL,0)&&!ferror(f);
}
bool beads_png_save(const uint8_t *fb,char *path,size_t capacity) {
    if(!fb||!path||capacity<1)return false;
    path[0]=0;
    if(mkdir("/sdcard/pictures",0775)&&errno!=EEXIST)return false;
    if(mkdir("/sdcard/pictures/pindou",0775)&&errno!=EEXIST)return false;
    char final[128],stage[144];int reserve=-1,fd=-1;
    for(unsigned i=1;i<=9999;++i) {
        snprintf(final,sizeof(final),"/sdcard/pictures/pindou/pindou-%04u.png",i);
        if(strlen(final)>=capacity)return false;
        reserve=open(final,O_WRONLY|O_CREAT|O_EXCL,0664);
        if(reserve<0){if(errno==EEXIST)continue;return false;}
        close(reserve);snprintf(stage,sizeof(stage),"%s.part",final);
        fd=open(stage,O_WRONLY|O_CREAT|O_EXCL,0664);
        if(fd<0){int error=errno;unlink(final);if(error==EEXIST)continue;return false;}
        break;
    }
    if(fd<0)return false;
    FILE *f=fdopen(fd,"wb");
    if(!f){close(fd);unlink(stage);unlink(final);return false;}
    bool ok=encode(f,fb);
    if(ok)ok=fflush(f)==0;
    if(ok)ok=fsync(fileno(f))==0;
    if(fclose(f))ok=false;
    // FatFs不替换现有目标；仅移除本次独占预留的空文件。传输页退出后才可进入拼豆。
    // FatFs cannot replace a destination; remove only our exclusive empty reservation. Beads starts after transfer exits.
    if(ok)ok=unlink(final)==0;
    if(ok)ok=rename(stage,final)==0;
    if(!ok){unlink(stage);unlink(final);return false;}
    snprintf(path,capacity,"%s",final);return true;
}

#define BOARD_BYTES (16+BEADS_COUNT+4)
static const char *const board_slots[]={"/sdcard/pictures/pindou/.board-a","/sdcard/pictures/pindou/.board-b"};
static uint32_t read32(const uint8_t *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static bool read_board(int slot,uint8_t data[BOARD_BYTES]) {
    FILE *f=fopen(board_slots[slot],"rb");if(!f)return false;
    bool ok=fread(data,1,BOARD_BYTES,f)==BOARD_BYTES&&fgetc(f)==EOF&&!ferror(f);
    if(fclose(f))ok=false;
    if(!ok||memcmp(data,"IFBEADS1",8)||data[12]>=6||data[13]!=BEADS_COLS||data[14]!=BEADS_ROWS||data[15]||
       read32(data+BOARD_BYTES-4)!=(uint32_t)crc32(0,data,BOARD_BYTES-4))return false;
    for(int i=0;i<BEADS_COUNT;++i)if(data[16+i]!=255&&data[16+i]>=6)return false;
    return true;
}
// 两份小状态交替提交，不依赖FatFs覆盖改名；写断时仍可恢复上一份。
// Alternate two small records without FatFs replacement renames, retaining a recovery copy after interrupted writes.
static int latest_board(uint8_t a[BOARD_BYTES],uint8_t b[BOARD_BYTES]) {
    bool va=read_board(0,a),vb=read_board(1,b);
    if(!va)return vb?1:-1;
    if(!vb)return 0;
    return (int32_t)(read32(b+8)-read32(a+8))>0?1:0;
}
bool beads_board_load(cyber_beads_t *board) {
    if(!board)return false;
    uint8_t a[BOARD_BYTES],b[BOARD_BYTES];int slot=latest_board(a,b);if(slot<0)return false;
    const uint8_t *record=slot?b:a;
    cyber_beads_reset(board);memcpy(board->cells,record+16,BEADS_COUNT);board->selected=record[12];
    cyber_beads_saved(board);return true;
}
bool beads_board_save(const cyber_beads_t *board) {
    if(!board||board->selected>=6)return false;
    for(int i=0;i<BEADS_COUNT;++i)if(board->cells[i]<-1||board->cells[i]>=6)return false;
    if(mkdir("/sdcard/pictures",0775)&&errno!=EEXIST)return false;
    if(mkdir("/sdcard/pictures/pindou",0775)&&errno!=EEXIST)return false;
    uint8_t a[BOARD_BYTES],b[BOARD_BYTES];int latest=latest_board(a,b);
    uint32_t generation=latest<0?1:read32((latest?b:a)+8)+1;
    uint8_t *data=latest==0?b:a;memset(data,0,BOARD_BYTES);
    memcpy(data,"IFBEADS1",8);be32(data+8,generation);data[12]=board->selected;data[13]=BEADS_COLS;data[14]=BEADS_ROWS;
    memcpy(data+16,board->cells,BEADS_COUNT);be32(data+BOARD_BYTES-4,(uint32_t)crc32(0,data,BOARD_BYTES-4));
    int target=latest==0?1:0;FILE *f=fopen(board_slots[target],"wb");if(!f)return false;
    bool ok=fwrite(data,1,BOARD_BYTES,f)==BOARD_BYTES&&fflush(f)==0;
    if(ok)ok=fsync(fileno(f))==0;
    if(fclose(f))ok=false;
    if(!ok)unlink(board_slots[target]);
    return ok;
}
