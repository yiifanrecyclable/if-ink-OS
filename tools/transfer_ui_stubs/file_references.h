#pragma once
#include "transfer_ui_test_env.h"
static inline esp_err_t file_references_begin(const char *a,const char *b,bool *done){(void)a;(void)b;*done=false;return ESP_OK;}
static inline esp_err_t file_references_finish(void){return ESP_OK;}
