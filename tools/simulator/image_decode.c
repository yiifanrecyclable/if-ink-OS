/* SPDX-License-Identifier: Apache-2.0
 * 中文：宿主图片解码使用 macOS ImageIO；原固件仍使用其嵌入式解码器。
 * English: ImageIO host decoding; firmware retains its embedded image decoders.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include "book_cover.h"
#include "book_cover_auto.h"
#include "book_epub.h"
book_crop_t book_cover_crop(unsigned sw,unsigned sh,unsigned dw,unsigned dh){
 book_crop_t crop={0,0,sw,sh};if(!sw||!sh||!dw||!dh)return crop;
 if((uint64_t)sw*dh>(uint64_t)sh*dw){crop.width=(unsigned)((uint64_t)sh*dw/dh);if(!crop.width)crop.width=1;crop.x=(sw-crop.width)/2;}
 else{crop.height=(unsigned)((uint64_t)sw*dh/dw);if(!crop.height)crop.height=1;crop.y=(sh-crop.height)/2;}return crop;
}
static CGImageRef decoded(const uint8_t *data,size_t size){
 if(!data||!size||size>2*1024*1024)return NULL;
 CFDataRef bytes=CFDataCreate(NULL,data,size);if(!bytes)return NULL;
 CGImageSourceRef src=CGImageSourceCreateWithData(bytes,NULL);CFRelease(bytes);
 if(!src)return NULL;CGImageRef img=CGImageSourceCreateImageAtIndex(src,0,NULL);CFRelease(src);return img;
}
bool book_image_dimensions(const uint8_t*d,size_t n,bool png,unsigned*w,unsigned*h){
 CGImageRef img=decoded(d,n);if(!img)return false;*w=CGImageGetWidth(img);*h=CGImageGetHeight(img);CGImageRelease(img);return *w&&*h;
}
bool book_image_grayscale(const uint8_t*d,size_t n,bool png,unsigned w,unsigned h,uint8_t*out){
 if(!w||!h||w>1216||h>1216||!out)return false;
 CGImageRef img=decoded(d,n);if(!img)return false;
 book_crop_t crop=book_cover_crop((unsigned)CGImageGetWidth(img),(unsigned)CGImageGetHeight(img),w,h);
 CGImageRef clipped=CGImageCreateWithImageInRect(img,CGRectMake(crop.x,crop.y,crop.width,crop.height));CGImageRelease(img);img=clipped;if(!img)return false;
 CGColorSpaceRef color=CGColorSpaceCreateDeviceGray();
 CGContextRef ctx=CGBitmapContextCreate(out,w,h,8,w,color,kCGImageAlphaNone);CGColorSpaceRelease(color);
 if(!ctx){CGImageRelease(img);return false;}
 CGContextSetGrayFillColor(ctx,1,1);CGContextFillRect(ctx,CGRectMake(0,0,w,h));
 // 灰度位图缓冲的行序已与CGImage一致，额外坐标翻转会把封面和插图倒置。
 // Bitmap rows already match CGImage order; an additional transform inverts covers and inline images.
 CGContextDrawImage(ctx,CGRectMake(0,0,w,h),img);CGContextRelease(ctx);CGImageRelease(img);return true;
}
bool book_cover_thumbnail(const uint8_t *data,size_t size,bool png,uint8_t out[BOOK_COVER_W*BOOK_COVER_H]){
 return book_image_grayscale(data,size,png,BOOK_COVER_W,BOOK_COVER_H,out);
}
