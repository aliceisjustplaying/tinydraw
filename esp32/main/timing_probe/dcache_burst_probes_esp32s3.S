// Matched ESP32-S3 data-cache burst probes. Each body issues one 32-bit load
// from the first word of each consecutive 64-byte line. Hot and cold cells use
// the same function; the C++ prepare seam controls only cache residency.

    .section .iram1, "ax"

    .macro DEFINE_DCACHE_BURST_PROBE name, context_offset, lines
    .balign 32
    .global \name
    .type   \name, @function
\name:
    entry   a1, 16
    l32i    a8, a2, \context_offset
    l32i    a4, a8, 0
    .if \lines >= 2
    l32i    a4, a8, 64
    .endif
    .if \lines >= 4
    l32i    a4, a8, 128
    l32i    a4, a8, 192
    .endif
    .if \lines >= 8
    l32i    a4, a8, 256
    l32i    a4, a8, 320
    l32i    a4, a8, 384
    l32i    a4, a8, 448
    .endif
    .if \lines >= 16
    l32i    a4, a8, 512
    l32i    a4, a8, 576
    l32i    a4, a8, 640
    l32i    a4, a8, 704
    l32i    a4, a8, 768
    l32i    a4, a8, 832
    l32i    a4, a8, 896
    l32i    a4, a8, 960
    .endif
    mov     a2, a4
    retw.n
    .size \name, .-\name
    .endm

// ProbeContext::psram is at byte offset 12; ProbeContext::flash is at 20.
// ProbeContext::sram_stream is at byte offset 8. Its sixteen-load peer uses
// the identical macro body as the hot PSRAM and flash measurements.
DEFINE_DCACHE_BURST_PROBE tinydraw_dcache_sram_16_lines, 8, 16

DEFINE_DCACHE_BURST_PROBE tinydraw_dcache_psram_1_lines, 12, 1
DEFINE_DCACHE_BURST_PROBE tinydraw_dcache_psram_2_lines, 12, 2
DEFINE_DCACHE_BURST_PROBE tinydraw_dcache_psram_4_lines, 12, 4
DEFINE_DCACHE_BURST_PROBE tinydraw_dcache_psram_8_lines, 12, 8
DEFINE_DCACHE_BURST_PROBE tinydraw_dcache_psram_16_lines, 12, 16

DEFINE_DCACHE_BURST_PROBE tinydraw_dcache_flash_1_lines, 20, 1
DEFINE_DCACHE_BURST_PROBE tinydraw_dcache_flash_2_lines, 20, 2
DEFINE_DCACHE_BURST_PROBE tinydraw_dcache_flash_4_lines, 20, 4
DEFINE_DCACHE_BURST_PROBE tinydraw_dcache_flash_8_lines, 20, 8
DEFINE_DCACHE_BURST_PROBE tinydraw_dcache_flash_16_lines, 20, 16
