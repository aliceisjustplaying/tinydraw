#pragma once

#include "tusb_option.h"

#define CFG_TUSB_OS OPT_OS_FREERTOS
#define CFG_TUSB_DEBUG 0
#define CFG_TUD_ENABLED 1
#define CFG_TUD_MAX_SPEED OPT_MODE_FULL_SPEED
#define CFG_TUD_DWC2_SLAVE_ENABLE 1
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#define CFG_TUD_ENDPOINT0_SIZE 64

#define CFG_TUD_CDC 0
#define CFG_TUD_MSC 1
#define CFG_TUD_HID 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0
#define CFG_TUD_ECM_RNDIS 0
#define CFG_TUD_NCM 0
#define CFG_TUD_DFU 0
#define CFG_TUD_DFU_RUNTIME 0
#define CFG_TUD_BTH 0
#define CFG_TUD_MSC_EP_BUFSIZE 512
