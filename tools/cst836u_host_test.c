/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实CST836U驱动和手势识别器的原始报文回归，不接触硬件。
 * English: Raw-report regressions through the real CST836U driver and gesture recognizer, without hardware.
 */
#include "cst836u.h"
#include "ui_gesture.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t packet[CST836U_RAW_LEN], selected;
static int reads;
static esp_err_t read_error;
esp_err_t gpio_config(const gpio_config_t *c) { (void)c; return ESP_OK; }
esp_err_t gpio_set_level(gpio_num_t g,int n) { (void)g;(void)n;return ESP_OK; }
int gpio_get_level(gpio_num_t g) { (void)g;return 1; }
void esp_rom_delay_us(uint32_t us) { (void)us; }
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t b,const i2c_device_config_t *c,i2c_master_dev_handle_t *d) {
    (void)c;*d=b;return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t d) { (void)d;return ESP_OK; }
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t d,const uint8_t *p,size_t n,int timeout) {
    (void)d;(void)timeout;if(n==1)selected=p[0];return ESP_OK;
}
esp_err_t i2c_master_receive(i2c_master_dev_handle_t d,uint8_t *p,size_t n,int timeout) {
    (void)d;(void)timeout;
    if(selected==0xA6){memset(p,0,n);return ESP_OK;}
    assert(n==sizeof(packet));++reads;
    if(read_error!=ESP_OK)return read_error;
    memcpy(p,packet,n);return ESP_OK;
}
static void point(int slot,int id,int event,int x,int y) {
    uint8_t *p=packet+3+slot*6;
    p[0]=(uint8_t)((event<<6)|(x>>8));p[1]=(uint8_t)x;
    p[2]=(uint8_t)((id<<4)|(y>>8));p[3]=(uint8_t)y;
}
static cst836u_touch_t read_touch(cst836u_handle_t h) {
    cst836u_touch_t t;assert(cst836u_read(h,&t)==ESP_OK);return t;
}
int main(void) {
    int bus;
    cst836u_handle_t h=NULL;
    cst836u_config_t config=CST836U_CONFIG_DEFAULT();
    assert(cst836u_init(&bus,&config,&h)==ESP_OK);
    // 零点、全零坐标和旧按下槽均不能制造按住；原实现会失败。
    // Neither zeroed reports nor stale down slots may fabricate a hold; the old implementation fails here.
    cst836u_touch_t t=read_touch(h);assert(!t.touched&&!t.count);
    point(0,0,0,100,400);point(1,1,2,600,900);
    t=read_touch(h);assert(!t.touched&&!t.count&&!t.points[0].active&&!t.points[1].active);
    packet[2]=1;t=read_touch(h);
    assert(t.touched&&t.count==1&&t.x==100&&t.y==400&&!t.points[1].active);
    // 第一槽抬起、第二槽仍移动时，后者是主坐标，而不是旧手指。
    // When slot zero lifts and slot one moves, the remaining finger owns the primary coordinates.
    point(0,0,1,100,400);t=read_touch(h);
    assert(t.touched&&t.count==1&&t.x==600&&t.y==900&&!t.points[0].active&&t.points[1].active);
    packet[2]=2;point(0,0,0,100,400);t=read_touch(h);
    assert(t.touched&&t.count==2&&t.points[0].active&&t.points[1].active);
    point(0,0,1,100,400);point(1,1,1,600,900);t=read_touch(h);assert(!t.touched&&!t.count);
    // 连续100次短按：读两次按下、一次残留坐标松手，每次恰好一个TAP/按键边沿。
    // Replay 100 taps with repeated down reports and stale release slots; require exactly one tap/key edge each.
    for(int channel=0;channel<2;++channel) {
        ui_gesture_t gesture={0};bool was_down=false;int taps=0,key_presses=0;
        cst836u_touch_t latest={0};
        for(int n=0;n<100;++n)for(int phase=0;phase<3;++phase) {
            packet[2]=phase==2?0:1;point(0,0,0,100,channel?1500:400);
            point(1,15,3,0,0);t=read_touch(h);
            // 主循环松手时保留最后一次有效坐标。/ The loop retains the last valid position on release.
            if(t.touched)latest=t;
            else {latest.touched=false;latest.count=0;}
            app_ctx_t ctx={.touch=&latest,.now_ms=n*100+phase*10,
                .pressed=t.touched&&!was_down,.released=!t.touched&&was_down};
            if(channel&&ctx.pressed)++key_presses;
            ui_gesture_event_t event;
            if(ui_gesture_feed(&gesture,&ctx,&event)&&event.type==UI_GESTURE_TAP)++taps;
            was_down=t.touched;
        }
        assert(taps==100&&(!channel||key_presses==100)&&!gesture.active);
    }
    packet[2]=3;reads=0;assert(cst836u_read(h,&t)==ESP_ERR_INVALID_RESPONSE&&reads==3);
    read_error=ESP_ERR_TIMEOUT;reads=0;assert(cst836u_read(h,&t)==ESP_ERR_TIMEOUT&&reads==1);
    read_error=ESP_OK;packet[2]=0;t=read_touch(h);assert(!t.touched);
    assert(cst836u_set_mode(h,CST836U_MODE_DEEPSLEEP)==ESP_OK);
    reads=0;t=read_touch(h);assert(!t.touched&&!reads);
    assert(cst836u_deinit(h)==ESP_OK);
    puts("CST836U raw reports: zero/stale slots, primary handoff, 200 repeated taps, invalid/error/sleep passed");
}
