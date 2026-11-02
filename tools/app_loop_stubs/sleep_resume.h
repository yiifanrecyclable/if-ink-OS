#pragma once
#include <stddef.h>
static inline void sleep_resume_set_target(unsigned tab,const char *path){(void)tab;(void)path;}

static inline esp_err_t sleep_resume_clear(void){return ESP_OK;}
