// Fake <esp_err.h>.
// SPDX-License-Identifier: MIT
// Part of TinyDraw's puck module. See ../README.md.
#ifndef TINYDRAW_PUCK_SHIM_ESP_ERR_H
#define TINYDRAW_PUCK_SHIM_ESP_ERR_H
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_NOT_FOUND 0x105
#endif
