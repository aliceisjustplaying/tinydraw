// Fake <pcf85063a.h>: just the device struct rtc_clock.h stores by value.
// SPDX-License-Identifier: MIT
// Part of TinyDraw's puck module. See ../README.md.
#ifndef TINYDRAW_PUCK_SHIM_PCF85063A_H
#define TINYDRAW_PUCK_SHIM_PCF85063A_H
typedef struct {
  void* handle;
} pcf85063a_dev_t;
#endif
