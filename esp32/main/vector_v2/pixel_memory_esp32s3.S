// Exact RGB565 rectangular copy/fill kernels for ESP32-S3 PIE. Each row may
// have arbitrary padding and a different 16-byte pointer phase. Equal source
// and destination phases scalar-align once, use 128-bit transfers for the
// interior, then copy the short tail. Differing phases use the scalar row body.

    .section .iram1, "ax"
    .align  4
    .global tinydraw_copy_pixel_rows_pie
    .type   tinydraw_copy_pixel_rows_pie, @function

// a2 source, a3 destination, a4/a5 strides in pixels, a6 width, a7 height
tinydraw_copy_pixel_rows_pie:
    entry   a1, 16
    slli    a4, a4, 1
    slli    a5, a5, 1
.Lcopy_row:
    mov     a8, a2
    mov     a9, a3
    xor     a10, a8, a9
    extui   a10, a10, 0, 4
    bnez    a10, .Lcopy_scalar_setup

    // Halfword prefix to the common 16-byte boundary.
    neg     a10, a8
    extui   a10, a10, 0, 4
    srli    a10, a10, 1
    min     a10, a10, a6
    mov     a11, a10
    loopnez a11, .Lcopy_prefix_done
.Lcopy_prefix:
    l16ui   a15, a8, 0
    s16i    a15, a9, 0
    addi    a8, a8, 2
    addi    a9, a9, 2
.Lcopy_prefix_done:
    sub     a10, a6, a10
    srli    a11, a10, 3
    loopnez a11, .Lcopy_vectors_done
.Lcopy_vectors:
    ee.vld.128.ip q0, a8, 16
    ee.vst.128.ip q0, a9, 16
.Lcopy_vectors_done:
    extui   a11, a10, 0, 3
    j       .Lcopy_scalar_tail

.Lcopy_scalar_setup:
    mov     a11, a6
.Lcopy_scalar_tail:
    loopnez a11, .Lcopy_row_done
.Lcopy_scalar:
    l16ui   a15, a8, 0
    s16i    a15, a9, 0
    addi    a8, a8, 2
    addi    a9, a9, 2
.Lcopy_row_done:
    add     a2, a2, a4
    add     a3, a3, a5
    addi    a7, a7, -1
    bnez    a7, .Lcopy_row
    retw.n

    .size tinydraw_copy_pixel_rows_pie, .-tinydraw_copy_pixel_rows_pie

    .align  4
    .global tinydraw_fill_pixel_rows_pie
    .type   tinydraw_fill_pixel_rows_pie, @function

// a2 destination, a3 replicated RGB565 color, a4 stride in pixels,
// a5 width, a6 height
tinydraw_fill_pixel_rows_pie:
    entry   a1, 16
    slli    a4, a4, 1
    ee.movi.32.q q0, a3, 0
    ee.movi.32.q q0, a3, 1
    ee.movi.32.q q0, a3, 2
    ee.movi.32.q q0, a3, 3
.Lfill_row:
    mov     a7, a2
    neg     a8, a7
    extui   a8, a8, 0, 4
    srli    a8, a8, 1
    min     a8, a8, a5
    mov     a9, a8
    loopnez a9, .Lfill_prefix_done
.Lfill_prefix:
    s16i    a3, a7, 0
    addi    a7, a7, 2
.Lfill_prefix_done:
    sub     a8, a5, a8
    srli    a9, a8, 3
    loopnez a9, .Lfill_vectors_done
.Lfill_vectors:
    ee.vst.128.ip q0, a7, 16
.Lfill_vectors_done:
    extui   a9, a8, 0, 3
    loopnez a9, .Lfill_row_done
.Lfill_tail:
    s16i    a3, a7, 0
    addi    a7, a7, 2
.Lfill_row_done:
    add     a2, a2, a4
    addi    a6, a6, -1
    bnez    a6, .Lfill_row
    retw.n

    .size tinydraw_fill_pixel_rows_pie, .-tinydraw_fill_pixel_rows_pie
