// Cycle window for the five-pixel scalar oracle. The caller supplies the fully
// materialized target and ABI arguments. Windowed call8 rotates a10-a12 into
// the oracle's a2-a4 while leaving a2 and a4-a5 here live across the call.

    .section .iram1, "ax"
    .align  4
    .global tinydraw_measure_rgb565_call_window
    .type   tinydraw_measure_rgb565_call_window, @function

// a2: uint32_t endpoints[2]
// a3: source
// a4: destination
// a5: width
// a6: oracle target
tinydraw_measure_rgb565_call_window:
    entry       a1, 16
    mov         a10, a3
    mov         a11, a4
    mov         a12, a5
    mov         a8, a6
    rsr.ccount  a4
    callx8      a8
    rsr.ccount  a5
    s32i        a4, a2, 0
    s32i        a5, a2, 4
    retw.n

    .size tinydraw_measure_rgb565_call_window, .-tinydraw_measure_rgb565_call_window
