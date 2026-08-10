/*****************************************************************************
* | File      	:   AMOLED_1in8.c
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
#include "DEV_Config.h"
#include "AMOLED_1in8.h"

AMOLED_1IN8_ATTRIBUTES AMOLED_1IN8;

/********************************************************************************
function:	Sets the start position and size of the display area
parameter:
        qspi    ：  qspi structure
		Xstart 	:   X direction Start coordinates
		Ystart  :   Y direction Start coordinates
		Xend    :   X direction end coordinates
		Yend    :   Y direction end coordinates
********************************************************************************/
void AMOLED_1IN8_SetWindows(uint32_t Xstart, uint32_t Ystart, uint32_t Xend, uint32_t Yend){
    QSPI_Select(qspi); 
    QSPI_REGISTER_Write(qspi, 0x2a); 
    QSPI_DATA_Write(qspi, Xstart>>8);
    QSPI_DATA_Write(qspi, Xstart&0xff);
    QSPI_DATA_Write(qspi, (Xend-1)>>8);
    QSPI_DATA_Write(qspi, (Xend-1)&0xff);
    QSPI_Deselect(qspi); 
    
    QSPI_Select(qspi); 
    QSPI_REGISTER_Write(qspi, 0x2b);
    QSPI_DATA_Write(qspi, Ystart>>8);
    QSPI_DATA_Write(qspi, Ystart&0xff);
    QSPI_DATA_Write(qspi, (Yend-1)>>8);
    QSPI_DATA_Write(qspi, (Yend-1)&0xff);
    QSPI_Deselect(qspi); 
    
    QSPI_Select(qspi); 
    QSPI_REGISTER_Write(qspi, 0x2c);
    QSPI_Deselect(qspi); 
    WAIT_TIME();
}

/******************************************************************************
function :	Initialize the lcd register
parameter:
        qspi    ：  qspi structure
******************************************************************************/
static void AMOLED_1IN8_InitReg(){
    QSPI_Select(qspi); 
    QSPI_REGISTER_Write(qspi, 0x11);
    sleep_ms(120);
    QSPI_Deselect(qspi);

    QSPI_Select(qspi);
    QSPI_REGISTER_Write(qspi, 0x44);
    QSPI_DATA_Write(qspi, 0x01);
    QSPI_DATA_Write(qspi, 0xC5); 
    QSPI_Deselect(qspi);
    
    QSPI_Select(qspi);
    QSPI_REGISTER_Write(qspi, 0x35);
    QSPI_DATA_Write(qspi, 0x00);
    QSPI_Deselect(qspi);

    QSPI_Select(qspi);
    QSPI_REGISTER_Write(qspi, 0x3A);
    QSPI_DATA_Write(qspi, 0x55);  
    QSPI_Deselect(qspi);
    
    QSPI_Select(qspi);
    QSPI_REGISTER_Write(qspi, 0xC4); 
    QSPI_DATA_Write(qspi, 0x80); 
    QSPI_Deselect(qspi);

    QSPI_Select(qspi);
    QSPI_REGISTER_Write(qspi, 0x53); 
    QSPI_DATA_Write(qspi, 0x20);
    QSPI_Deselect(qspi);

    QSPI_Select(qspi);
    QSPI_REGISTER_Write(qspi, 0x51);   
    QSPI_DATA_Write(qspi, 0xFF);
    QSPI_Deselect(qspi);

    QSPI_Select(qspi);
    QSPI_REGISTER_Write(qspi, 0x29);  
    QSPI_Deselect(qspi);
    
    sleep_ms(10);
}

/********************************************************************************
function :	Reset the lcd
parameter:
        qspi    ：  qspi structure
********************************************************************************/
static void AMOLED_1IN8_Reset(){
    gpio_put(qspi.pin_rst,1);
    DEV_Delay_ms(50);
    gpio_put(qspi.pin_rst,0);
    DEV_Delay_ms(50);
    gpio_put(qspi.pin_rst,1);
    DEV_Delay_ms(300);
}

/********************************************************************************
function :	Initialize the lcd
parameter:
        qspi    ：  qspi structure
********************************************************************************/
void AMOLED_1IN8_Init()
{
    //Hardware reset
    AMOLED_1IN8_Reset();
    
    //Set the initialization register
    AMOLED_1IN8_InitReg();

    AMOLED_1IN8.HEIGHT	= AMOLED_1IN8_HEIGHT;
    AMOLED_1IN8.WIDTH   = AMOLED_1IN8_WIDTH;
}

/******************************************************************************
function :	Set AMOLED Brightness
parameter:
******************************************************************************/
void AMOLED_1IN8_SetBrightness(uint8_t brightness){
    if(brightness > 100) brightness = 100;
    brightness = brightness * 255 / 100;

    // QSPI_1Wrie_Mode(&qspi);
    QSPI_Select(qspi); 
    QSPI_REGISTER_Write(qspi, 0x51);
    QSPI_DATA_Write(qspi, brightness);
    QSPI_Deselect(qspi);
}


/******************************************************************************
function :	Clear screen
parameter:
******************************************************************************/
void AMOLED_1IN8_Clear(UWORD Color) {
    // Color data
    UWORD i;
	UWORD image[AMOLED_1IN8.HEIGHT];
	for(i=0;i<AMOLED_1IN8.HEIGHT;i++){
		image[i] = Color>>8 | (Color&0xff)<<8;
	}
	UBYTE *partial_image = (UBYTE *)(image);

    // Send command in one-line mode
    // QSPI_1Wrie_Mode(&qspi);
    AMOLED_1IN8_SetWindows(0,0,AMOLED_1IN8.WIDTH,AMOLED_1IN8.HEIGHT);
    QSPI_Select(qspi);
    QSPI_Pixel_Write(qspi,0x2c);

    // Four-wire mode sends RGB data
    // QSPI_4Wrie_Mode(&qspi);
    channel_config_set_dreq(&c, pio_get_dreq(qspi.pio, qspi.sm, true));
    for (int i = 0; i < AMOLED_1IN8.HEIGHT; i++) {
        dma_channel_configure(dma_tx, 
                            &c,
                            &qspi.pio->txf[qspi.sm],  // Destination pointer (PIO TX FIFO)
                            partial_image,            // Source pointer (data buffer)
                            AMOLED_1IN8.WIDTH*2,      // Data length (unit: number of transmissions)
                            true);                    // Start transferring immediately
        
        // Waiting for DMA transfer to complete
        while(dma_channel_is_busy(dma_tx));
    }

    QSPI_Deselect(qspi);
}


/******************************************************************************
function :	Send data to AMOLED to complete full screen refresh
parameter:
        Image   ：  Image data
******************************************************************************/
void AMOLED_1IN8_Display(UWORD *Image)
{
    // Send command in one-line mode
    // QSPI_1Wrie_Mode(&qspi);
    AMOLED_1IN8_SetWindows(0,0,AMOLED_1IN8.WIDTH,AMOLED_1IN8.HEIGHT);
    QSPI_Select(qspi);
    QSPI_Pixel_Write(qspi,0x2c);

    // Four-wire mode sends RGB data
    // QSPI_4Wrie_Mode(&qspi);
    channel_config_set_dreq(&c, pio_get_dreq(qspi.pio, qspi.sm, true));
    dma_channel_configure(dma_tx, 
                        &c,
                        &qspi.pio->txf[qspi.sm],  // Destination pointer (PIO TX FIFO)
                        (UBYTE *)Image,           // Source pointer (data buffer)
                        AMOLED_1IN8.WIDTH*AMOLED_1IN8.HEIGHT*2,   // Data length (unit: number of transmissions)
                        true);                    // Start transferring immediately
    
    // Waiting for DMA transfer to complete
    while(dma_channel_is_busy(dma_tx));
    // DMA completion only means the final bytes reached the PIO FIFO.
    // Let the state machine shift them onto the panel before raising CS.
    busy_wait_us_32(1);
    QSPI_Deselect(qspi);             
}

/******************************************************************************
function : Send tightly packed data to one AMOLED window
******************************************************************************/
void AMOLED_1IN8_DisplayWindowPacked(uint32_t Xstart, uint32_t Ystart, uint32_t Xend,
                                     uint32_t Yend, UWORD *Image) {
    if(Xend > AMOLED_1IN8.WIDTH) Xend = AMOLED_1IN8.WIDTH;
    if(Yend > AMOLED_1IN8.HEIGHT) Yend = AMOLED_1IN8.HEIGHT;
    if(Xstart >= Xend || Ystart >= Yend) return;

    AMOLED_1IN8_SetWindows(Xstart, Ystart, Xend, Yend);
    QSPI_Select(qspi);
    QSPI_Pixel_Write(qspi, 0x2c);
    channel_config_set_dreq(&c, pio_get_dreq(qspi.pio, qspi.sm, true));
    dma_channel_configure(dma_tx,
                          &c,
                          &qspi.pio->txf[qspi.sm],
                          (UBYTE *)Image,
                          (Xend - Xstart) * (Yend - Ystart) * 2,
                          true);
    while(dma_channel_is_busy(dma_tx));
    QSPI_Deselect(qspi);
}

/******************************************************************************
function :	Send data to AMOLED to complete partial refresh
parameter:
		Xstart 	:   X direction Start coordinates
		Ystart  :   Y direction Start coordinates
		Xend    :   X direction end coordinates
		Yend    :   Y direction end coordinates
        Image   ：  Image data
******************************************************************************/
void AMOLED_1IN8_DisplayWindows(uint32_t Xstart, uint32_t Ystart, uint32_t Xend, uint32_t Yend, UWORD *Image) {
    (void)Xstart;
    (void)Ystart;
    (void)Xend;
    (void)Yend;
    AMOLED_1IN8_Display(Image);
}
