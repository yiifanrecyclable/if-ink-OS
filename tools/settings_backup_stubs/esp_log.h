/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdio.h>
#define ESP_LOGW(tag,...) ((void)(tag),(void)snprintf(NULL,0,__VA_ARGS__))
#define ESP_LOGE(tag,...) ((void)(tag),(void)snprintf(NULL,0,__VA_ARGS__))
#define ESP_LOGI(tag,...) ((void)(tag),(void)snprintf(NULL,0,__VA_ARGS__))
