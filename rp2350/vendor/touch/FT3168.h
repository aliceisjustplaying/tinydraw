/*****************************************************************************
* | File      	:   FT3168.h
* | Author      :   Waveshare Team
* | Function    :   FT3168 Interface Functions
* | Info        :
*----------------
* |	This version:   V1.0
* | Date        :   2025-03-20
* | Info        :   
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documnetation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of theex Software, and to permit persons to  whom the Software is
# furished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
******************************************************************************/
#ifndef _FT3168_H_
#define _FT3168_H_

#include <stdint.h>
#include "hardware/i2c.h"
#include "pico/time.h"

#define FT3168_I2C_ADDR  0x38

//Register
#define REG_GESTURE_ID  		0xD3
#define REG_FINGER_NUM 	 		0x02
#define REG_X1_H				0x03
#define REG_X1_L        		0x04
#define REG_Y1_H        		0x05
#define REG_Y1_L         		0x06
#define REG_MONITOR_MODE		0x86
#define REG_MONITOR_TIME		0x87
#define FT3168_RD_DEVICE_ID 	0xA0
#define REG_POWER_MODE      	0xA5
#define FT3168_RD_WR_DEVICE_GESTUREID_MODE 			0xD0
#define FT3168_RD_WR_DEVICE_PROXIMITY_SENSING_MODE 	0xB0

typedef enum {
    FT3168_DEVICE_ON,
    FT3168_DEVICE_OFF
} Device_State;

typedef enum {
    FT3168_POWER_ACTIVE,
    FT3168_POWER_MONITOR,
    FT3168_POWER_STANDBY,
    FT3168_POWER_HIBERNATE
} Device_Mode;

typedef enum
{
	FT3168_Point_Mode = 1,
	FT3168_Gesture_Mode,
} FT3168_Mode;

typedef enum {
	FT3168_GESTURE_ID,
    FT3168_FINGER_NUMBER,
    FT3168_COORDINATE_X,
    FT3168_COORDINATE_Y,
} Value_Information;

typedef enum
{
	FT3168_Gesture_None  = 0,
	FT3168_Gesture_Right = 0x21,
	FT3168_Gesture_Left  = 0x20,
	FT3168_Gesture_Down  = 0x23,
	FT3168_Gesture_Up    = 0x22,
	FT3168_Gesture_Click = 0x26,
	FT3168_Gesture_Double_Click = 0x24,
} FT3168_Gesture;

typedef struct
{
	uint8_t mode;
	uint16_t x_point;
	uint16_t y_point;
} FT3168_Struct;

extern FT3168_Struct FT3168;

void FT3168_Init(uint8_t mode);
void FT3168_Reset();
uint16_t FT3168_ReadID();
uint16_t FT3168_ReadState(Value_Information info);
void FT3168_Get_Point();
uint8_t FT3168_Get_Gesture();

#endif // !_FT3168_H_
