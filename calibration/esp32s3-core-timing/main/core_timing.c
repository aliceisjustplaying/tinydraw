/* ESP32-S3 core-timing calibration probes.
 *
 * Measures three CPU-side cost categories the memory-timing harness does not
 * cover, in the same CAL_RECORD line format:
 *
 *   1. Window overflow/underflow exception cost: a non-tail call8 recursion
 *      ladder. Shallow depths fit the 64-entry physical register file; each
 *      additional level past the knee pays one WindowOverflow8 on the way
 *      down and one WindowUnderflow8 on the way up. The per-depth samples
 *      let the analysis fit both slopes and their difference.
 *   2. Issue rate by instruction width, dependency, and loop-body alignment:
 *      IRAM-resident assembly blocks of known encodings (verified from the
 *      built ELF, not assumed), plus a zero-overhead loop whose body start
 *      is placed at +0/+1/+2/+3 mod 4 by explicit padding.
 *   3. Interrupt entry and resume latency through the ESP-IDF dispatcher:
 *      a software interrupt (INTSET) timed from trigger WSR to handler
 *      entry, and from handler exit back to interrupted code, at dispatcher
 *      level 1 and level 3.
 *
 * Trials for loop metrics follow the sibling harness (warm run, then 9
 * samples, watchdog fed between samples). Window and issue trials run at
 * INTLEVEL 15 so ticks never land inside a measured window; interrupt
 * trials obviously run with interrupts enabled.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_chip_info.h"
#include "esp_intr_alloc.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "xtensa_api.h"

#define SCHEMA_VERSION "1.0.0"
#define HARNESS_VERSION "1.0.0"
#define TRIALS 9
#define CHAIN_ITERATIONS 256u
#define ISSUE_CALLS 4096u
#define ISSUE_OPS_PER_CALL 256u
#define LOOP_ITERATIONS 1024u /* movi immediate range is +-2048 */
#define LOOP_BODY_NOPS 8u
#define INTR_SAMPLES 33u

static volatile uint32_t benchmark_sink;

static inline uint32_t read_ccount(void) {
  uint32_t value;
  __asm__ __volatile__("rsr.ccount %0" : "=a"(value));
  return value;
}

static inline uint32_t mask_interrupts(void) {
  uint32_t previous;
  __asm__ __volatile__("rsil %0, 15" : "=a"(previous));
  return previous;
}

static inline void restore_interrupts(uint32_t previous) {
  __asm__ __volatile__("wsr.ps %0\n rsync" ::"a"(previous));
}

/* --- Probe 1: window overflow/underflow ladder ------------------------- */

static uint32_t window_chain(uint32_t depth);
/* The volatile pointer defeats GCC's tail/accumulator recursion
 * transformations, so every level is a real windowed callx8. */
static uint32_t (*volatile window_chain_ptr)(uint32_t) = window_chain;

static IRAM_ATTR __attribute__((noinline)) uint32_t window_chain(uint32_t depth) {
  if (depth == 0u) {
    return 1u;
  }
  return window_chain_ptr(depth - 1u) + depth;
}

static IRAM_ATTR uint32_t bench_window_depth(uint32_t depth) {
  uint32_t sum = window_chain(depth); /* warm */
  const uint32_t previous = mask_interrupts();
  const uint32_t start = read_ccount();
  for (uint32_t i = 0; i < CHAIN_ITERATIONS; ++i) {
    sum += window_chain(depth);
  }
  const uint32_t elapsed = read_ccount() - start;
  restore_interrupts(previous);
  benchmark_sink = sum;
  return elapsed;
}

/* --- Probe 2: issue rate and alignment --------------------------------- */

static IRAM_ATTR __attribute__((noinline)) void issue_empty_block(void) {
  __asm__ __volatile__("" ::: "memory");
}

static IRAM_ATTR __attribute__((noinline)) void issue_narrow_block(void) {
  __asm__ __volatile__(
      ".rept 256\n"
      "nop.n\n"
      ".endr\n" ::: "memory");
}

static IRAM_ATTR __attribute__((noinline)) void issue_wide_block(void) {
  /* Underscore forms disable assembler narrowing; verified 3-byte. */
  __asm__ __volatile__(
      ".rept 256\n"
      "_or a8, a8, a8\n"
      ".endr\n" ::: "a8", "memory");
}

static IRAM_ATTR __attribute__((noinline)) void issue_mixed_block(void) {
  __asm__ __volatile__(
      ".rept 128\n"
      "nop.n\n"
      "_or a8, a8, a8\n"
      ".endr\n" ::: "a8", "memory");
}

static IRAM_ATTR __attribute__((noinline)) void issue_dependent_block(void) {
  __asm__ __volatile__(
      ".rept 256\n"
      "_addi a8, a8, 1\n"
      ".endr\n" ::: "a8", "memory");
}

static IRAM_ATTR __attribute__((noinline)) void issue_independent_block(void) {
  __asm__ __volatile__(
      ".rept 64\n"
      "_addi a8, a8, 1\n"
      "_addi a9, a9, 1\n"
      "_addi a10, a10, 1\n"
      "_addi a11, a11, 1\n"
      ".endr\n" ::: "a8", "a9", "a10", "a11", "memory");
}

typedef void (*issue_block_t)(void);

static IRAM_ATTR uint32_t bench_issue(issue_block_t block) {
  block(); /* warm */
  const uint32_t previous = mask_interrupts();
  const uint32_t start = read_ccount();
  for (uint32_t i = 0; i < ISSUE_CALLS; ++i) {
    block();
  }
  const uint32_t elapsed = read_ccount() - start;
  restore_interrupts(previous);
  return elapsed;
}

/* Zero-overhead loop whose body start lands at +0/+2/+3/+1 mod 4 through
 * explicit padding after a 16-byte anchor. The body is LOOP_BODY_NOPS
 * two-byte nops; the loopback target is the body start. */
/* Assembly probes with label-controlled layout; see core_timing_loops.S.
 * The suffix is the loop-body start residue mod 4, verified from the ELF. */
void loop_body_r0(void);
void loop_body_r1(void);
void loop_body_r2(void);
void loop_body_r3(void);

#define LOOP_CALLS 64u

static IRAM_ATTR uint32_t bench_loop(void (*probe)(void)) {
  probe(); /* warm */
  const uint32_t previous = mask_interrupts();
  const uint32_t start = read_ccount();
  for (uint32_t i = 0; i < LOOP_CALLS; ++i) {
    probe();
  }
  const uint32_t elapsed = read_ccount() - start;
  restore_interrupts(previous);
  return elapsed;
}

static uint32_t loop_r0_trial(void) { return bench_loop(loop_body_r0); }
static uint32_t loop_r1_trial(void) { return bench_loop(loop_body_r1); }
static uint32_t loop_r2_trial(void) { return bench_loop(loop_body_r2); }
static uint32_t loop_r3_trial(void) { return bench_loop(loop_body_r3); }

/* --- Probe 3: interrupt entry/resume latency --------------------------- */

static volatile uint32_t isr_enter_cc;
static volatile uint32_t isr_exit_cc;
static volatile uint32_t isr_fired;
static uint32_t sw_intr_mask;

static void IRAM_ATTR sw_interrupt_handler(void *argument) {
  (void)argument;
  isr_enter_cc = read_ccount();
  xt_set_intclear(sw_intr_mask);
  isr_fired = 1u;
  isr_exit_cc = read_ccount();
}

static void emit_samples(const char *name, const char *memory,
                         const char *pattern, uint32_t operations,
                         const uint32_t *samples, uint32_t count,
                         const char *baseline, const char *note) {
  printf("CAL_RECORD {\"type\":\"metric\",\"name\":\"%s\","
         "\"memory\":\"%s\",\"access_pattern\":\"%s\","
         "\"operations_per_trial\":%" PRIu32 ","
         "\"bytes_per_operation\":0,\"ccount_samples\":[",
         name, memory, pattern, operations);
  for (uint32_t index = 0; index < count; ++index) {
    printf("%s%" PRIu32, index == 0 ? "" : ",", samples[index]);
  }
  printf("],\"baseline\":%s,\"note\":\"%s\"}\n",
         baseline == NULL ? "null" : baseline, note);
  fflush(stdout);
}

typedef uint32_t (*trial_fn_t)(void);

static void run_trials(const char *name, const char *pattern,
                       uint32_t operations, trial_fn_t function,
                       const char *baseline, const char *note) {
  uint32_t samples[TRIALS];
  for (uint32_t trial = 0; trial < TRIALS; ++trial) {
    samples[trial] = function();
    vTaskDelay(1);
  }
  emit_samples(name, "iram", pattern, operations, samples, TRIALS, baseline, note);
}

/* Bounce depth through a volatile so each bench call is a fresh trial fn. */
static volatile uint32_t current_depth;
static uint32_t window_trial(void) { return bench_window_depth(current_depth); }
static uint32_t issue_empty_trial(void) { return bench_issue(issue_empty_block); }
static uint32_t issue_narrow_trial(void) { return bench_issue(issue_narrow_block); }
static uint32_t issue_wide_trial(void) { return bench_issue(issue_wide_block); }
static uint32_t issue_mixed_trial(void) { return bench_issue(issue_mixed_block); }
static uint32_t issue_dependent_trial(void) { return bench_issue(issue_dependent_block); }
static uint32_t issue_independent_trial(void) { return bench_issue(issue_independent_block); }

static void run_interrupt_probe(const char *suffix, int flags) {
  intr_handle_t handle = NULL;
  esp_err_t err = esp_intr_alloc(ETS_INTERNAL_SW0_INTR_SOURCE,
                                 flags | ESP_INTR_FLAG_IRAM,
                                 sw_interrupt_handler, NULL, &handle);
  if (err != ESP_OK) {
    /* SW0 can be owned by the FreeRTOS port; fall back to SW1. */
    err = esp_intr_alloc(ETS_INTERNAL_SW1_INTR_SOURCE,
                         flags | ESP_INTR_FLAG_IRAM,
                         sw_interrupt_handler, NULL, &handle);
  }
  if (err != ESP_OK) {
    printf("CALIBRATION_FAILED intr_alloc %s err=%d\n", suffix, (int)err);
    return;
  }
  sw_intr_mask = 1u << (uint32_t)esp_intr_get_intno(handle);

  uint32_t entry_samples[INTR_SAMPLES];
  uint32_t resume_samples[INTR_SAMPLES];
  for (uint32_t sample = 0; sample < INTR_SAMPLES; ++sample) {
    isr_fired = 0u;
    const uint32_t start = read_ccount();
    xt_set_intset(sw_intr_mask);
    while (isr_fired == 0u) {
    }
    const uint32_t resumed = read_ccount();
    entry_samples[sample] = isr_enter_cc - start;
    resume_samples[sample] = resumed - isr_exit_cc;
    vTaskDelay(1);
  }
  ESP_ERROR_CHECK(esp_intr_free(handle));

  char name[64];
  snprintf(name, sizeof(name), "intr_entry_%s", suffix);
  emit_samples(name, "iram", "sw_intset_to_handler", 1, entry_samples,
               INTR_SAMPLES, NULL,
               "CCOUNT from WSR INTSET in the interrupted task to the first instruction of the IRAM handler, through the ESP-IDF dispatcher.");
  snprintf(name, sizeof(name), "intr_resume_%s", suffix);
  emit_samples(name, "iram", "handler_exit_to_task", 1, resume_samples,
               INTR_SAMPLES, NULL,
               "CCOUNT from the handler's last instruction back to the interrupted spin loop; includes dispatcher epilogue, restore, and up to one spin iteration.");
}

void app_main(void) {
  esp_chip_info_t chip = {0};
  esp_chip_info(&chip);
  const uint32_t cpu_hz = esp_rom_get_cpu_ticks_per_us() * 1000000u;

  printf("CAL_RECORD {\"type\":\"configuration\","
         "\"schema_version\":\"%s\",\"harness_version\":\"%s\","
         "\"idf_version\":\"%s\",\"target\":\"esp32s3\","
         "\"chip_revision\":%u,\"cores\":%u,"
         "\"cpu_hz\":%" PRIu32 ",\"ccount_hz\":%" PRIu32 ","
         "\"probe\":\"core-timing\",\"trials\":%u,"
         "\"chain_iterations\":%u,\"issue_calls\":%u,"
         "\"issue_ops_per_call\":%u,\"loop_iterations\":%u,"
         "\"loop_body_nops\":%u,\"intr_samples\":%u,"
         "\"uncertainty_notes\":["
         "\"Window and issue trials run at INTLEVEL 15; ticks cannot land inside a window.\","
         "\"Encodings are verified from the built ELF by the capture tooling, not assumed.\","
         "\"Interrupt latencies include the ESP-IDF dispatcher on purpose: that is what firmware experiences.\","
         "\"Resume latency includes up to one spin-loop iteration of quantization.\"]}\n",
         SCHEMA_VERSION, HARNESS_VERSION, esp_get_idf_version(), chip.revision,
         chip.cores, cpu_hz, cpu_hz, TRIALS, CHAIN_ITERATIONS, ISSUE_CALLS,
         ISSUE_OPS_PER_CALL, LOOP_ITERATIONS, LOOP_BODY_NOPS, INTR_SAMPLES);
  fflush(stdout);

  static const uint32_t depths[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 16, 20, 24, 32};
  for (uint32_t index = 0; index < sizeof(depths) / sizeof(depths[0]); ++index) {
    current_depth = depths[index];
    char name[48];
    snprintf(name, sizeof(name), "window_chain_depth_%02" PRIu32, depths[index]);
    run_trials(name, "call8_recursion", CHAIN_ITERATIONS, window_trial, NULL,
               "Non-tail call8 recursion to the named depth, 256 chains per trial.");
  }

  run_trials("issue_empty_baseline", "empty_calls", ISSUE_CALLS,
             issue_empty_trial, NULL,
             "Call-and-loop baseline for the 256-op issue blocks.");
  run_trials("issue_narrow_nops", "sequential_2byte", ISSUE_CALLS * ISSUE_OPS_PER_CALL,
             issue_narrow_trial, "\"issue_empty_baseline\"",
             "256 two-byte nop.n per call.");
  run_trials("issue_wide_or", "sequential_3byte", ISSUE_CALLS * ISSUE_OPS_PER_CALL,
             issue_wide_trial, "\"issue_empty_baseline\"",
             "256 three-byte or a8,a8,a8 per call.");
  run_trials("issue_mixed_widths", "alternating_2_3byte", ISSUE_CALLS * ISSUE_OPS_PER_CALL,
             issue_mixed_trial, "\"issue_empty_baseline\"",
             "Alternating nop.n and or a8,a8,a8, 256 ops per call.");
  run_trials("issue_dependent_addi", "serial_dependency", ISSUE_CALLS * ISSUE_OPS_PER_CALL,
             issue_dependent_trial, "\"issue_empty_baseline\"",
             "256 chained addi a8,a8,1 per call: every instruction depends on the previous one.");
  run_trials("issue_independent_addi", "four_way_independent", ISSUE_CALLS * ISSUE_OPS_PER_CALL,
             issue_independent_trial, "\"issue_empty_baseline\"",
             "256 addi over four rotating registers per call.");

  run_trials("loop_body_align_r0", "zero_overhead_loop",
             LOOP_CALLS * LOOP_ITERATIONS, loop_r0_trial, NULL,
             "64 calls of a 1024-iteration nop.n loop, body start at +0 mod 4.");
  run_trials("loop_body_align_r1", "zero_overhead_loop",
             LOOP_CALLS * LOOP_ITERATIONS, loop_r1_trial, NULL,
             "64 calls of a 1024-iteration nop.n loop, body start at +1 mod 4.");
  run_trials("loop_body_align_r2", "zero_overhead_loop",
             LOOP_CALLS * LOOP_ITERATIONS, loop_r2_trial, NULL,
             "64 calls of a 1024-iteration nop.n loop, body start at +2 mod 4.");
  run_trials("loop_body_align_r3", "zero_overhead_loop",
             LOOP_CALLS * LOOP_ITERATIONS, loop_r3_trial, NULL,
             "64 calls of a 1024-iteration nop.n loop, body start at +3 mod 4.");

  run_interrupt_probe("level1", ESP_INTR_FLAG_LEVEL1);
  run_interrupt_probe("level3", ESP_INTR_FLAG_LEVEL3);

  printf("CALIBRATION_DONE sink=%" PRIu32 "\n", benchmark_sink);
  fflush(stdout);
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
