// ESP32-S3 PIE RGB565 byte-swap kernel. Each iteration handles 16 pixels:
// VUNZIP separates even/odd bytes from two vectors and reversed VZIP operands
// interleave each odd byte before its matching even byte.

    .section .iram1, "ax"
    .align  4
    .global tinydraw_stage_pixels_swapped_pie
    .type   tinydraw_stage_pixels_swapped_pie, @function

// a2: 16-byte-aligned source
// a3: 16-byte-aligned destination
// a4: positive count of 32-byte blocks
tinydraw_stage_pixels_swapped_pie:
    entry   a1, 16
    loopnez a4, .Lstage_done
.Lstage_loop:
    ee.vld.128.ip  q0, a2, 16
    ee.vld.128.ip  q1, a2, 16
    ee.vunzip.8    q0, q1
    ee.vzip.8      q1, q0
    ee.vst.128.ip  q1, a3, 16
    ee.vst.128.ip  q0, a3, 16
.Lstage_done:
    retw.n

    .size tinydraw_stage_pixels_swapped_pie, .-tinydraw_stage_pixels_swapped_pie

    .align  4
    .global tinydraw_stage_pixels_swapped_unaligned_pie
    .type   tinydraw_stage_pixels_swapped_unaligned_pie, @function

// Arbitrary-source-phase sibling. The destination remains 16-byte aligned.
// LD.USAR captures the source byte phase and SRC reconstructs the unaligned
// stream from consecutive aligned loads. The C++ caller leaves at least one
// readable vector after the output run; the final SRC.Q avoids one needless
// speculative load beyond that padding.
// a2: naturally uint16-aligned source, a3: 16-byte-aligned destination
// a4: positive count of 32-byte blocks
tinydraw_stage_pixels_swapped_unaligned_pie:
    entry   a1, 16
    ee.ld.128.usar.ip q0, a2, 16
    ee.vld.128.ip     q1, a2, 16
    addi    a4, a4, -1
    beqz    a4, .Lunaligned_final_01

.Lunaligned_01:
    ee.src.q.ld.ip q2, a2, 16, q0, q1
    ee.src.q.ld.ip q3, a2, 16, q1, q2
    ee.vunzip.8    q0, q1
    ee.vzip.8      q1, q0
    ee.vst.128.ip  q1, a3, 16
    ee.vst.128.ip  q0, a3, 16
    addi    a4, a4, -1
    beqz    a4, .Lunaligned_final_23

.Lunaligned_23:
    ee.src.q.ld.ip q0, a2, 16, q2, q3
    ee.src.q.ld.ip q1, a2, 16, q3, q0
    ee.vunzip.8    q2, q3
    ee.vzip.8      q3, q2
    ee.vst.128.ip  q3, a3, 16
    ee.vst.128.ip  q2, a3, 16
    addi    a4, a4, -1
    bnez    a4, .Lunaligned_01

.Lunaligned_final_01:
    ee.src.q.ld.ip q2, a2, 16, q0, q1
    ee.src.q       q1, q1, q2
    ee.vunzip.8    q0, q1
    ee.vzip.8      q1, q0
    ee.vst.128.ip  q1, a3, 16
    ee.vst.128.ip  q0, a3, 16
    retw.n

.Lunaligned_final_23:
    ee.src.q.ld.ip q0, a2, 16, q2, q3
    ee.src.q       q3, q3, q0
    ee.vunzip.8    q2, q3
    ee.vzip.8      q3, q2
    ee.vst.128.ip  q3, a3, 16
    ee.vst.128.ip  q2, a3, 16
    retw.n

    .size tinydraw_stage_pixels_swapped_unaligned_pie, .-tinydraw_stage_pixels_swapped_unaligned_pie

    .align  4
    .global tinydraw_stage_full_ring_rows_swapped_pie
    .type   tinydraw_stage_full_ring_rows_swapped_pie, @function

// Fixed 368-pixel full-row ring staging. Both bases are 16-byte aligned by the
// C++ guard. There are 46 vectors per row. Eight-pixel shifts retain the fully
// aligned paired-vector loop. Other shifts reconstruct 44 vectors with the
// USAR/SRC stream and scalarize exactly 16 pixels around the ring seam, which
// keeps every aligned PIE load inside its physical row. The vertical ring is
// split into at most two linear segments in both paths.
// a2: ring base, a3: destination, a4: first physical row
// a5: row count, a6: ring height, a7: horizontal shift in pixels
tinydraw_stage_full_ring_rows_swapped_pie:
    entry   a1, 32
    extui   a8, a7, 0, 3
    bnez    a8, .Lfull_unaligned

    // Original all-vector path for a multiple-of-eight-pixel shift.
    movi    a8, 368
    sub     a15, a6, a4
    min     a15, a15, a5
    sub     a5, a5, a15
    mull    a4, a4, a8
    addx2   a4, a4, a2
    srli    a7, a7, 3
    movi    a8, 46
    sub     a8, a8, a7

    // All horizontal seam geometry is invariant across the row batch. Keep
    // the byte offset and pair counts live so the per-row body is only the
    // two vector runs, the optional seam pair, and vertical ring advance.
    slli    a13, a7, 4
    srli    a10, a8, 1
    extui   a12, a8, 0, 1
    sub     a11, a7, a12
    srli    a11, a11, 1
    movi    a14, 736

.Lfull_row:
    add     a8, a4, a13
    loopnez a10, .Ltail_done
.Ltail_loop:
    ee.vld.128.ip  q0, a8, 16
    ee.vld.128.ip  q1, a8, 16
    ee.vunzip.8    q0, q1
    ee.vzip.8      q1, q0
    ee.vst.128.ip  q1, a3, 16
    ee.vst.128.ip  q0, a3, 16
.Ltail_done:
    mov     a9, a4
    loopnez a12, .Lseam_done
.Lseam_pair:
    ee.vld.128.ip  q0, a8, 16
    ee.vld.128.ip  q1, a9, 16
    ee.vunzip.8    q0, q1
    ee.vzip.8      q1, q0
    ee.vst.128.ip  q1, a3, 16
    ee.vst.128.ip  q0, a3, 16
.Lseam_done:
    loopnez a11, .Lhead_done
.Lhead_loop:
    ee.vld.128.ip  q0, a9, 16
    ee.vld.128.ip  q1, a9, 16
    ee.vunzip.8    q0, q1
    ee.vzip.8      q1, q0
    ee.vst.128.ip  q1, a3, 16
    ee.vst.128.ip  q0, a3, 16
.Lhead_done:
    add     a4, a4, a14
    addi    a15, a15, -1
    bnez    a15, .Lfull_row
    beqz    a5, .Lfull_done
    mov     a4, a2
    mov     a15, a5
    movi    a5, 0
    j       .Lfull_row
.Lfull_done:
    retw.n

    // Arbitrary pixel phase. Invariant geometry:
    //   v = shift_x / 8, r = shift_x % 8
    //   floor((45-v)/2) 32-byte tail blocks
    //   scalar tail and r-pixel head prefix
    //   floor(v/2) 32-byte head blocks and optional final vector
    // This always emits 22 PIE blocks plus 16 scalar pixels per row.
.Lfull_unaligned:
    s32i    a0, a1, 0
    movi    a8, 368
    sub     a15, a6, a4
    min     a15, a15, a5
    sub     a5, a5, a15
    mull    a4, a4, a8
    addx2   a4, a4, a2
    srli    a12, a7, 3
    extui   a13, a7, 0, 3
    movi    a14, 736

.Lfull_unaligned_row:
    // Vectorize the even-sized prefix before the physical row end.
    addx2   a8, a7, a4
    mov     a9, a3
    movi    a10, 45
    sub     a10, a10, a12
    srli    a10, a10, 1
    call0   .Lfull_unaligned_blocks

    // Finish the physical-row tail scalarly. The source is computed from the
    // output geometry because the USAR stream itself reads one vector ahead.
    addx2   a8, a7, a4
    movi    a11, 45
    sub     a11, a11, a12
    srli    a11, a11, 1
    slli    a10, a11, 5
    add     a8, a8, a10
    slli    a10, a11, 4
    movi    a11, 368
    sub     a11, a11, a7
    sub     a11, a11, a10
    loopnez a11, .Lfull_tail_scalar_done
.Lfull_tail_scalar:
    l16ui   a6, a8, 0
    slli    a10, a6, 8
    srli    a6, a6, 8
    or      a6, a6, a10
    s16i    a6, a9, 0
    addi    a8, a8, 2
    addi    a9, a9, 2
.Lfull_tail_scalar_done:

    // Cross the cyclic seam scalarly until the destination realigns. Source
    // and destination now share phase r, so the remaining head can use SRC.
    mov     a8, a4
    mov     a11, a13
    loopnez a11, .Lfull_head_prefix_done
.Lfull_head_prefix:
    l16ui   a6, a8, 0
    slli    a10, a6, 8
    srli    a6, a6, 8
    or      a6, a6, a10
    s16i    a6, a9, 0
    addi    a8, a8, 2
    addi    a9, a9, 2
.Lfull_head_prefix_done:

    addx2   a8, a13, a4
    mov     a10, a12
    srli    a10, a10, 1
    call0   .Lfull_unaligned_blocks
    bbci    a12, 0, .Lfull_unaligned_row_done

    // An odd v leaves one final eight-pixel vector. Scalar access is required
    // here because the generic two-vector block kernel deliberately has no
    // one-vector store path.
    addx2   a8, a13, a4
    mov     a11, a12
    srli    a11, a11, 1
    slli    a11, a11, 5
    add     a8, a8, a11
    movi    a11, 8
    loopnez a11, .Lfull_head_tail_done
.Lfull_head_tail:
    l16ui   a6, a8, 0
    slli    a10, a6, 8
    srli    a6, a6, 8
    or      a6, a6, a10
    s16i    a6, a9, 0
    addi    a8, a8, 2
    addi    a9, a9, 2
.Lfull_head_tail_done:

.Lfull_unaligned_row_done:
    add     a4, a4, a14
    add     a3, a3, a14
    addi    a15, a15, -1
    bnez    a15, .Lfull_unaligned_row
    beqz    a5, .Lfull_unaligned_done
    mov     a4, a2
    mov     a15, a5
    movi    a5, 0
    j       .Lfull_unaligned_row
.Lfull_unaligned_done:
    l32i    a0, a1, 0
    retw.n

// Local call0 leaf. a8/a9 are source/destination and a10 is a positive count
// of 32-byte blocks. It preserves a2-a7 and a12-a15 for the enclosing 2D loop.
.Lfull_unaligned_blocks:
    beqz    a10, .Lfull_unaligned_blocks_done
    ee.ld.128.usar.ip q0, a8, 16
    ee.vld.128.ip     q1, a8, 16
    addi    a10, a10, -1
    beqz    a10, .Lfull_blocks_final_01
.Lfull_blocks_01:
    ee.src.q.ld.ip q2, a8, 16, q0, q1
    ee.src.q.ld.ip q3, a8, 16, q1, q2
    ee.vunzip.8    q0, q1
    ee.vzip.8      q1, q0
    ee.vst.128.ip  q1, a9, 16
    ee.vst.128.ip  q0, a9, 16
    addi    a10, a10, -1
    beqz    a10, .Lfull_blocks_final_23
.Lfull_blocks_23:
    ee.src.q.ld.ip q0, a8, 16, q2, q3
    ee.src.q.ld.ip q1, a8, 16, q3, q0
    ee.vunzip.8    q2, q3
    ee.vzip.8      q3, q2
    ee.vst.128.ip  q3, a9, 16
    ee.vst.128.ip  q2, a9, 16
    addi    a10, a10, -1
    bnez    a10, .Lfull_blocks_01
.Lfull_blocks_final_01:
    ee.src.q.ld.ip q2, a8, 16, q0, q1
    ee.src.q       q1, q1, q2
    ee.vunzip.8    q0, q1
    ee.vzip.8      q1, q0
    ee.vst.128.ip  q1, a9, 16
    ee.vst.128.ip  q0, a9, 16
    ret
.Lfull_blocks_final_23:
    ee.src.q.ld.ip q0, a8, 16, q2, q3
    ee.src.q       q3, q3, q0
    ee.vunzip.8    q2, q3
    ee.vzip.8      q3, q2
    ee.vst.128.ip  q3, a9, 16
    ee.vst.128.ip  q2, a9, 16
.Lfull_unaligned_blocks_done:
    ret

    .size tinydraw_stage_full_ring_rows_swapped_pie, .-tinydraw_stage_full_ring_rows_swapped_pie
