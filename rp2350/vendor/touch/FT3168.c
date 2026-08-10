/*****************************************************************************
* | File      	:   FT3168.c
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
#include "FT3168.h"
#include "DEV_Config.h"

FT3168_Struct FT3168;

/******************************************************************************
function :	Send one byte of data to the specified register of FT3168
parameter:
******************************************************************************/
static void FT3168_I2C_Write_Byte(uint8_t reg, uint8_t value) {
    DEV_I2C_Write_Byte(FT3168_I2C_ADDR, reg, value);
}

/******************************************************************************
function :	Read one byte of data from the specified register of FT3168
parameter:
******************************************************************************/
static uint8_t FT3168_I2C_Read_Byte(uint8_t reg) {
    uint8_t value;
    value = DEV_I2C_Read_Byte(FT3168_I2C_ADDR,reg);
    return value;
}

/******************************************************************************
function :	Read n byte of data from the specified register of FT3168
parameter:
******************************************************************************/
static void FT3168_I2C_Read_nByte(uint8_t reg, uint8_t *pData, uint32_t Len) {
    DEV_I2C_Read_nByte(FT3168_I2C_ADDR,reg,pData,Len);
}

/******************************************************************************
function :	Reset the FT3168
parameter:
******************************************************************************/
void FT3168_Reset() {
    gpio_put(Touch_RST_PIN, 1);
    sleep_ms(20);
    gpio_put(Touch_RST_PIN, 0);
    sleep_ms(20);
    gpio_put(Touch_RST_PIN, 1);
    sleep_ms(50);
}

/******************************************************************************
function :	Initialize the FT3168
parameter:
        mode    ：  FT3168_Point_Mode
                    FT3168_Gesture_Mode
******************************************************************************/
void FT3168_Init(uint8_t mode) {
    // FT3168 Reset
    FT3168_Reset();

    // FT3168 Init
    FT3168_I2C_Write_Byte(REG_POWER_MODE,0X01);
    if(mode != FT3168_Point_Mode){
        FT3168_I2C_Write_Byte(FT3168_RD_WR_DEVICE_GESTUREID_MODE,0X01);
        FT3168.mode = FT3168_Gesture_Mode;
    }
    else{
        FT3168.mode = FT3168_Point_Mode;
    }

    sleep_ms(20);

    int32_t id = FT3168_ReadID();
    printf("FT3168Register_WhoAmI = %d\n", id);
    if(id != 0x03) { // FT3168的ID应为0x03
        printf("Invalid device ID: 0x%x\n", id);
        return;
    }
    printf("FT3168 initialized successfully\n");
}

/******************************************************************************
function :	Read the ID of FT3168
parameter:
******************************************************************************/
uint16_t FT3168_ReadID() {
    uint8_t id;
    id = FT3168_I2C_Read_Byte(FT3168_RD_DEVICE_ID);
    return id;
}

/******************************************************************************
function :	Read the current status of FT3168
parameter:
******************************************************************************/
uint16_t FT3168_ReadState(Value_Information info) {
    uint8_t buf[2];
    
    switch(info) {
        case FT3168_GESTURE_ID:
            buf[0] = FT3168_I2C_Read_Byte(REG_GESTURE_ID);
            return buf[0];

        case FT3168_FINGER_NUMBER:
            buf[0] = FT3168_I2C_Read_Byte(REG_FINGER_NUM);
            return buf[0];

        case FT3168_COORDINATE_X:
            FT3168_I2C_Read_nByte(REG_X1_H, buf, 2);
            return ((int16_t)(buf[0] & 0x0F) << 8) | buf[1];
            
        case FT3168_COORDINATE_Y:
            FT3168_I2C_Read_nByte(REG_Y1_H, buf, 2);
            return ((int16_t)(buf[0] & 0x0F) << 8) | buf[1];
    }
    return -1;
}

/******************************************************************************
function :	Get the coordinate value of FT3168 contact
parameter:
******************************************************************************/
void FT3168_Get_Point() {
    uint8_t fingers = (uint8_t)FT3168_ReadState(FT3168_FINGER_NUMBER);
    if(fingers != 0) 
    {
        FT3168.x_point = (int)FT3168_ReadState(FT3168_COORDINATE_X);
        FT3168.y_point = (int)FT3168_ReadState(FT3168_COORDINATE_Y);
    }
}

/******************************************************************************
function :	Get the coordinate value of FT3168 contact
parameter:
******************************************************************************/
uint8_t FT3168_Get_Gesture() {
    uint8_t gesture = FT3168_ReadState(FT3168_GESTURE_ID);
    return gesture;
}

