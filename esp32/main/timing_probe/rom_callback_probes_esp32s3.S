// Exact windowed-ABI caller peers for state-safe ROM callback calibration.
// Each target and baseline wrapper has identical argument setup and one
// literal-backed callx8; only the call destination differs. ProbeContext
// supplies the live buffer and same-value CPU-tick argument at fixed offsets.

    .section .iram1, "ax"

    .balign 32
    .global tinydraw_rom_noop_u32
    .type tinydraw_rom_noop_u32, @function
tinydraw_rom_noop_u32:
    .begin no-transform
    entry   a1, 16
    retw.n
    .end no-transform
    .size tinydraw_rom_noop_u32, .-tinydraw_rom_noop_u32

    .balign 32
    .global tinydraw_rom_noop_memset
    .type tinydraw_rom_noop_memset, @function
tinydraw_rom_noop_memset:
    .begin no-transform
    entry   a1, 16
    retw.n
    .end no-transform
    .size tinydraw_rom_noop_memset, .-tinydraw_rom_noop_memset

    .balign 32
    .global tinydraw_rom_noop_void_u32
    .type tinydraw_rom_noop_void_u32, @function
tinydraw_rom_noop_void_u32:
    .begin no-transform
    entry   a1, 16
    retw.n
    .end no-transform
    .size tinydraw_rom_noop_void_u32, .-tinydraw_rom_noop_void_u32

    .macro DEFINE_XTOS_INTLEVEL_WRAPPER name, target
    .literal .Lxtos_intlevel_restore_ps_\@, 0x00040c00
    .literal .Lxtos_intlevel_target_\@, \target
    .balign 32
    .global \name
    .type \name, @function
\name:
    .begin no-transform
    entry   a1, 32
    rsr.ps  a9
    s32i    a9, a1, 16
    l32r    a10, .Lxtos_intlevel_restore_ps_\@
    l32r    a8, .Lxtos_intlevel_target_\@
    callx8  a8
    l32i    a9, a1, 16
    wsr.ps  a9
    rsync
    mov.n   a2, a10
    retw.n
    .end no-transform
    .size \name, .-\name
    .endm

    .macro DEFINE_RESET_REASON_WRAPPER name, target, core
    .literal .Lrom_reset_target_\@, \target
    .balign 32
    .global \name
    .type \name, @function
\name:
    .begin no-transform
    entry   a1, 16
    movi.n  a10, \core
    l32r    a8, .Lrom_reset_target_\@
    callx8  a8
    mov.n   a2, a10
    retw.n
    .end no-transform
    .size \name, .-\name
    .endm

    .macro DEFINE_MEMSET_WRAPPER name, target, length_words
    .literal .Lrom_memset_target_\@, \target
    .balign 32
    .global \name
    .type \name, @function
\name:
    .begin no-transform
    entry   a1, 16
    l32i    a10, a2, 104
    movi.n  a11, 0
    movi    a12, \length_words
    slli    a12, a12, 4
    l32r    a8, .Lrom_memset_target_\@
    callx8  a8
    mov.n   a2, a10
    retw.n
    .end no-transform
    .size \name, .-\name
    .endm

    .macro DEFINE_CPU_TICKS_WRAPPER name, target
    .literal .Lrom_cpu_ticks_target_\@, \target
    .balign 32
    .global \name
    .type \name, @function
\name:
    .begin no-transform
    entry   a1, 16
    l32i    a10, a2, 108
    l32r    a8, .Lrom_cpu_ticks_target_\@
    callx8  a8
    movi.n  a2, 0
    retw.n
    .end no-transform
    .size \name, .-\name
    .endm

DEFINE_RESET_REASON_WRAPPER tinydraw_rom_baseline_reset_reason_core0, tinydraw_rom_noop_u32, 0
DEFINE_RESET_REASON_WRAPPER tinydraw_rom_reset_reason_core0, esp_rom_get_reset_reason, 0
DEFINE_RESET_REASON_WRAPPER tinydraw_rom_baseline_reset_reason_core1, tinydraw_rom_noop_u32, 1
DEFINE_RESET_REASON_WRAPPER tinydraw_rom_reset_reason_core1, esp_rom_get_reset_reason, 1

DEFINE_MEMSET_WRAPPER tinydraw_rom_baseline_memset_zero, tinydraw_rom_noop_memset, 0
DEFINE_MEMSET_WRAPPER tinydraw_rom_memset_zero, memset, 0
DEFINE_MEMSET_WRAPPER tinydraw_rom_baseline_memset_52e0, tinydraw_rom_noop_memset, 0x52e
DEFINE_MEMSET_WRAPPER tinydraw_rom_memset_52e0, memset, 0x52e

DEFINE_CPU_TICKS_WRAPPER tinydraw_rom_baseline_set_cpu_ticks, tinydraw_rom_noop_void_u32
DEFINE_CPU_TICKS_WRAPPER tinydraw_rom_set_cpu_ticks, esp_rom_set_cpu_ticks_per_us

DEFINE_XTOS_INTLEVEL_WRAPPER tinydraw_rom_baseline_xtos_set_intlevel_restore_ps_40c00, tinydraw_rom_noop_u32
DEFINE_XTOS_INTLEVEL_WRAPPER tinydraw_rom_xtos_set_intlevel_restore_ps_40c00, _xtos_set_intlevel
