#include "vector_v2_autosave_store.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <span>

#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tinydraw/vector_v2/memory_layout.h"

namespace tinydraw::esp32 {
namespace {

constexpr char kPartitionLabel[] = "drawing";
constexpr auto kPartitionSubtype = static_cast<esp_partition_subtype_t>(0x40);
constexpr std::size_t kSectorBytes = 4096U;
constexpr std::size_t kWriteQueueLength = 8U;
constexpr std::size_t kWorkerStackBytes = 8192U;
constexpr UBaseType_t kWorkerPriority = 1U;

enum class AutosaveStatus : std::uint8_t {
  kIdle,
  kSaving,
  kNeedsCheckpoint,
  kFull,
  kError,
};

std::size_t align_to_sector(std::size_t bytes) {
  if (bytes > std::numeric_limits<std::size_t>::max() - (kSectorBytes - 1U)) {
    return 0U;
  }
  return (bytes + kSectorBytes - 1U) / kSectorBytes * kSectorBytes;
}

class PartitionJournalSource final : public vector_v2::AuthorityJournalSource {
 public:
  explicit PartitionJournalSource(const esp_partition_t* partition) : partition_(partition) {}

  bool read(std::size_t offset, std::span<std::byte> output) const override {
    return partition_ != nullptr && offset <= partition_->size &&
           output.size() <= partition_->size - offset &&
           esp_partition_read(partition_, offset, output.data(), output.size()) == ESP_OK;
  }

 private:
  const esp_partition_t* partition_ = nullptr;
};

struct PendingWrite {
  std::byte* bytes = nullptr;
  std::size_t size = 0;
  std::size_t offset = 0;
  std::uint64_t sequence = 0;
  vector_v2::AuthorityReadView authority{};
  bool erase_partition = false;
  bool erase_tail = false;
};

void free_pending(PendingWrite* pending) {
  if (pending != nullptr) {
    heap_caps_free(pending->bytes);
    delete pending;
  }
}

}  // namespace

struct VectorV2AutosaveStore::Impl {
  Impl() {
    partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, kPartitionSubtype, kPartitionLabel);
    queue = xQueueCreate(kWriteQueueLength, sizeof(PendingWrite*));
    mutex = xSemaphoreCreateMutexStatic(&mutex_storage);
    io_buffer = static_cast<std::byte*>(
        heap_caps_malloc(kSectorBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (partition == nullptr || partition->size < kSectorBytes ||
        partition->size % kSectorBytes != 0U || queue == nullptr || mutex == nullptr ||
        io_buffer == nullptr) {
      return;
    }
    ready_flag = xTaskCreatePinnedToCore(worker_entry, "v2_autosave", kWorkerStackBytes, this,
                                         kWorkerPriority, &worker_task, 0) == pdPASS;
  }

  ~Impl() {
    if (worker_task != nullptr) {
      vTaskDelete(worker_task);
    }
    if (queue != nullptr) {
      PendingWrite* pending = nullptr;
      while (xQueueReceive(queue, &pending, 0) == pdTRUE) {
        free_pending(pending);
      }
      vQueueDelete(queue);
    }
    heap_caps_free(io_buffer);
  }

  static void worker_entry(void* argument) { static_cast<Impl*>(argument)->worker(); }

  bool erase_range(std::size_t offset, std::size_t bytes) {
    for (std::size_t erased = 0; erased < bytes; erased += kSectorBytes) {
      if (esp_partition_erase_range(partition, offset + erased, kSectorBytes) != ESP_OK) {
        return false;
      }
      taskYIELD();
    }
    return true;
  }

  bool write_bytes(std::size_t physical_offset, std::span<const std::byte> bytes) {
    std::size_t written = 0;
    while (written < bytes.size()) {
      const std::size_t count = std::min(kSectorBytes, bytes.size() - written);
      std::memcpy(io_buffer, bytes.data() + written, count);
      if (esp_partition_write(partition, physical_offset + written, io_buffer, count) != ESP_OK) {
        return false;
      }
      written += count;
      taskYIELD();
    }
    return true;
  }

  bool verify_bytes(std::size_t physical_offset, std::span<const std::byte> expected) {
    std::size_t verified = 0;
    while (verified < expected.size()) {
      const std::size_t count = std::min(kSectorBytes, expected.size() - verified);
      if (esp_partition_read(partition, physical_offset + verified, io_buffer, count) != ESP_OK ||
          !std::equal(expected.begin() + static_cast<std::ptrdiff_t>(verified),
                      expected.begin() + static_cast<std::ptrdiff_t>(verified + count),
                      io_buffer)) {
        return false;
      }
      verified += count;
      taskYIELD();
    }
    return true;
  }

  bool write_pending(const PendingWrite& pending) {
    if (pending.erase_partition && !erase_range(0U, partition->size)) {
      return false;
    }
    if (pending.erase_tail && !erase_range(pending.offset, partition->size - pending.offset)) {
      return false;
    }
    if (!pending.erase_partition && !pending.erase_tail &&
        !erase_range(pending.offset, pending.size)) {
      return false;
    }
    const std::size_t body_bytes = pending.size - vector_v2::kAuthorityJournalCommitMarkerBytes;
    const auto bytes = std::span<const std::byte>(pending.bytes, pending.size);
    // The final marker is the publication point and is deliberately its own
    // flash call after every preceding byte succeeds.
    return write_bytes(pending.offset, bytes.first(body_bytes)) &&
           write_bytes(pending.offset + body_bytes,
                       bytes.last(vector_v2::kAuthorityJournalCommitMarkerBytes)) &&
           verify_bytes(pending.offset, bytes);
  }

  void worker() {
    for (;;) {
      PendingWrite* pending = nullptr;
      if (xQueueReceive(queue, &pending, portMAX_DELAY) != pdTRUE || pending == nullptr) {
        continue;
      }
      // Taking ownership under the submit mutex closes the only window in
      // which a newer state-only snapshot may replace this queued buffer.
      xSemaphoreTake(mutex, portMAX_DELAY);
      if (coalescible_state == pending) {
        coalescible_state = nullptr;
      }
      xSemaphoreGive(mutex);
      const bool written = !write_failed.load() && write_pending(*pending);
      if (written) {
        std::printf("TINYDRAW_AUTOSAVE_COMMIT sequence=%llu bytes=%lu offset=%lu\n",
                    static_cast<unsigned long long>(pending->sequence),
                    static_cast<unsigned long>(pending->size),
                    static_cast<unsigned long>(pending->offset));
      } else {
        write_failed.store(true);
        checkpoint_needed.store(true);
        std::printf("TINYDRAW_AUTOSAVE_WRITE_FAIL sequence=%llu offset=%lu\n",
                    static_cast<unsigned long long>(pending->sequence),
                    static_cast<unsigned long>(pending->offset));
      }
      std::fflush(stdout);
      free_pending(pending);
      if (write_failed.load()) {
        save_status.store(AutosaveStatus::kError);
      } else if (uxQueueMessagesWaiting(queue) == 0U) {
        save_status.store(checkpoint_needed.load() ? AutosaveStatus::kNeedsCheckpoint
                                                   : AutosaveStatus::kIdle);
      }
      outstanding_writes.fetch_sub(1U);
    }
  }

  bool ready() const { return ready_flag; }

  bool submit(vector_v2::JournalChange change, const vector_v2::OperationLog& log,
              const vector_v2::JournalState& state) {
    if (!ready() || !initialized.load() || write_failed.load() ||
        (checkpoint_needed.load() && change.kind != vector_v2::JournalChangeKind::kCheckpoint)) {
      return false;
    }
    const auto minimum = vector_v2::authority_journal_encoded_size(change, log);
    if (!minimum.has_value()) {
      return false;
    }
    const std::size_t transaction_bytes = align_to_sector(*minimum);
    if (transaction_bytes == 0U) {
      return false;
    }
    auto* pending = new (std::nothrow) PendingWrite;
    if (pending == nullptr) {
      checkpoint_needed.store(true);
      save_status.store(AutosaveStatus::kNeedsCheckpoint);
      return false;
    }
    pending->bytes = static_cast<std::byte*>(
        heap_caps_malloc(transaction_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (pending->bytes == nullptr) {
      free_pending(pending);
      checkpoint_needed.store(true);
      save_status.store(AutosaveStatus::kNeedsCheckpoint);
      return false;
    }

    xSemaphoreTake(mutex, portMAX_DELAY);
    const std::uint64_t sequence = next_sequence;
    const std::size_t offset = reserved_offset;
    const std::size_t partition_bytes = partition->size;
    const vector_v2::AuthorityReadView authority = log.read_view();
    // State-only transactions carry no drawing payload. While the newest one
    // is still queue-owned, replace its immutable buffer at the same flash
    // offset and sequence so recovery sees one committed state transition.
    const bool replace_queued_state =
        change.kind == vector_v2::JournalChangeKind::kState && coalescible_state != nullptr &&
        coalescible_state->size == transaction_bytes && coalescible_state->authority == authority;
    if (replace_queued_state) {
      pending->size = transaction_bytes;
      pending->offset = coalescible_state->offset;
      pending->sequence = coalescible_state->sequence;
      pending->authority = authority;
      const bool encoded = vector_v2::encode_authority_journal(
          change, log, state, pending->sequence, std::span(pending->bytes, pending->size));
      if (encoded) {
        std::swap(pending->bytes, coalescible_state->bytes);
        save_status.store(AutosaveStatus::kSaving);
        xSemaphoreGive(mutex);
        free_pending(pending);
        return true;
      }
      xSemaphoreGive(mutex);
      free_pending(pending);
      checkpoint_needed.store(true);
      save_status.store(AutosaveStatus::kNeedsCheckpoint);
      return false;
    }
    if (offset > partition_bytes || transaction_bytes > partition_bytes - offset) {
      xSemaphoreGive(mutex);
      free_pending(pending);
      save_status.store(AutosaveStatus::kFull);
      return false;
    }
    pending->size = transaction_bytes;
    pending->offset = offset;
    pending->sequence = sequence;
    pending->authority = authority;
    pending->erase_partition = erase_partition_before_next;
    pending->erase_tail = erase_tail_before_next;
    const bool encoded = vector_v2::encode_authority_journal(
        change, log, state, sequence, std::span(pending->bytes, pending->size));
    if (!encoded) {
      xSemaphoreGive(mutex);
      free_pending(pending);
      checkpoint_needed.store(true);
      save_status.store(AutosaveStatus::kNeedsCheckpoint);
      return false;
    }
    outstanding_writes.fetch_add(1U);
    if (xQueueSend(queue, &pending, 0) != pdTRUE) {
      outstanding_writes.fetch_sub(1U);
      xSemaphoreGive(mutex);
      free_pending(pending);
      checkpoint_needed.store(true);
      save_status.store(AutosaveStatus::kNeedsCheckpoint);
      return false;
    }
    reserved_offset += transaction_bytes;
    ++next_sequence;
    if (next_sequence == 0U) {
      next_sequence = 1U;
    }
    erase_partition_before_next = false;
    erase_tail_before_next = false;
    coalescible_state = change.kind == vector_v2::JournalChangeKind::kState ? pending : nullptr;
    if (change.kind == vector_v2::JournalChangeKind::kCheckpoint) {
      checkpoint_needed.store(false);
    }
    save_status.store(AutosaveStatus::kSaving);
    xSemaphoreGive(mutex);
    return true;
  }

  const esp_partition_t* partition = nullptr;
  QueueHandle_t queue = nullptr;
  StaticSemaphore_t mutex_storage{};
  SemaphoreHandle_t mutex = nullptr;
  TaskHandle_t worker_task = nullptr;
  std::byte* io_buffer = nullptr;
  std::atomic<AutosaveStatus> save_status{AutosaveStatus::kIdle};
  std::atomic<bool> initialized{false};
  std::atomic<bool> checkpoint_needed{false};
  std::atomic<bool> write_failed{false};
  // Counts queue-owned and worker-owned writes, closing the dequeue window
  // where uxQueueMessagesWaiting() alone could let flush return early.
  std::atomic<std::size_t> outstanding_writes{0U};
  // Guarded by mutex; cleared when the worker claims the pointed-to write.
  PendingWrite* coalescible_state = nullptr;
  std::size_t reserved_offset = 0U;
  std::uint64_t next_sequence = 1U;
  bool erase_partition_before_next = false;
  bool erase_tail_before_next = false;
  bool ready_flag = false;
};

VectorV2AutosaveStore::VectorV2AutosaveStore() : impl_(new (std::nothrow) Impl) {}

VectorV2AutosaveStore::~VectorV2AutosaveStore() { delete impl_; }

bool VectorV2AutosaveStore::ready() const { return impl_ != nullptr && impl_->ready(); }

VectorV2AutosaveRestoreStatus VectorV2AutosaveStore::restore(vector_v2::OperationLog& log,
                                                             vector_v2::JournalState& state) {
  if (!ready() || impl_->initialized.load()) {
    return VectorV2AutosaveRestoreStatus::kUnavailable;
  }
  auto* records = static_cast<vector_v2::OperationRecord*>(
      heap_caps_malloc(vector_v2::kOperationRecordBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  auto* samples = static_cast<vector_v2::CompactOperationSample*>(
      heap_caps_malloc(vector_v2::kOperationSampleBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (records == nullptr || samples == nullptr) {
    heap_caps_free(records);
    heap_caps_free(samples);
    return VectorV2AutosaveRestoreStatus::kError;
  }
  const PartitionJournalSource source(impl_->partition);
  vector_v2::JournalState recovered_state{};
  const vector_v2::JournalRecovery recovery = vector_v2::recover_authority_journal(
      source, impl_->partition->size, std::span(records, vector_v2::kOperationCapacity),
      std::span(samples, vector_v2::kOperationSampleCapacity), recovered_state);

  VectorV2AutosaveRestoreStatus status = VectorV2AutosaveRestoreStatus::kError;
  if (recovery.status == vector_v2::JournalRecoveryStatus::kRecovered) {
    const bool restored =
        log.restore({.epoch = recovery.state.epoch,
                     .generation = recovery.state.generation,
                     .active_operation_count = recovery.state.active_operation_count,
                     .records = std::span(records, recovery.state.retained_operation_count),
                     .samples = std::span(samples, recovery.retained_sample_count)});
    if (restored && recovery.bytes_consumed % kSectorBytes == 0U) {
      state = recovered_state;
      impl_->reserved_offset = recovery.bytes_consumed;
      impl_->next_sequence = recovery.sequence + 1U;
      if (impl_->next_sequence == 0U) {
        impl_->next_sequence = 1U;
      }
      impl_->checkpoint_needed.store(recovery.discarded_tail);
      impl_->erase_tail_before_next = recovery.discarded_tail;
      impl_->save_status.store(recovery.discarded_tail ? AutosaveStatus::kNeedsCheckpoint
                                                       : AutosaveStatus::kIdle);
      status = recovery.discarded_tail ? VectorV2AutosaveRestoreStatus::kRecoveredTail
                                       : VectorV2AutosaveRestoreStatus::kRestored;
    }
  } else if (recovery.status == vector_v2::JournalRecoveryStatus::kEmpty ||
             recovery.status == vector_v2::JournalRecoveryStatus::kCorrupt) {
    // Empty flash and pre-V2 Raster snapshot bytes both start a fresh V2
    // journal. Erasure stays on the low-priority worker and the first
    // checkpoint is not published until that erase completes.
    impl_->reserved_offset = 0U;
    impl_->next_sequence = 1U;
    impl_->erase_partition_before_next = true;
    impl_->checkpoint_needed.store(true);
    impl_->save_status.store(AutosaveStatus::kNeedsCheckpoint);
    status = VectorV2AutosaveRestoreStatus::kBlank;
  }
  heap_caps_free(records);
  heap_caps_free(samples);
  impl_->initialized.store(status != VectorV2AutosaveRestoreStatus::kError);
  return status;
}

bool VectorV2AutosaveStore::submit(vector_v2::JournalChange change,
                                   const vector_v2::OperationLog& log,
                                   const vector_v2::JournalState& state) {
  return impl_ != nullptr && impl_->submit(change, log, state);
}

bool VectorV2AutosaveStore::submit_checkpoint(const vector_v2::OperationLog& log,
                                              const vector_v2::JournalState& state) {
  return submit({.kind = vector_v2::JournalChangeKind::kCheckpoint}, log, state);
}

bool VectorV2AutosaveStore::checkpoint_required() const {
  return impl_ != nullptr && impl_->checkpoint_needed.load();
}

bool VectorV2AutosaveStore::flush(std::uint32_t timeout_ms) {
  if (!ready()) {
    return false;
  }
  const TickType_t started = xTaskGetTickCount();
  const TickType_t timeout = pdMS_TO_TICKS(timeout_ms);
  while (impl_->outstanding_writes.load() != 0U) {
    if (xTaskGetTickCount() - started >= timeout) {
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  return impl_->save_status.load() == AutosaveStatus::kIdle;
}

}  // namespace tinydraw::esp32
