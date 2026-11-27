#pragma once

// Open Transfer directly in USB storage mode.
void app_transfer_request_usb_start(void);

// File manager opens the selected transfer mode directly, without the method picker.
void app_transfer_request_wifi_upload(void);
void app_transfer_request_hotspot_start(void);

// Settings uses this before opening Transfer to enter WiFi provisioning directly.
void app_transfer_request_wifi_setup(void);
// Opens the same Wi-Fi setup and returns to the font installer.
void app_transfer_request_standard_font_wifi(void);

// 应用页打开文件传输方式选择，选择前不启动服务。
// The Apps grid opens the transfer picker without starting any method.
void app_transfer_request_method_picker(void);
