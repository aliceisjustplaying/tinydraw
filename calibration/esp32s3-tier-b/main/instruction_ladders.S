/* Flash-resident instruction ranges. CONFIG_SPIRAM_FETCH_INSTRUCTIONS copies
 * these ranges to PSRAM at boot for the instruction-PSRAM cells. The ELF map
 * and disassembly remain capture-session preconditions. */

.section .text.tier_b_instruction_ladders,"ax",@progbits
.literal_position

.macro instruction_ladder lines
  .align 32
  .global tier_b_instruction_\lines\()_lines_start
  .global tier_b_instruction_\lines\()_lines_end
  .global tier_b_instruction_\lines\()_lines
  .type tier_b_instruction_\lines\()_lines,@function
tier_b_instruction_\lines\()_lines_start:
tier_b_instruction_\lines\()_lines:
  .rept ((\lines * 32 - 2) / 2)
  nop.n
  .endr
  ret.n
tier_b_instruction_\lines\()_lines_end:
  .size tier_b_instruction_\lines\()_lines, .-tier_b_instruction_\lines\()_lines
.endm

instruction_ladder 1
instruction_ladder 2
instruction_ladder 4
instruction_ladder 8
instruction_ladder 16
