// Matched windowed-ABI callers for one state-preserving ROM REGI2C write.
// Both peers materialize the documented BOD block, host, threshold register,
// and boot-captured same value before one literal-backed callx8.

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
    movi    a10, 0x61
    movi.n  a11, 1
    movi.n  a12, 5
    l32i    a13, a2, 120
    l32r    a8, .Lrom_i2c_write_target_\@
    callx8  a8
    movi.n  a2, 0
    retw.n
    .end no-transform
    .size \name, .-\name
    .endm

DEFINE_ROM_I2C_WRITE_WRAPPER tinydraw_rom_i2c_baseline_write_same_bod_threshold, tinydraw_rom_i2c_noop_write
DEFINE_ROM_I2C_WRITE_WRAPPER tinydraw_rom_i2c_write_same_bod_threshold, rom_i2c_writeReg
