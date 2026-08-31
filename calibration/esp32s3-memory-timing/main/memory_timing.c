#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define SCHEMA_VERSION "1.0.0"
#define HARNESS_VERSION "1.0.0"
#define TRIALS 9
#define STREAM_WORDS (128u * 1024u)
#define INTERNAL_WORDS (32u * 1024u)
#define STREAM_OPS (1024u * 1024u)
#define HIT_OPS (1024u * 1024u)
#define IRAM_CALLS 8192u
#define IRAM_NOPS_PER_CALL 256u

static volatile uint32_t benchmark_sink;

/* A non-zero initializer forces this 512 KiB working set into mapped flash. */
static const uint32_t flash_words[STREAM_WORDS]
    __attribute__((used, aligned(64))) = {[0 ... STREAM_WORDS - 1] = 0x6d2b79f5u};

typedef uint32_t (*bench_fn_t)(volatile uint32_t *buffer, uint32_t words,
                               uint32_t operations);

static inline uint32_t read_ccount(void) {
  uint32_t value;
  __asm__ __volatile__("rsr.ccount %0" : "=a"(value));
  return value;
}

static IRAM_ATTR __attribute__((noinline)) void iram_empty_block(void) {
  __asm__ __volatile__("" ::: "memory");
}

static IRAM_ATTR __attribute__((noinline)) void iram_nop_block(void) {
  __asm__ __volatile__(
      ".rept 256\n"
      "nop\n"
      ".endr\n"
      ::: "memory");
}

static IRAM_ATTR uint32_t bench_iram_empty(volatile uint32_t *unused,
                                           uint32_t words,
                                           uint32_t operations) {
  (void)unused;
  (void)words;
  (void)operations;
  const uint32_t start = read_ccount();
  for (uint32_t i = 0; i < IRAM_CALLS; ++i) {
    iram_empty_block();
  }
  return read_ccount() - start;
}

static IRAM_ATTR uint32_t bench_iram_nops(volatile uint32_t *unused,
                                          uint32_t words,
                                          uint32_t operations) {
  (void)unused;
  (void)words;
  (void)operations;
  const uint32_t start = read_ccount();
  for (uint32_t i = 0; i < IRAM_CALLS; ++i) {
    iram_nop_block();
  }
  return read_ccount() - start;
}

static IRAM_ATTR uint32_t bench_read_hit(volatile uint32_t *buffer,
                                         uint32_t words,
                                         uint32_t operations) {
  (void)words;
  uint32_t sum = buffer[0];
  const uint32_t start = read_ccount();
  for (uint32_t i = 0; i < operations; ++i) {
    sum += buffer[0];
  }
  const uint32_t elapsed = read_ccount() - start;
  benchmark_sink = sum;
  return elapsed;
}

static IRAM_ATTR uint32_t bench_read_sequential(volatile uint32_t *buffer,
                                                uint32_t words,
                                                uint32_t operations) {
  const uint32_t mask = words - 1u;
  uint32_t sum = 0;
  const uint32_t start = read_ccount();
  for (uint32_t i = 0; i < operations; ++i) {
    sum += buffer[i & mask];
  }
  const uint32_t elapsed = read_ccount() - start;
  benchmark_sink = sum;
  return elapsed;
}

static IRAM_ATTR uint32_t bench_write_sequential(volatile uint32_t *buffer,
                                                 uint32_t words,
                                                 uint32_t operations) {
  const uint32_t mask = words - 1u;
  const uint32_t start = read_ccount();
  for (uint32_t i = 0; i < operations; ++i) {
    buffer[i & mask] = i ^ 0xa5a5a5a5u;
  }
  const uint32_t elapsed = read_ccount() - start;
  benchmark_sink = buffer[operations & mask];
  return elapsed;
}

static IRAM_ATTR uint32_t bench_random_index_baseline(
    volatile uint32_t *buffer, uint32_t words, uint32_t operations) {
  (void)buffer;
  const uint32_t mask = words - 1u;
  uint32_t index = 0x12345u;
  uint32_t sum = 0;
  const uint32_t start = read_ccount();
  for (uint32_t i = 0; i < operations; ++i) {
    index = (index * 1664525u + 1013904223u) & mask;
    sum += index;
  }
  const uint32_t elapsed = read_ccount() - start;
  benchmark_sink = sum;
  return elapsed;
}

static IRAM_ATTR uint32_t bench_read_random(volatile uint32_t *buffer,
                                            uint32_t words,
                                            uint32_t operations) {
  const uint32_t mask = words - 1u;
  uint32_t index = 0x12345u;
  uint32_t sum = 0;
  const uint32_t start = read_ccount();
  for (uint32_t i = 0; i < operations; ++i) {
    index = (index * 1664525u + 1013904223u) & mask;
    sum += buffer[index];
  }
  const uint32_t elapsed = read_ccount() - start;
  benchmark_sink = sum;
  return elapsed;
}

static void emit_metric(const char *name, const char *memory,
                        const char *access_pattern, uint32_t operations,
                        uint32_t bytes_per_operation, bench_fn_t function,
                        volatile uint32_t *buffer, uint32_t words,
                        const char *baseline, const char *note) {
  uint32_t samples[TRIALS];
  function(buffer, words, operations); /* warm instruction/data paths */
  for (uint32_t trial = 0; trial < TRIALS; ++trial) {
    samples[trial] = function(buffer, words, operations);
    /* Let IDLE0 feed the watchdog; this delay is outside the CCOUNT window. */
    vTaskDelay(1);
  }

  printf("CAL_RECORD {\"type\":\"metric\",\"name\":\"%s\","
         "\"memory\":\"%s\",\"access_pattern\":\"%s\","
         "\"operations_per_trial\":%" PRIu32 ","
         "\"bytes_per_operation\":%" PRIu32 ",\"ccount_samples\":[",
         name, memory, access_pattern, operations, bytes_per_operation);
  for (uint32_t trial = 0; trial < TRIALS; ++trial) {
    printf("%s%" PRIu32, trial == 0 ? "" : ",", samples[trial]);
  }
  printf("],\"baseline\":%s,\"note\":\"%s\"}\n",
         baseline == NULL ? "null" : baseline, note);
  fflush(stdout);
}

static const char *flash_mode(void) {
#if CONFIG_ESPTOOLPY_FLASHMODE_QIO
  return "qio";
#elif CONFIG_ESPTOOLPY_FLASHMODE_DIO
  return "dio";
#else
  return "other";
#endif
}

static uint32_t flash_frequency_mhz(void) {
#if CONFIG_ESPTOOLPY_FLASHFREQ_120M
  return 120;
#elif CONFIG_ESPTOOLPY_FLASHFREQ_80M
  return 80;
#elif CONFIG_ESPTOOLPY_FLASHFREQ_40M
  return 40;
#else
  return 0;
#endif
}

static uint32_t psram_frequency_mhz(void) {
#if CONFIG_SPIRAM_SPEED_120M
  return 120;
#elif CONFIG_SPIRAM_SPEED_80M
  return 80;
#elif CONFIG_SPIRAM_SPEED_40M
  return 40;
#else
  return 0;
#endif
}

void app_main(void) {
  esp_chip_info_t chip = {0};
  esp_chip_info(&chip);
  uint32_t flash_size = 0;
  ESP_ERROR_CHECK(esp_flash_get_size(esp_flash_default_chip, &flash_size));
  const size_t psram_size = esp_psram_get_size();
  const uint32_t cpu_hz = esp_rom_get_cpu_ticks_per_us() * 1000000u;

  volatile uint32_t *sram = heap_caps_aligned_alloc(
      64, INTERNAL_WORDS * sizeof(uint32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  volatile uint32_t *psram = heap_caps_aligned_alloc(
      64, STREAM_WORDS * sizeof(uint32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (sram == NULL || psram == NULL) {
    printf("CALIBRATION_FAILED allocation sram=%p psram=%p\n", sram, psram);
    return;
  }
  for (uint32_t i = 0; i < INTERNAL_WORDS; ++i) {
    sram[i] = i * 2654435761u;
  }
  for (uint32_t i = 0; i < STREAM_WORDS; ++i) {
    psram[i] = i * 2654435761u;
  }

  printf("CAL_RECORD {\"type\":\"configuration\","
         "\"schema_version\":\"%s\",\"harness_version\":\"%s\","
         "\"idf_version\":\"%s\",\"target\":\"esp32s3\","
         "\"chip_revision\":%u,\"cores\":%u,"
         "\"cpu_hz\":%" PRIu32 ",\"ccount_hz\":%" PRIu32 ","
         "\"flash_bytes\":%" PRIu32 ",\"flash_mode\":\"%s\","
         "\"flash_image_header_mode\":\"%s\","
         "\"flash_frequency_mhz\":%" PRIu32 ","
         "\"psram_bytes\":%u,\"psram_mode\":\"octal\","
         "\"psram_frequency_mhz\":%" PRIu32 ","
         "\"data_cache_bytes\":%d,\"data_cache_line_bytes\":%d,"
         "\"instruction_cache_bytes\":%d,"
         "\"external_working_set_bytes\":%u,"
         "\"internal_working_set_bytes\":%u,\"trials\":%u,"
         "\"uncertainty_notes\":["
         "\"CCOUNT includes interrupt and loop overhead; medians and minima are retained.\","
         "\"Random external-memory results subtract an identical LCG/index loop baseline.\","
         "\"Random 512 KiB working sets are miss-dominated, not guaranteed one miss per access.\","
         "\"IRAM bandwidth uses the two-byte Xtensa density nop encoding confirmed from the ELF symbol size.\"]}\n",
         SCHEMA_VERSION, HARNESS_VERSION, esp_get_idf_version(), chip.revision,
         chip.cores, cpu_hz, cpu_hz, flash_size, flash_mode(),
         CONFIG_ESPTOOLPY_FLASHMODE, flash_frequency_mhz(), (unsigned)psram_size,
         psram_frequency_mhz(),
#ifdef CONFIG_ESP32S3_DATA_CACHE_SIZE
         CONFIG_ESP32S3_DATA_CACHE_SIZE,
#else
         0,
#endif
#ifdef CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE
         CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE,
#else
         0,
#endif
#ifdef CONFIG_ESP32S3_INSTRUCTION_CACHE_SIZE
         CONFIG_ESP32S3_INSTRUCTION_CACHE_SIZE,
#else
         0,
#endif
         (unsigned)(STREAM_WORDS * sizeof(uint32_t)),
         (unsigned)(INTERNAL_WORDS * sizeof(uint32_t)), TRIALS);
  fflush(stdout);

  emit_metric("iram_call_loop_baseline", "iram", "empty_calls", IRAM_CALLS,
              0, bench_iram_empty, NULL, 0, NULL,
              "Function-call and loop baseline for the IRAM nop block.");
  emit_metric("iram_instruction_fetch", "iram", "sequential_nops",
              IRAM_CALLS * IRAM_NOPS_PER_CALL, 2, bench_iram_nops, NULL, 0,
              "\"iram_call_loop_baseline\"",
              "256 two-byte sequential IRAM-resident nops per call; baseline is one empty call per block.");

  emit_metric("sram_read_hit", "internal_sram", "same_word", HIT_OPS, 4,
              bench_read_hit, sram, INTERNAL_WORDS, NULL,
              "Repeated volatile reads from one warm internal-SRAM word.");
  emit_metric("sram_read_sequential", "internal_sram", "sequential_128k",
              STREAM_OPS, 4, bench_read_sequential, sram, INTERNAL_WORDS, NULL,
              "Eight complete passes through a 128 KiB internal-SRAM working set.");
  emit_metric("sram_write_sequential", "internal_sram", "sequential_128k",
              STREAM_OPS, 4, bench_write_sequential, sram, INTERNAL_WORDS, NULL,
              "Eight complete volatile-write passes through internal SRAM.");

  emit_metric("flash_read_hit", "mapped_flash", "same_word", HIT_OPS, 4,
              bench_read_hit, (volatile uint32_t *)flash_words, STREAM_WORDS,
              NULL, "Repeated volatile reads from one warm flash-cache word.");
  emit_metric("flash_read_sequential", "mapped_flash", "sequential_512k",
              STREAM_OPS, 4, bench_read_sequential,
              (volatile uint32_t *)flash_words, STREAM_WORDS, NULL,
              "Four sequential passes through mapped flash; includes line fills and cache hits.");
  emit_metric("flash_random_index_baseline", "internal_registers",
              "lcg_index_only", STREAM_OPS, 0, bench_random_index_baseline,
              NULL, STREAM_WORDS, NULL,
              "Identical LCG/index arithmetic used by flash_read_random.");
  emit_metric("flash_read_random", "mapped_flash", "random_512k", STREAM_OPS,
              4, bench_read_random, (volatile uint32_t *)flash_words,
              STREAM_WORDS, "\"flash_random_index_baseline\"",
              "Pseudo-random reads over 512 KiB; cache-miss-dominated.");

  emit_metric("psram_read_hit", "octal_psram", "same_word", HIT_OPS, 4,
              bench_read_hit, psram, STREAM_WORDS, NULL,
              "Repeated volatile reads from one warm PSRAM-cache word.");
  emit_metric("psram_read_sequential", "octal_psram", "sequential_512k",
              STREAM_OPS, 4, bench_read_sequential, psram, STREAM_WORDS, NULL,
              "Four sequential passes through PSRAM; includes line fills and cache hits.");
  emit_metric("psram_write_sequential", "octal_psram", "sequential_512k",
              STREAM_OPS, 4, bench_write_sequential, psram, STREAM_WORDS, NULL,
              "Four sequential volatile-write passes through PSRAM.");
  emit_metric("psram_random_index_baseline", "internal_registers",
              "lcg_index_only", STREAM_OPS, 0, bench_random_index_baseline,
              NULL, STREAM_WORDS, NULL,
              "Identical LCG/index arithmetic used by psram_read_random.");
  emit_metric("psram_read_random", "octal_psram", "random_512k", STREAM_OPS,
              4, bench_read_random, psram, STREAM_WORDS,
              "\"psram_random_index_baseline\"",
              "Pseudo-random reads over 512 KiB; cache-miss-dominated.");

  printf("CALIBRATION_DONE sink=%" PRIu32 "\n", benchmark_sink);
  fflush(stdout);
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
