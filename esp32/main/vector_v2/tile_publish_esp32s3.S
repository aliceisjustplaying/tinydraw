// ESP32-S3 PIE 64x64 RGB565 tile publication. The fixed production source is
// one tile window inside a 128-pixel-stride producer surface; the destination
// is a packed tile-pool slot. Both pointers are 16-byte aligned by the caller.

    .section .iram1, "ax"
    .align  4
    .global tinydraw_publish_tile_64x64_stride128_pie
    .type   tinydraw_publish_tile_64x64_stride128_pie, @function

// a2: 16-byte-aligned source, 256 bytes between row starts
// a3: 16-byte-aligned packed destination, 128 bytes between row starts
tinydraw_publish_tile_64x64_stride128_pie:
    entry   a1, 16
    movi    a4, 64
    movi    a5, 128
    loopnez a4, .Lpublish_done
.Lpublish_row:
    ee.vld.128.ip  q0, a2, 16
    ee.vld.128.ip  q1, a2, 16
    ee.vst.128.ip  q0, a3, 16
    ee.vst.128.ip  q1, a3, 16
    ee.vld.128.ip  q0, a2, 16
    ee.vld.128.ip  q1, a2, 16
    ee.vst.128.ip  q0, a3, 16
    ee.vst.128.ip  q1, a3, 16
    ee.vld.128.ip  q0, a2, 16
    ee.vld.128.ip  q1, a2, 16
    ee.vst.128.ip  q0, a3, 16
    ee.vst.128.ip  q1, a3, 16
    ee.vld.128.ip  q0, a2, 16
    ee.vld.128.ip  q1, a2, 16
    ee.vst.128.ip  q0, a3, 16
    ee.vst.128.ip  q1, a3, 16
    add     a2, a2, a5
.Lpublish_done:
    retw.n

    .size tinydraw_publish_tile_64x64_stride128_pie, .-tinydraw_publish_tile_64x64_stride128_pie
