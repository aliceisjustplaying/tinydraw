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

// Matched dependent load-use probes. Every iteration computes an address from
// the preceding load, performs one aligned load, and masks that result into the
// next 4,096-word index. The three instantiations differ only in the context
// pointer field selected before the loop.
    .macro DEFINE_DEPENDENT_LOAD_PROBE name, context_offset
    .balign 32
    .global \name
    .type   \name, @function
\name:
    entry   a1, 16
    l32i    a8, a2, \context_offset
    extui   a2, a3, 0, 12
    movi    a9, 1
    slli    a9, a9, 12
    loop    a9, .Ldependent_done\@
.Ldependent_loop\@:
    addx4   a10, a2, a8
    l32i    a2, a10, 0
    extui   a2, a2, 0, 12
.Ldependent_done\@:
    retw.n
    .size \name, .-\name
    .endm

// ProbeContext::flash is at 20; the appended SRAM and PSRAM peers are 24/28.
DEFINE_DEPENDENT_LOAD_PROBE tinydraw_dependent_load_flash, 20
DEFINE_DEPENDENT_LOAD_PROBE tinydraw_dependent_load_sram, 24
DEFINE_DEPENDENT_LOAD_PROBE tinydraw_dependent_load_psram, 28

// Repeated conditional-branch probes. Every loop iteration executes exactly
// four body instructions: the probe instruction, one increment, one
// unconditional join, and one common no-op add. Taken and not-taken kernels
// have byte-identical loop bodies and differ only in the condition initialized
// before the loop. The baseline follows the not-taken route and replaces beqz
// with one three-byte OR instruction.
    .macro DEFINE_CONDITIONAL_BRANCH_PROBE name, condition, baseline
    .balign 32
    .global \name
    .type   \name, @function
\name:
    .begin no-transform
    entry   a1, 16
    movi    a2, \condition
    movi.n  a3, 0
    movi    a9, 1
    slli    a9, a9, 12
    loop    a9, .Lbranch_done\@
.Lbranch_loop\@:
    .if \baseline
    or      a4, a2, a2
    .else
    beqz    a2, .Lbranch_taken\@
    .endif
.Lbranch_not_taken\@:
    addi.n  a3, a3, 1
    j       .Lbranch_join\@
.Lbranch_taken\@:
    addi.n  a3, a3, 1
    j       .Lbranch_join\@
.Lbranch_join\@:
    addi    a3, a3, 0
.Lbranch_done\@:
    mov.n   a2, a3
    retw.n
    .end no-transform
    .size \name, .-\name
    .endm

DEFINE_CONDITIONAL_BRANCH_PROBE tinydraw_branch_baseline, 1, 1
DEFINE_CONDITIONAL_BRANCH_PROBE tinydraw_branch_not_taken, 1, 0
DEFINE_CONDITIONAL_BRANCH_PROBE tinydraw_branch_taken, 0, 0

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
