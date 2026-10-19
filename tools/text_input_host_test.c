/* SPDX-License-Identifier: Apache-2.0
 * 共用键盘的实际编辑状态与几何测试。/ Exercise the production editor state and shared geometry.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../main/ui/ui_text_input.c"
#include "../main/ui/ui_pinyin.c"
static int drawn_buttons, footer_bottom;
static int field_calls;
static char drawn_field[1025];
void ui_clear_page(uint8_t *fb){(void)fb;}
void ui_nav_status(uint8_t *fb){(void)fb;}
void ui_nav_back(uint8_t *fb,int x,int y){(void)fb;(void)x;(void)y;}
void ui_fill_round_rect(uint8_t *fb,EpdRect r,int radius,uint8_t gray){(void)fb;(void)radius;(void)gray;++drawn_buttons;if(r.y>=UI_INPUT_FOOTER_TOP)footer_bottom=r.y+r.height;assert(r.y>=0&&r.y+r.height<=1216);}
void ui_draw_round_rect(uint8_t *fb,EpdRect r,int radius,uint8_t gray){(void)fb;(void)r;(void)radius;(void)gray;}
void ui_text_fixed(uint8_t *fb,int x,int y,int px,const char *s,enum EpdFontFlags align,bool inverted){(void)fb;(void)align;(void)inverted;if(x==58&&y==UI_PAGE_Y(286)&&px==ui_text_effective_px(UI_TEXT_BODY)){++field_calls;snprintf(drawn_field,sizeof(drawn_field),"%s",s);}}
void ui_text_fixed_vc(uint8_t *fb,int x,int y,int px,const char *s,enum EpdFontFlags align,bool inverted){ui_text_fixed(fb,x,y,px,s,align,inverted);}
int ui_text_fixed_width_px(int px,const char *s){int n=0;for(size_t at=0;s[at];at=next_character(s,at))++n;return n*px;}
void epd_fill_rect(EpdRect r,uint8_t color,uint8_t *fb){(void)r;(void)color;(void)fb;}
bool ui_rect_hit(EpdRect r,uint16_t x,uint16_t y){return x>=r.x&&y>=r.y&&x<r.x+r.width&&y<r.y+r.height;}
static ui_input_result_t tap(ui_text_input_t *in,int id){EpdRect r=input_key_rect(in,id);ui_gesture_event_t ev={.type=UI_GESTURE_TAP,.x0=r.x+r.width/2,.x=r.x+r.width/2,.y0=r.y+r.height/2,.y=r.y+r.height/2};return ui_text_input_gesture(in,&ev);}
static void spell(ui_text_input_t *in,const char *s){const char *keys="1234567890qwertyuiopasdfghjkl'zxcvbnm,./";for(;*s;++s){const char *p=strchr(keys,*s);assert(p);assert(tap(in,p-keys)==UI_INPUT_CHANGED);}}
int main(int argc,char **argv){
 (void)argv;
 char text[257]="甲乙";ui_text_input_t in;
 ui_text_input_open(&in,text,sizeof(text),true,false,UI_INPUT_ENGLISH);
 assert(in.cursor==6);tap(&in,LEFT);assert(in.cursor==3);assert(ui_text_input_insert(&in,"A"));assert(!strcmp(text,"甲A乙"));tap(&in,BACKSPACE);assert(!strcmp(text,"甲乙"));tap(&in,BACKSPACE);assert(!strcmp(text,"乙")&&in.cursor==0);tap(&in,RIGHT);assert(in.cursor==3);
 tap(&in,CLEAR);assert(ui_text_input_insert(&in,"😀中"));tap(&in,LEFT);assert(in.cursor==4);tap(&in,BACKSPACE);assert(!strcmp(text,"中")&&in.cursor==0);
 assert(!ui_text_input_insert(&in,"\xc0\x80"));assert(!strcmp(text,"中"));assert(!ui_text_input_insert(&in,"\xed\xa0\x80"));assert(!ui_text_input_insert(&in,"\n"));
 char small[4]="";ui_text_input_open(&in,small,sizeof(small),true,false,UI_INPUT_CHINESE);spell(&in,"wo");assert(in.candidate_count);assert(tap(&in,DONE)==UI_INPUT_CHANGED);assert(in.pinyin[0]);tap(&in,CANDIDATE);assert(strlen(small)==3);spell(&in,"wo");tap(&in,CANDIDATE);assert(in.pinyin[0]&&strlen(small)==3);tap(&in,BACKSPACE);assert(!strcmp(in.pinyin,"w"));tap(&in,CLEAR);assert(!*small&&!in.pinyin[0]);
 text[0]=0;ui_text_input_open(&in,text,sizeof(text),true,false,UI_INPUT_ENGLISH);spell(&in,"abc");assert(!strcmp(text,"abc"));tap(&in,SHIFT);spell(&in,"d");assert(!strcmp(text,"abcD"));tap(&in,LANGUAGE);assert(in.mode==UI_INPUT_CHINESE);spell(&in,"wo");assert(!strcmp(in.pinyin,"wo"));tap(&in,RAW);assert(!strcmp(text,"abcDwo"));
 tap(&in,CLEAR);spell(&in,"wo");size_t page=in.candidate_page;while(in.candidate_count>5)tap(&in,NEXT);size_t last=in.candidate_page;assert(last>page);tap(&in,NEXT);assert(in.candidate_page==last);tap(&in,PREVIOUS);assert(in.candidate_page==last-1);tap(&in,CLEAR);
 assert(tap(&in,DONE)==UI_INPUT_DONE);assert(tap(&in,CANCEL)==UI_INPUT_CANCEL);
 // 所有可见英文标点都能通过键盘输入；密码页没有中文标点。/ All printable ASCII punctuation is reachable; secret keyboard offers ASCII symbols.
 ui_text_input_open(&in,text,sizeof(text),false,true,UI_INPUT_ENGLISH);tap(&in,SYMBOLS);bool seen[128]={0};
 for(unsigned page2=0;page2<2;++page2){for(int i=0;i<40;++i){char key[8];key_label(&in,i,key);assert(strlen(key)==1);seen[(unsigned char)key[0]]=true;}tap(&in,NEXT);}
 for(int c=33;c<127;++c)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')))assert(seen[c]);
 spell(&in,"");tap(&in,CLEAR);tap(&in,SYMBOLS);spell(&in,"abc");assert(!in.revealed);tap(&in,REVEAL);assert(in.revealed);ui_text_input_clear(&in);assert(!in.revealed);for(size_t i=0;i<sizeof(text);++i)assert(text[i]==0);
 // 非同一按钮释放不提交。/ Releasing outside the originating button never commits.
 EpdRect r=ui_text_input_key_rect(DONE);ui_gesture_event_t ev={.type=UI_GESTURE_TAP,.x0=r.x+2,.y0=r.y+2,.x=0,.y=0};assert(ui_text_input_gesture(&in,&ev)==UI_INPUT_NONE);
 for(int key=0;key<40;++key){r=ui_text_input_key_rect(key);assert(r.y>=UI_INPUT_KEYS_TOP&&r.y+r.height<UI_INPUT_FOOTER_TOP);}
 for(int i=0;i<4;++i){ui_text_input_open(&in,text,sizeof(text),i%2,i==3,i%2?UI_INPUT_CHINESE:UI_INPUT_ENGLISH);drawn_buttons=footer_bottom=0;ui_text_input_draw(NULL,&in,"测试","",NULL,"完成",NULL);assert(drawn_buttons>=56&&footer_bottom==1200);}
 if(argc>1){
  char words[6][UI_PINYIN_WORD_BYTES];assert(ui_pinyin_candidates("yuedu",words,6,0)==2);assert(!strcmp(words[0],"阅读"));
  tap(&in,CLEAR);ui_text_input_open(&in,text,sizeof(text),true,false,UI_INPUT_CHINESE);spell(&in,"yuedu");tap(&in,CANDIDATE);assert(!strcmp(text,"阅读")&&!in.pinyin[0]);
  assert(ui_pinyin_candidates("ceshi",words,6,5)==6);assert(ui_pinyin_candidates("ceshi",words,6,10)==2);
 }
 strcpy(text,"Agyp,@a.com");ui_text_input_open(&in,text,sizeof(text),false,false,UI_INPUT_ENGLISH);
 field_calls=0;ui_text_input_draw(NULL,&in,"测试","",NULL,NULL,NULL);
 assert(field_calls==1&&!strcmp(drawn_field,text));
 ui_text_input_open(&in,text,sizeof(text),false,true,UI_INPUT_ENGLISH);
 field_calls=0;ui_text_input_draw(NULL,&in,"测试","",NULL,NULL,NULL);
 assert(field_calls==1&&strlen(drawn_field)==strlen(text));for(const char *p=drawn_field;*p;++p)assert(*p=='*');
 // Banner可显式换行，限制四行并可跨行移动/删除；普通字段继续拒绝换行。/ Banner supports four explicit lines and cross-line editing; ordinary fields reject newlines.
 text[0]=0;ui_text_input_open(&in,text,sizeof(text),true,false,UI_INPUT_ENGLISH);in.max_lines=4;
 spell(&in,"one");tap(&in,NEWLINE);spell(&in,"two");tap(&in,NEWLINE);spell(&in,"three");tap(&in,NEWLINE);spell(&in,"four");
 assert(!strcmp(text,"one\ntwo\nthree\nfour"));tap(&in,NEWLINE);assert(!strcmp(text,"one\ntwo\nthree\nfour"));
 ui_text_input_draw(NULL,&in,"Banner","",NULL,NULL,NULL);place_cursor(&in,58,input_key_rect(&in,FIELD).y+32+44);assert(in.cursor==4);
 tap(&in,BACKSPACE);assert(!strcmp(text,"onetwo\nthree\nfour"));tap(&in,CLEAR);assert(!text[0]&&in.max_lines==4);
 ui_text_input_open(&in,text,sizeof(text),true,false,UI_INPUT_CHINESE);in.max_lines=4;spell(&in,"wo");tap(&in,NEWLINE);assert(!text[0]&&in.pinyin[0]);
 ui_text_input_open(&in,text,sizeof(text),false,true,UI_INPUT_ENGLISH);in.max_lines=4;assert(!ui_text_input_insert(&in,"\n"));
 puts("Shared input: UTF-8 editing, overflow, candidates, modes, full ASCII symbols, secrets, fixed bottom geometry and common field baseline passed");
 return 0;
}

int ui_text_effective_px(int px){return px+2;}
int ui_text_fit_px(int px,const char *s,int w,int h){int size=px+2;while(size>16&&(size>h||ui_text_fixed_width_px(size,s)>w))--size;return size;}
