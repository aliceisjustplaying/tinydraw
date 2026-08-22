// ESP32-S3 PIE painter-buffer initialization. The caller scalar-aligns both
// spans to 16 bytes; each vector store initializes 16 bytes in one instruction.

    .section .iram1, "ax"
    .align  4
    .global tinydraw_initialize_raster_buffers_pie
    .type   tinydraw_initialize_raster_buffers_pie, @function

// a2: 16-byte-aligned RGB565 surface
// a3: packed color (the same RGB565 value in both halfwords)
// a4: positive count of 16-byte surface blocks
// a5: 16-byte-aligned mask
// a6: positive count of 16-byte mask blocks
tinydraw_initialize_raster_buffers_pie:
    entry   a1, 16
    ee.movi.32.q q0, a3, 0
    ee.movi.32.q q0, a3, 1
    ee.movi.32.q q0, a3, 2
    ee.movi.32.q q0, a3, 3
    loopnez a4, .Lsurface_done
.Lsurface_loop:
    ee.vst.128.ip q0, a2, 16
.Lsurface_done:
    ee.zero.q q0
    loopnez a6, .Lmask_done
.Lmask_loop:
    ee.vst.128.ip q0, a5, 16
.Lmask_done:
    retw.n

    .size tinydraw_initialize_raster_buffers_pie, .-tinydraw_initialize_raster_buffers_pie
