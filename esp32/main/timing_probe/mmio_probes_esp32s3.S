// Matched aligned 32-bit SRAM and peripheral-register probes. Every read
// iteration executes one dependent l32i/xor pair; every write iteration
// executes one s32i. Register addresses enter through ProbeContext so all
// peers have identical setup and loop encodings. Both write peers place one
// completion fence after all stores so CCOUNT includes their completion.

    .section .iram1, "ax"

    .macro DEFINE_MMIO_READ_PROBE name, context_offset
    .balign 32
    .global \name
    .type   \name, @function
\name:
    .begin no-transform
    entry   a1, 16
    l32i    a8, a2, \context_offset
    movi.n  a2, 0
    movi    a9, 1
    slli    a9, a9, 12
    loop    a9, .Lmmio_read_done\@
.Lmmio_read_loop\@:
    l32i    a4, a8, 0
    xor     a2, a2, a4
.Lmmio_read_done\@:
    retw.n
    .end no-transform
    .size \name, .-\name
    .endm

    .macro DEFINE_MMIO_WRITE_PROBE name, context_offset
    .balign 32
    .global \name
    .type   \name, @function
\name:
    .begin no-transform
    entry   a1, 16
    l32i    a8, a2, \context_offset
    movi.n  a4, 3
    movi    a9, 1
    slli    a9, a9, 12
    loop    a9, .Lmmio_write_done\@
.Lmmio_write_loop\@:
    s32i    a4, a8, 0
.Lmmio_write_done\@:
    memw
    movi.n  a2, 0
    retw.n
    .end no-transform
    .size \name, .-\name
    .endm

    .macro DEFINE_MMIO_SAME_VALUE_WRITE_PROBE name, pointer_offset, value_offset
    .balign 32
    .global \name
    .type   \name, @function
\name:
    .begin no-transform
    entry   a1, 16
    l32i    a8, a2, \pointer_offset
    l32i    a4, a2, \value_offset
    movi    a9, 1
    slli    a9, a9, 12
    nop.n
    loop    a9, .Lmmio_same_value_write_done\@
.Lmmio_same_value_write_loop\@:
    s32i    a4, a8, 0
.Lmmio_same_value_write_done\@:
    memw
    movi.n  a2, 0
    retw.n
    .end no-transform
    .size \name, .-\name
    .endm

// ProbeContext register-peer pointers begin at byte offset 32.
DEFINE_MMIO_READ_PROBE tinydraw_mmio_read_sram, 32
DEFINE_MMIO_READ_PROBE tinydraw_mmio_read_system_cpu_per_conf, 36
DEFINE_MMIO_READ_PROBE tinydraw_mmio_read_rtc_store1, 40
DEFINE_MMIO_READ_PROBE tinydraw_mmio_read_extmem_cache_state, 44
DEFINE_MMIO_READ_PROBE tinydraw_mmio_read_system_sysclk_conf, 52
DEFINE_MMIO_READ_PROBE tinydraw_mmio_read_extmem_dcache_ctrl1, 56
DEFINE_MMIO_READ_PROBE tinydraw_mmio_read_extmem_dcache_autoload_ctrl, 60
DEFINE_MMIO_READ_PROBE tinydraw_mmio_read_extmem_icache_ctrl1, 64
DEFINE_MMIO_READ_PROBE tinydraw_mmio_read_extmem_icache_autoload_ctrl, 68

DEFINE_MMIO_WRITE_PROBE tinydraw_mmio_write_sram, 32
DEFINE_MMIO_WRITE_PROBE tinydraw_mmio_write_extmem_cache_counter_clear, 48

// Same-value write peers load both the address and boot-captured value through
// ProbeContext. The SRAM peer therefore has the same dynamic instruction shape.
DEFINE_MMIO_SAME_VALUE_WRITE_PROBE tinydraw_mmio_write_same_value_sram, 32, 72
DEFINE_MMIO_SAME_VALUE_WRITE_PROBE tinydraw_mmio_write_same_value_system_sysclk_conf, 52, 76
DEFINE_MMIO_SAME_VALUE_WRITE_PROBE tinydraw_mmio_write_same_value_extmem_dcache_ctrl1, 56, 80
DEFINE_MMIO_SAME_VALUE_WRITE_PROBE tinydraw_mmio_write_same_value_extmem_dcache_autoload_ctrl, 60, 84
DEFINE_MMIO_SAME_VALUE_WRITE_PROBE tinydraw_mmio_write_same_value_extmem_icache_ctrl1, 64, 88
DEFINE_MMIO_SAME_VALUE_WRITE_PROBE tinydraw_mmio_write_same_value_extmem_icache_autoload_ctrl, 68, 92
