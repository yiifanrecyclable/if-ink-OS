#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BOOK_COVER_W 176
#define BOOK_COVER_H 240

// Output is one grayscale byte per pixel (0=black, 255=white).
bool book_cover_thumbnail(const uint8_t *data, size_t size, bool png,
                          uint8_t out[BOOK_COVER_W * BOOK_COVER_H]);
// Decode an image to a chosen grayscale size; output is one byte per pixel.
bool book_image_grayscale(const uint8_t *data, size_t size, bool png,
                          unsigned out_width, unsigned out_height, uint8_t *out);
bool book_image_dimensions(const uint8_t *data, size_t size, bool png,
                           unsigned *width, unsigned *height);

/// 源图取样窗口，按「填满目标再居中裁剪」计算。
/// 长宽比不同时按长边铺满、裁掉多余的一边，绝不拉伸。
/// Source sampling window computed as cover-then-centre-crop: the longer side fills the target
/// and the excess on the other axis is cropped, so a mismatched aspect ratio is never stretched.
typedef struct {
    unsigned x, y, width, height;
} book_crop_t;

book_crop_t book_cover_crop(unsigned src_width, unsigned src_height,
                            unsigned dst_width, unsigned dst_height);

/// 优先读取书内封面，缺少或无法解码时按书籍标识生成并缓存灰阶封面。
/// Prefer a valid embedded cover; otherwise generate and cache a deterministic grayscale cover.
bool book_cover_load_gray(const char *path, const char *title, const char *author,
                          uint8_t out[BOOK_COVER_W * BOOK_COVER_H],
                          bool allow_decode, bool *pending);

/// 完整封面等比缩入边界，不裁切；输出至少1像素。/ Contain the entire source within bounds without cropping, at least one pixel.
static inline void book_cover_fit_size(unsigned source_w,unsigned source_h,unsigned max_w,unsigned max_h,unsigned *width,unsigned *height){
    if(!source_w||!source_h||!max_w||!max_h){*width=*height=0;return;}
    if((uint64_t)source_w*max_h>(uint64_t)source_h*max_w){*width=max_w;*height=(unsigned)((uint64_t)max_w*source_h/source_w);}
    else{*height=max_h;*width=(unsigned)((uint64_t)max_h*source_w/source_h);}
    if(!*width)*width=1;
    if(!*height)*height=1;
}
// 封面按最终显示尺寸裁角，首页与书架共用。/ Clip covers at their final display size on both Home and the shelf.
#define BOOK_COVER_RADIUS 7
static inline bool book_cover_pixel_inside(int x,int y,int width,int height){
    int radius=BOOK_COVER_RADIUS;if(radius>width/2)radius=width/2;if(radius>height/2)radius=height/2;
    if(radius<=0)return true;
    int cx=x<radius?radius:x>=width-radius?width-1-radius:x;
    int cy=y<radius?radius:y>=height-radius?height-1-radius:y;
    int dx=x-cx,dy=y-cy;return dx*dx+dy*dy<=radius*radius;
}
/// 缓冲容量仍为176x240，实际像素紧密排列，宽高随图返回；独立版本缓存避开旧裁切缩略图。
/// Keep the 176x240 capacity but return tightly packed pixels and actual dimensions; use a separate cache revision from cropped thumbnails.
bool book_cover_load_fitted_gray(const char *path,const char *title,const char *author,
                                uint8_t out[BOOK_COVER_W*BOOK_COVER_H],bool allow_decode,bool *pending,
                                unsigned *width,unsigned *height);
