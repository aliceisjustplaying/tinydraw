// Bounded internal-SRAM microprobes with instruction bodies fixed in assembly.
// The matched issue/load loops each execute nine body instructions 1,024 times.
// The dependent loop has one address instruction and one l32i per load. The
// store loop places its only memw after all stores so CCOUNT includes completion.

    .section .iram1, "ax"

    .balign 32
    .global tinydraw_sram_instruction_issue
    .type   tinydraw_sram_instruction_issue, @function

// a2: ProbeContext (unused)
// a3: seed, returned unchanged
tinydraw_sram_instruction_issue:
    entry   a1, 16
    movi    a8, 1024
    movi    a14, 0
    loop    a8, .Lissue_done
.Lissue_loop:
    addi    a4, a4, 0
    addi    a5, a5, 0
    addi    a6, a6, 0
    addi    a7, a7, 0
    addi    a9, a9, 0
    addi    a10, a10, 0
    addi    a11, a11, 0
    addi    a12, a12, 0
    addi    a14, a14, 32
.Lissue_done:
    mov     a2, a3
    retw.n

    .size tinydraw_sram_instruction_issue, .-tinydraw_sram_instruction_issue

    .balign 32
    .global tinydraw_sram_l32_dependent
    .type   tinydraw_sram_l32_dependent, @function

// a2: ProbeContext, whose first field is the 4,096-entry pointer chain
// a3: seed
tinydraw_sram_l32_dependent:
    entry   a1, 16
    l32i    a8, a2, 0
    extui   a2, a3, 0, 12
    movi    a9, 1
    slli    a9, a9, 12
    loop    a9, .Ldependent_done
.Ldependent_loop:
    addx4  a10, a2, a8
    l32i   a2, a10, 0
.Ldependent_done:
    retw.n

    .size tinydraw_sram_l32_dependent, .-tinydraw_sram_l32_dependent

    .balign 32
    .global tinydraw_sram_l32_independent
    .type   tinydraw_sram_l32_independent, @function

// a2: ProbeContext, whose third field is the 32 KiB stream buffer
// a3: seed (unused)
tinydraw_sram_l32_independent:
    entry   a1, 16
    l32i    a8, a2, 8
    movi    a14, 1024
    loop    a14, .Lindependent_done
.Lindependent_loop:
    l32i    a4, a8, 0
    l32i    a5, a8, 4
    l32i    a6, a8, 8
    l32i    a7, a8, 12
    l32i    a9, a8, 16
    l32i    a10, a8, 20
    l32i    a11, a8, 24
    l32i    a12, a8, 28
    addi    a8, a8, 32
.Lindependent_done:
    xor     a2, a4, a5
    xor     a2, a2, a6
    xor     a2, a2, a7
    xor     a2, a2, a9
    xor     a2, a2, a10
    xor     a2, a2, a11
    xor     a2, a2, a12
    retw.n

    .size tinydraw_sram_l32_independent, .-tinydraw_sram_l32_independent

    .balign 32
    .global tinydraw_sram_s32_store_complete
    .type   tinydraw_sram_s32_store_complete, @function

// a2: ProbeContext, whose third field is the 32 KiB stream buffer
// a3: value written to every word and returned
tinydraw_sram_s32_store_complete:
    entry   a1, 16
    l32i    a8, a2, 8
    movi    a14, 1024
    loop    a14, .Lstore_done
.Lstore_loop:
    s32i    a3, a8, 0
    s32i    a3, a8, 4
    s32i    a3, a8, 8
    s32i    a3, a8, 12
    s32i    a3, a8, 16
    s32i    a3, a8, 20
    s32i    a3, a8, 24
    s32i    a3, a8, 28
    addi    a8, a8, 32
.Lstore_done:
    memw
    mov     a2, a3
    retw.n

    .size tinydraw_sram_s32_store_complete, .-tinydraw_sram_s32_store_complete
