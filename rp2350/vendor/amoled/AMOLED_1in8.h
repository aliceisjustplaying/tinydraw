/*****************************************************************************
* | File      	:   AMOLED_1in8.h
* | Author      :   Waveshare Team
* | Function    :   AMOLED Interface Functions
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
#ifndef _AMOLED_1IN8_H_
#define _AMOLED_1IN8_H_

#include "qspi_pio.h"

#define AMOLED_1IN8_WIDTH 368
#define AMOLED_1IN8_HEIGHT 448

#define HORIZONTAL 0
#define VERTICAL   1

typedef struct{
    UWORD WIDTH;
    UWORD HEIGHT;
    UBYTE SCAN_DIR;
}AMOLED_1IN8_ATTRIBUTES;
extern AMOLED_1IN8_ATTRIBUTES AMOLED_1IN8;

void AMOLED_1IN8_Init();
void AMOLED_1IN8_SetBrightness(uint8_t brightness);
void AMOLED_1IN8_SetWindows(uint32_t Xstart, uint32_t Ystart, uint32_t Xend, uint32_t Yend);
void AMOLED_1IN8_Display(UWORD *Image);
void AMOLED_1IN8_DisplayWindows(uint32_t Xstart, uint32_t Ystart, uint32_t Xend, uint32_t Yend, UWORD *Image);
void AMOLED_1IN8_Clear(UWORD Color);

#endif // !_AMOLED_1IN8_H_
