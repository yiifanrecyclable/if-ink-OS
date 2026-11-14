/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
typedef struct { int x, y, width, height; } EpdRect;
enum EpdFontFlags { EPD_DRAW_ALIGN_LEFT, EPD_DRAW_ALIGN_CENTER, EPD_DRAW_ALIGN_RIGHT };
enum EpdRotation {EPD_ROT_LANDSCAPE,EPD_ROT_PORTRAIT,EPD_ROT_INVERTED_LANDSCAPE,EPD_ROT_INVERTED_PORTRAIT};
enum EpdDrawMode {MODE_DU,MODE_GL16,MODE_GC16};
enum EpdDrawError {EPD_DRAW_SUCCESS};
void epd_draw_line(int x0,int y0,int x1,int y1,uint8_t color,uint8_t *fb);
void epd_draw_rect(EpdRect r,uint8_t color,uint8_t *fb);
void epd_draw_circle(int x,int y,int radius,uint8_t color,uint8_t *fb);
void epd_fill_circle(int x,int y,int radius,uint8_t color,uint8_t *fb);
void epd_fill_circle_helper(int x,int y,int radius,int corners,int delta,uint8_t color,uint8_t *fb);
uint8_t epd_get_pixel(int x,int y,int width,int height,const uint8_t *fb);
enum EpdRotation epd_get_rotation(void);
void epd_set_rotation(enum EpdRotation rotation);
int epd_width(void);
int epd_height(void);
int epd_rotated_display_width(void);
int epd_rotated_display_height(void);
void epd_draw_rotated_image(EpdRect r,const uint8_t *image,uint8_t *fb);
void epd_fill_triangle(int x0,int y0,int x1,int y1,int x2,int y2,uint8_t color,uint8_t *fb);
void epd_draw_pixel(int x, int y, uint8_t color, uint8_t *fb);
void epd_fill_rect(EpdRect rect, uint8_t color, uint8_t *fb);
