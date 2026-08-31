// Exact flash instruction-cache burst ladder for ESP32-S3. The flash targets
// use call0 deliberately: omitting a windowed-ABI entry instruction lets each
// target occupy an exact whole number of 32-byte I-cache lines.

    .section .iram1, "ax"
    .balign 4
    .global tinydraw_measure_flash_instruction_burst_window
    .type   tinydraw_measure_flash_instruction_burst_window, @function

// Windowed-ABI entry:
//   a2: uint32_t endpoints_and_sentinel[3]
//   a3: address of a call0-only tinydraw_flash_instruction_burst target
//
// The target changes only a0 (call0 return address) and a2 (line-count
// sentinel). The endpoint pointer, outer return address, and start CCOUNT stay
// live in a6, a7, and a4. No setup or endpoint store is inside the interval.
tinydraw_measure_flash_instruction_burst_window:
    entry       a1, 16
    mov         a6, a2
    mov         a8, a3
    mov         a7, a0
    rsr.ccount  a4
    callx0      a8
    rsr.ccount  a5
    mov         a0, a7
    s32i        a4, a6, 0
    s32i        a5, a6, 4
    s32i        a2, a6, 8
    retw.n
    .size tinydraw_measure_flash_instruction_burst_window, .-tinydraw_measure_flash_instruction_burst_window

// The normal ESP-IDF linker mapping collects .text.* input sections into
// .flash.text. A 32-byte input-section alignment is retained in the output.
    .section .text.tinydraw_flash_instruction_bursts, "ax"
    .balign 32

    .macro DEFINE_FLASH_INSTRUCTION_BURST function, start, end, lines
    .balign 32
    .global \function
    .global \start
    .global \end
    .type   \function, @function
\function:
\start:
    // Every preceding line executes sixteen two-byte narrow NOPs.
    .rept ((\lines - 1) * 16)
    nop.n
    .endr

    // The final line executes through byte 15. Eight trailing NOPs provide
    // sixteen safe same-line prefetch bytes after ret.n without fetching the
    // next target's first line.
    .rept 6
    nop.n
    .endr
    movi.n      a2, \lines
    ret.n
    .rept 8
    nop.n
    .endr
\end:
    .size \function, \end-\function
    .endm

// Exact IRAM peer for the eight-line flash target. Both targets execute 120
// instructions before returning: 112 leading NOPs, six final-line NOPs,
// one sentinel move, and one return. The eight trailing NOPs are prefetch
// padding and never execute.
    .section .iram1, "ax"
DEFINE_FLASH_INSTRUCTION_BURST tinydraw_iram_instruction_hit_8_lines, tinydraw_iram_instruction_hit_8_lines_start, tinydraw_iram_instruction_hit_8_lines_end, 8

    .section .text.tinydraw_flash_instruction_bursts, "ax"
    .balign 32
DEFINE_FLASH_INSTRUCTION_BURST tinydraw_flash_instruction_burst_1_lines, tinydraw_flash_instruction_burst_1_lines_start, tinydraw_flash_instruction_burst_1_lines_end, 1
DEFINE_FLASH_INSTRUCTION_BURST tinydraw_flash_instruction_burst_2_lines, tinydraw_flash_instruction_burst_2_lines_start, tinydraw_flash_instruction_burst_2_lines_end, 2
DEFINE_FLASH_INSTRUCTION_BURST tinydraw_flash_instruction_burst_4_lines, tinydraw_flash_instruction_burst_4_lines_start, tinydraw_flash_instruction_burst_4_lines_end, 4
DEFINE_FLASH_INSTRUCTION_BURST tinydraw_flash_instruction_burst_8_lines, tinydraw_flash_instruction_burst_8_lines_start, tinydraw_flash_instruction_burst_8_lines_end, 8
