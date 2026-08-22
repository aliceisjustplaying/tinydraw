// Exact RGB565 channel-plane compositing for the settled renderer.  The five
// planes are independent, unaligned byte/halfword arrays in internal RAM.
// Keeping their cursors and the expanded source color in registers avoids the
// repeated workspace loads and caller-frame color spills emitted by GCC 15.

    .section .iram1, "ax"
    .align  4
    .literal .Ldivide_255_magic, 0x80808081
    .global tinydraw_settled_composite_esp32s3
    .type   tinydraw_settled_composite_esp32s3, @function

// a2: SettledTileWorkspace (five std::span data pointers at offsets 0,8,...32)
// a3: first pixel
// a4: pixel count
// a5: expanded source channels packed as 0x00BBGGRR
// a2 return: number of pixels that became fully saturated
tinydraw_settled_composite_esp32s3:
    entry   a1, 16
    l32i    a6, a2, 0
    l32i    a7, a2, 8
    l32i    a8, a2, 16
    l32i    a9, a2, 24
    l32i    a10, a2, 32
    add     a6, a6, a3
    add     a7, a7, a3
    addx2   a8, a3, a8
    addx2   a9, a3, a9
    addx2   a10, a3, a10
    extui   a2, a5, 0, 8
    extui   a3, a5, 8, 8
    extui   a5, a5, 16, 8
    movi    a11, 0
    loopnez a4, .Lcomposite_done
.Lcomposite_loop:
    l8ui    a12, a6, 0
    beqz    a12, .Lcomposite_next
    l8ui    a13, a7, 0
    movi    a14, 255
    bne     a12, a14, .Lcomposite_blend
    bnez    a13, .Lcomposite_blend
    s16i    a2, a8, 0
    s16i    a3, a9, 0
    s16i    a5, a10, 0
    s8i     a14, a7, 0
    addi    a11, a11, 1
    j       .Lcomposite_next

.Lcomposite_blend:
    sub     a14, a14, a13
    mul16u  a12, a12, a14
    addi    a12, a12, 127
    movi    a14, 254
    bgeu    a14, a12, .Lcomposite_next
    l32r    a15, .Ldivide_255_magic
    muluh   a12, a12, a15
    srli    a12, a12, 7

    mull    a14, a2, a12
    muluh   a14, a14, a15
    srli    a14, a14, 7
    l16ui   a4, a8, 0
    add     a14, a14, a4
    s16i    a14, a8, 0

    mull    a14, a3, a12
    muluh   a14, a14, a15
    srli    a14, a14, 7
    l16ui   a4, a9, 0
    add     a14, a14, a4
    s16i    a14, a9, 0

    mull    a14, a5, a12
    muluh   a14, a14, a15
    srli    a14, a14, 7
    l16ui   a4, a10, 0
    add     a14, a14, a4
    s16i    a14, a10, 0

    add     a13, a13, a12
    movi    a14, 255
    bne     a13, a14, .Lcomposite_store_alpha
    addi    a11, a11, 1
.Lcomposite_store_alpha:
    s8i     a13, a7, 0

.Lcomposite_next:
    addi    a6, a6, 1
    addi    a7, a7, 1
    addi    a8, a8, 2
    addi    a9, a9, 2
    addi    a10, a10, 2
.Lcomposite_done:
    mov     a2, a11
    retw.n

    .size tinydraw_settled_composite_esp32s3, .-tinydraw_settled_composite_esp32s3
