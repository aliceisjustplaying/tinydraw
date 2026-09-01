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

.macro first_line_target index
  .align 32
  .global tier_b_first_line_i_\index\()
  .type tier_b_first_line_i_\index\(),@function
tier_b_first_line_i_\index\():
  .rept 15
  nop.n
  .endr
  ret.n
  .size tier_b_first_line_i_\index\(), .-tier_b_first_line_i_\index\()
.endm

first_line_target 0
first_line_target 1
first_line_target 2
first_line_target 3
first_line_target 4

.section .iram1,"ax",@progbits
.align 32
.global tier_b_store_issue_block_start
.global tier_b_store_issue_block_end
.global tier_b_store_issue_block
.type tier_b_store_issue_block,@function
tier_b_store_issue_block_start:
tier_b_store_issue_block:
  entry a1, 16
  .begin no-transform
  .rept 256
  s32i.n a3, a2, 0
  .endr
  memw
  .end no-transform
  retw.n
tier_b_store_issue_block_end:
.size tier_b_store_issue_block, .-tier_b_store_issue_block
