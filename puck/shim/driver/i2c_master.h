// Fake <driver/i2c_master.h>. The board hangs touch, the PMIC and the RTC off
// one I2C bus, and the product headers pass its handle between the three
// adapters. None of those adapters survives into wasm (see ../physical_touch.h,
// ../power_manager.h, ../rtc_clock.h), so this is only enough of a type for
// those signatures to keep their shape.
//
// SPDX-License-Identifier: MIT
// Part of TinyDraw's puck module. See ../../README.md.
#ifndef TINYDRAW_PUCK_SHIM_DRIVER_I2C_MASTER_H
#define TINYDRAW_PUCK_SHIM_DRIVER_I2C_MASTER_H

typedef struct i2c_master_bus_t* i2c_master_bus_handle_t;
typedef struct i2c_master_dev_t* i2c_master_dev_handle_t;

#endif  // TINYDRAW_PUCK_SHIM_DRIVER_I2C_MASTER_H
