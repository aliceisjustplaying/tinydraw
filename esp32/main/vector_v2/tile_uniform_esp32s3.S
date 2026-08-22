// ESP32-S3 PIE uniform-color classifier for strided RGB565 rectangles. Each
// row scalar-aligns without crossing its end, compares the aligned 128-bit
// interior eight pixels at a time, and handles the short tail as halfwords.

    .section .iram1, "ax"
    .align  4
    .global tinydraw_tile_uniform_color_pie
    .type   tinydraw_tile_uniform_color_pie, @function

// a2 pixels, a3 width, a4 height, a5 source stride in pixels, a6 color
// returns a2 = 1 only if every pixel matches color
tinydraw_tile_uniform_color_pie:
    entry   a1, 32
    slli    a5, a5, 1
    slli    a7, a6, 16
    or      a7, a7, a6
    ee.movi.32.q q0, a7, 0
    ee.movi.32.q q0, a7, 1
    ee.movi.32.q q0, a7, 2
    ee.movi.32.q q0, a7, 3

    // Immediate publications are aligned 64x64 windows of the producer's
    // stride-128 surface. Keep that dominant path free of per-row alignment,
    // prefix, vector-count, and tail setup.
    movi    a8, 64
    bne     a3, a8, .Luniform_generic_setup
    or      a8, a2, a5
    extui   a8, a8, 0, 4
    bnez    a8, .Luniform_generic_setup
    movi.n  a15, 1
.Luniform_fast_row:
    mov     a8, a2
    movi    a10, 8
    j       .Luniform_vector_first

.Luniform_generic_setup:
    movi.n  a15, 0
.Luniform_row:
    mov     a8, a2
    mov     a9, a3

    // Consume at most seven halfwords to reach a 16-byte boundary. Width can
    // be shorter than the alignment prefix, so clamp before every load.
    neg     a10, a8
    extui   a10, a10, 0, 4
    srli    a10, a10, 1
    min     a10, a10, a9
    sub     a9, a9, a10
    beqz    a10, .Luniform_vectors
.Luniform_prefix:
    l16ui   a11, a8, 0
    bne     a11, a6, .Luniform_false
    addi    a8, a8, 2
    addi    a10, a10, -1
    bnez    a10, .Luniform_prefix

.Luniform_vectors:
    srli    a10, a9, 3
    beqz    a10, .Luniform_tail_setup

    // The first comparison seeds q3; later vectors fold into it. A single
    // 16-byte stack spill then reduces the row result in scalar registers.
.Luniform_vector_first:
    ee.vld.128.ip  q1, a8, 16
    ee.vcmp.eq.s16 q3, q1, q0
    addi    a10, a10, -1
    loopnez a10, .Luniform_reduce
.Luniform_vector:
    ee.vld.128.ip  q1, a8, 16
    ee.vcmp.eq.s16 q2, q1, q0
    ee.andq         q3, q3, q2
.Luniform_reduce:
    mov     a10, a1
    ee.vst.128.ip q3, a10, 16
    l32i    a10, a1, 0
    l32i    a11, a1, 4
    and     a10, a10, a11
    l32i    a11, a1, 8
    and     a10, a10, a11
    l32i    a11, a1, 12
    and     a10, a10, a11
    bnei    a10, -1, .Luniform_false
    bnez    a15, .Luniform_fast_row_done

.Luniform_tail_setup:
    extui   a10, a9, 0, 3
    beqz    a10, .Luniform_row_done
.Luniform_tail:
    l16ui   a11, a8, 0
    bne     a11, a6, .Luniform_false
    addi    a8, a8, 2
    addi    a10, a10, -1
    bnez    a10, .Luniform_tail

.Luniform_row_done:
    add     a2, a2, a5
    addi    a4, a4, -1
    bnez    a4, .Luniform_row
    movi.n  a2, 1
    retw.n

.Luniform_fast_row_done:
    add     a2, a2, a5
    addi    a4, a4, -1
    bnez    a4, .Luniform_fast_row
    movi.n  a2, 1
    retw.n

.Luniform_false:
    movi.n  a2, 0
    retw.n

    .size tinydraw_tile_uniform_color_pie, .-tinydraw_tile_uniform_color_pie
