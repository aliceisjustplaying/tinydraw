// Matched windowed-ABI callers for the exact full-boot ROM REGI2C write.
// The firmware refuses to enter this pair unless a read-only preflight proves
// that BBPLL block 0x66, host 1, register 4 already contains 0x6b.

    .section .iram1, "ax"

    .balign 32
    .global tinydraw_rom_i2c_noop_write
    .type tinydraw_rom_i2c_noop_write, @function
tinydraw_rom_i2c_noop_write:
    .begin no-transform
    entry   a1, 16
    retw.n
    .end no-transform
    .size tinydraw_rom_i2c_noop_write, .-tinydraw_rom_i2c_noop_write

    .macro DEFINE_ROM_I2C_WRITE_WRAPPER name, target
    .literal .Lrom_i2c_write_target_\@, \target
    .balign 32
    .global \name
    .type \name, @function
\name:
    .begin no-transform
    entry   a1, 16
    movi    a10, 0x66
    movi.n  a11, 1
    movi.n  a12, 4
    movi    a13, 0x6b
    l32r    a8, .Lrom_i2c_write_target_\@
    callx8  a8
    movi.n  a2, 0
    retw.n
    .end no-transform
    .size \name, .-\name
    .endm

DEFINE_ROM_I2C_WRITE_WRAPPER tinydraw_rom_i2c_baseline_write_same_bbpll_mode, tinydraw_rom_i2c_noop_write
DEFINE_ROM_I2C_WRITE_WRAPPER tinydraw_rom_i2c_write_same_bbpll_mode, rom_i2c_writeReg
