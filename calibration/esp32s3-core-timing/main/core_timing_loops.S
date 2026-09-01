/* Zero-overhead-loop alignment probes with layout controlled from each
 * function label. Alignment fill sits BEFORE the label, outside execution.
 * The no-transform region stops the assembler from widening, narrowing, or
 * re-aligning anything, so the loop-target residue is what the source says:
 *
 *   entry (3 bytes) + movi.n->movi (3 bytes under no-transform) = offset 6,
 *   function labels are 4-aligned, loopnez is 3 bytes, so the body starts
 *   at (9 + padding) mod 4:
 *     padding 0 bytes        -> body at +1
 *     padding nop.n (2)      -> body at +3
 *     padding _or (3)        -> body at +0
 *     padding nop.n+_or (5)  -> body at +2
 *
 * Body: 8 two-byte nop.n. 1024 iterations per call. Verified from the built
 * ELF by the capture tooling before flashing, not assumed.
 */

#define LOOP_ITERS 1024

.macro loop_probe name, pad_bytes
  .section .iram1, "ax"
  .align 4
  .global \name
  .type \name, @function
\name:
  entry a1, 32
  .begin no-transform
  _movi a8, LOOP_ITERS
  .ifc \pad_bytes, p2
  nop.n
  .endif
  .ifc \pad_bytes, p3
  _or a9, a9, a9
  .endif
  .ifc \pad_bytes, p5
  nop.n
  _or a9, a9, a9
  .endif
  /* Exact encoding of `loopnez a8, . + 19`. The fixed 16-byte body makes the
   * displacement invariant. Encoding it directly preserves the deliberately
   * unaligned r3 target without turning that calibration case into an
   * assembler warning. The built ELF is disassembled and checked before use. */
  .byte 0x76, 0x98, 0x0f
  .rept 8
  nop.n
  .endr
8:
  .end no-transform
  retw.n
  .size \name, . - \name
.endm

loop_probe loop_body_r1, p0
loop_probe loop_body_r3, p2
loop_probe loop_body_r0, p3
loop_probe loop_body_r2, p5
