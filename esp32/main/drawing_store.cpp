#include "drawing_store.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <span>

#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tinydraw/storage/drawing_snapshot.h"

namespace tinydraw::esp32 {
namespace {

constexpr char kPartitionLabel[] = "drawing";
constexpr auto kPartitionSubtype = static_cast<esp_partition_subtype_t>(0x40);
constexpr std::uint32_t kHeaderMagic = 0x57415244U;
constexpr std::uint32_t kHeaderVersion = 3U;
constexpr std::size_t kHeaderBytes = DrawingSnapshot::kSectorBytes;
constexpr std::size_t kBankBytes =
    kHeaderBytes + DrawingSnapshot::kSectorCount * DrawingSnapshot::kSectorBytes;
constexpr std::size_t kRequiredPartitionBytes = 2U * kBankBytes;
constexpr std::uint32_t kIdleDelayUs = 500'000U;
constexpr std::uint32_t kHashOffset = 2'166'136'261U;
constexpr std::uint32_t kHashPrime = 16'777'619U;

struct LegacySnapshotHeader {
  std::uint32_t magic = kHeaderMagic;
  std::uint32_t version = 2U;
  std::uint32_t width = WorldCanvas::kWidth;
  std::uint32_t height = WorldCanvas::kHeight;
  std::int32_t origin_x = (WorldCanvas::kWidth - kCanvasWidth) / 2;
  std::int32_t origin_y = (WorldCanvas::kHeight - kCanvasHeight) / 2;
  std::uint32_t checksum = 0U;
};

struct SnapshotHeader {
  std::uint32_t magic = kHeaderMagic;
  std::uint32_t version = kHeaderVersion;
  std::uint32_t width = WorldCanvas::kWidth;
  std::uint32_t height = WorldCanvas::kHeight;
  std::int32_t origin_x = (WorldCanvas::kWidth - kCanvasWidth) / 2;
  std::int32_t origin_y = (WorldCanvas::kHeight - kCanvasHeight) / 2;
  std::uint32_t generation = 0U;
  std::uint32_t committed = 0U;
  std::array<std::uint32_t, DrawingSnapshot::kSectorCount> sector_checksums{};
  std::uint32_t checksum = 0U;
};
static_assert(sizeof(SnapshotHeader) <= kHeaderBytes);

std::size_t header_offset(std::size_t bank) { return bank * kBankBytes; }

std::size_t data_offset(std::size_t bank, std::size_t sector) {
  return header_offset(bank) + kHeaderBytes + sector * DrawingSnapshot::kSectorBytes;
}

std::uint32_t include_word(std::uint32_t hash, std::uint32_t word) {
  for (unsigned shift = 0U; shift < 32U; shift += 8U) {
    hash = (hash ^ static_cast<std::uint8_t>(word >> shift)) * kHashPrime;
  }
  return hash;
}

std::uint32_t sector_checksum(std::span<const std::uint16_t> pixels) {
  std::uint32_t hash = kHashOffset;
  for (const std::uint16_t pixel : pixels) {
    hash = (hash ^ static_cast<std::uint8_t>(pixel)) * kHashPrime;
    hash = (hash ^ static_cast<std::uint8_t>(pixel >> 8U)) * kHashPrime;
  }
  return hash;
}

std::uint32_t header_checksum(const SnapshotHeader& header) {
  std::uint32_t hash = kHashOffset;
  hash = include_word(hash, header.magic);
  hash = include_word(hash, header.version);
  hash = include_word(hash, header.width);
  hash = include_word(hash, header.height);
  hash = include_word(hash, static_cast<std::uint32_t>(header.origin_x));
  hash = include_word(hash, static_cast<std::uint32_t>(header.origin_y));
  hash = include_word(hash, header.generation);
  hash = include_word(hash, header.committed);
  for (const std::uint32_t checksum : header.sector_checksums) {
    hash = include_word(hash, checksum);
  }
  return hash;
}

SnapshotHeader make_blank_header(ViewOrigin origin) {
  SnapshotHeader header;
  header.origin_x = origin.x;
  header.origin_y = origin.y;
  std::uint32_t erased_checksum = kHashOffset;
  for (std::size_t index = 0U; index < DrawingSnapshot::kSectorPixels; ++index) {
    erased_checksum = (erased_checksum ^ 0xFFU) * kHashPrime;
    erased_checksum = (erased_checksum ^ 0xFFU) * kHashPrime;
  }
  header.sector_checksums.fill(erased_checksum);
  header.checksum = header_checksum(header);
  return header;
}

bool valid_legacy_header(const LegacySnapshotHeader& header) {
  const std::uint32_t checksum = header.magic ^ header.version ^ header.width ^ header.height ^
                                 static_cast<std::uint32_t>(header.origin_x) ^
                                 static_cast<std::uint32_t>(header.origin_y) ^ 0xA5D3'719BU;
  return header.magic == kHeaderMagic && header.version == 2U &&
         header.width == static_cast<std::uint32_t>(WorldCanvas::kWidth) &&
         header.height == static_cast<std::uint32_t>(WorldCanvas::kHeight) &&
         header.origin_x >= 0 && header.origin_x <= WorldCanvas::kWidth - kCanvasWidth &&
         header.origin_y >= 0 && header.origin_y <= WorldCanvas::kHeight - kCanvasHeight &&
         header.checksum == checksum;
}

bool valid_header(const SnapshotHeader& header) {
  return header.magic == kHeaderMagic && header.version == kHeaderVersion &&
         header.width == static_cast<std::uint32_t>(WorldCanvas::kWidth) &&
         header.height == static_cast<std::uint32_t>(WorldCanvas::kHeight) &&
         header.origin_x >= 0 && header.origin_x <= WorldCanvas::kWidth - kCanvasWidth &&
         header.origin_y >= 0 && header.origin_y <= WorldCanvas::kHeight - kCanvasHeight &&
         header.committed <= 1U && header.checksum == header_checksum(header);
}

bool newer_generation(std::uint32_t first, std::uint32_t second) {
  return static_cast<std::int32_t>(first - second) > 0;
}

enum class LegacyMigration { kAbsent, kMigrated, kFailed };

}  // namespace

struct DrawingStore::Impl {
  Impl() {
    partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, kPartitionSubtype, kPartitionLabel);
    sector_buffer = static_cast<std::uint16_t*>(
        heap_caps_malloc(DrawingSnapshot::kSectorBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    mutex = xSemaphoreCreateMutexStatic(&mutex_storage);
    if (partition == nullptr || partition->size < kRequiredPartitionBytes ||
        sector_buffer == nullptr || mutex == nullptr) {
      return;
    }

    snapshot.initialize_blank();
    const bool first_valid = read_validated_header(0U, active_header);
    SnapshotHeader second_header{};
    const bool second_valid = read_validated_header(1U, second_header);
    if (first_valid || second_valid) {
      if (second_valid &&
          (!first_valid || newer_generation(second_header.generation, active_header.generation))) {
        active_bank = 1U;
        active_header = second_header;
      }
      saved_drawing = active_header.committed != 0U;
      snapshot.load_origin({active_header.origin_x, active_header.origin_y});
    } else {
      const LegacyMigration migration = migrate_legacy();
      if (migration == LegacyMigration::kFailed) {
        return;
      }
      if (migration == LegacyMigration::kAbsent) {
        active_bank = 0U;
        active_header = make_blank_header(snapshot.origin());
        if (esp_partition_erase_range(partition, 0U, kRequiredPartitionBytes) != ESP_OK ||
            !write_header(active_bank, active_header)) {
          return;
        }
      }
    }

    last_activity_us = static_cast<std::uint32_t>(esp_timer_get_time());
    ready = xTaskCreatePinnedToCore(save_task_entry, "tinydraw_save", 10'240U, this, 1U, &task,
                                    0) == pdPASS;
  }

  ~Impl() {
    if (task != nullptr) {
      vTaskDelete(task);
    }
    heap_caps_free(sector_buffer);
  }

  LegacyMigration migrate_legacy() {
    LegacySnapshotHeader legacy{};
    if (esp_partition_read(partition, 0U, &legacy, sizeof(legacy)) != ESP_OK ||
        !valid_legacy_header(legacy)) {
      return LegacyMigration::kAbsent;
    }
    SnapshotHeader migrated = make_blank_header({legacy.origin_x, legacy.origin_y});
    migrated.committed = 1U;
    if (esp_partition_erase_range(partition, header_offset(1U), kBankBytes) != ESP_OK) {
      return LegacyMigration::kFailed;
    }
    for (std::size_t sector = 0U; sector < DrawingSnapshot::kSectorCount; ++sector) {
      if (esp_partition_read(partition, data_offset(0U, sector), sector_buffer,
                             DrawingSnapshot::kSectorBytes) != ESP_OK) {
        return LegacyMigration::kFailed;
      }
      const auto pixels =
          std::span<const std::uint16_t>(sector_buffer, DrawingSnapshot::kSectorPixels);
      migrated.sector_checksums[sector] = sector_checksum(pixels);
      if (!write_sector(1U, sector, pixels)) {
        return LegacyMigration::kFailed;
      }
    }
    migrated.checksum = header_checksum(migrated);
    if (!write_header(1U, migrated)) {
      return LegacyMigration::kFailed;
    }
    active_bank = 1U;
    active_header = migrated;
    saved_drawing = true;
    snapshot.load_origin({migrated.origin_x, migrated.origin_y});
    return LegacyMigration::kMigrated;
  }

  bool read_validated_header(std::size_t bank, SnapshotHeader& header) {
    if (esp_partition_read(partition, header_offset(bank), &header, sizeof(header)) != ESP_OK ||
        !valid_header(header)) {
      return false;
    }
    if (header.committed == 0U) {
      return true;
    }
    for (std::size_t sector = 0U; sector < DrawingSnapshot::kSectorCount; ++sector) {
      if (esp_partition_read(partition, data_offset(bank, sector), sector_buffer,
                             DrawingSnapshot::kSectorBytes) != ESP_OK ||
          sector_checksum(std::span<const std::uint16_t>(
              sector_buffer, DrawingSnapshot::kSectorPixels)) != header.sector_checksums[sector]) {
        return false;
      }
    }
    return true;
  }

  bool erase_header(std::size_t bank) {
    return esp_partition_erase_range(partition, header_offset(bank), kHeaderBytes) == ESP_OK;
  }

  bool write_header(std::size_t bank, const SnapshotHeader& header) {
    std::fill_n(sector_buffer, DrawingSnapshot::kSectorPixels, 0xFFFFU);
    std::memcpy(sector_buffer, &header, sizeof(header));
    return erase_header(bank) && esp_partition_write(partition, header_offset(bank), sector_buffer,
                                                     kHeaderBytes) == ESP_OK;
  }

  bool write_sector(std::size_t bank, std::size_t sector, std::span<const std::uint16_t> pixels) {
    if (sector >= DrawingSnapshot::kSectorCount || pixels.size() < DrawingSnapshot::kSectorPixels) {
      return false;
    }
    const std::size_t offset = data_offset(bank, sector);
    return esp_partition_erase_range(partition, offset, DrawingSnapshot::kSectorBytes) == ESP_OK &&
           esp_partition_write(partition, offset, pixels.data(), DrawingSnapshot::kSectorBytes) ==
               ESP_OK;
  }

  bool ensure_mirror() {
    const std::size_t mirror = 1U - active_bank;
    SnapshotHeader header{};
    if (esp_partition_read(partition, header_offset(mirror), &header, sizeof(header)) == ESP_OK &&
        valid_header(header) && header.generation == active_header.generation &&
        header.checksum == active_header.checksum) {
      return true;
    }
    if (!erase_header(mirror)) {
      return false;
    }
    for (std::size_t sector = 0U; sector < DrawingSnapshot::kSectorCount; ++sector) {
      if (esp_partition_read(partition, data_offset(mirror, sector), sector_buffer,
                             DrawingSnapshot::kSectorBytes) != ESP_OK) {
        return false;
      }
      const auto pixels =
          std::span<const std::uint16_t>(sector_buffer, DrawingSnapshot::kSectorPixels);
      if (sector_checksum(pixels) == active_header.sector_checksums[sector]) {
        continue;
      }
      if (esp_partition_read(partition, data_offset(active_bank, sector), sector_buffer,
                             DrawingSnapshot::kSectorBytes) != ESP_OK ||
          sector_checksum(pixels) != active_header.sector_checksums[sector] ||
          !write_sector(mirror, sector, pixels)) {
        return false;
      }
    }
    return write_header(mirror, active_header);
  }

  bool commit_sector(std::size_t sector, std::span<const std::uint16_t> pixels, ViewOrigin origin) {
    if (!ensure_mirror()) {
      return false;
    }
    const std::size_t target = 1U - active_bank;
    SnapshotHeader next = active_header;
    next.origin_x = origin.x;
    next.origin_y = origin.y;
    ++next.generation;
    next.committed = 1U;
    next.sector_checksums[sector] = sector_checksum(pixels);
    next.checksum = header_checksum(next);
    if (!erase_header(target) || !write_sector(target, sector, pixels) ||
        !write_header(target, next)) {
      return false;
    }

    const std::size_t previous = active_bank;
    active_bank = target;
    active_header = next;
    saved_drawing = true;
    // write_header uses sector_buffer as staging, so reload the durable data
    // before mirroring it and again before the caller's generation check.
    if (esp_partition_read(partition, data_offset(active_bank, sector), sector_buffer,
                           DrawingSnapshot::kSectorBytes) != ESP_OK) {
      return false;
    }
    const auto committed_pixels =
        std::span<const std::uint16_t>(sector_buffer, DrawingSnapshot::kSectorPixels);
    // The new bank is already durable. Mirror the same sector so the next
    // transaction can remain tile-granular; a failed mirror is repaired from
    // the validated active bank before the next commit.
    if (!erase_header(previous) || !write_sector(previous, sector, committed_pixels) ||
        !write_header(previous, active_header)) {
      return false;
    }
    return esp_partition_read(partition, data_offset(active_bank, sector), sector_buffer,
                              DrawingSnapshot::kSectorBytes) == ESP_OK;
  }

  bool commit_metadata(ViewOrigin origin) {
    if (!ensure_mirror()) {
      return false;
    }
    const std::size_t target = 1U - active_bank;
    SnapshotHeader next = active_header;
    next.origin_x = origin.x;
    next.origin_y = origin.y;
    ++next.generation;
    next.checksum = header_checksum(next);
    if (!write_header(target, next)) {
      return false;
    }
    const std::size_t previous = active_bank;
    active_bank = target;
    active_header = next;
    return write_header(previous, active_header);
  }

  void note_activity() {
    if (!ready) {
      return;
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    last_activity_us = static_cast<std::uint32_t>(esp_timer_get_time());
    ++generation;
    xSemaphoreGive(mutex);
  }

  void pause() {
    if (!ready) {
      return;
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    suspended = true;
    ++generation;
    xSemaphoreGive(mutex);
  }

  void request_save() {
    last_activity_us = static_cast<std::uint32_t>(esp_timer_get_time());
    ++generation;
    xTaskNotifyGive(task);
  }

  static void save_task_entry(void* argument) { static_cast<Impl*>(argument)->save_task(); }

  void save_task() {
    while (true) {
      static_cast<void>(ulTaskNotifyTake(pdTRUE, portMAX_DELAY));
      std::uint32_t sectors_written = 0;
      const auto started = esp_timer_get_time();
      while (true) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (suspended || world_pixels.size() < WorldCanvas::kRequiredPixels) {
          xSemaphoreGive(mutex);
          break;
        }
        const std::uint32_t elapsed =
            static_cast<std::uint32_t>(esp_timer_get_time()) - last_activity_us;
        if (elapsed < kIdleDelayUs) {
          const auto remaining_ms = (kIdleDelayUs - elapsed + 999U) / 1'000U;
          xSemaphoreGive(mutex);
          static_cast<void>(ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(remaining_ms)));
          continue;
        }

        std::size_t sector = DrawingSnapshot::kSectorCount;
        for (std::size_t candidate = 0; candidate < DrawingSnapshot::kSectorCount; ++candidate) {
          if (snapshot.sector_pending(candidate)) {
            sector = candidate;
            break;
          }
        }
        if (sector < DrawingSnapshot::kSectorCount) {
          // Repair an interrupted mirror before borrowing sector_buffer for
          // the mutable world copy.
          xSemaphoreGive(mutex);
          if (!ensure_mirror()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
          }
          xSemaphoreTake(mutex, portMAX_DELAY);
          if (!snapshot.sector_pending(sector)) {
            xSemaphoreGive(mutex);
            continue;
          }
          const std::uint32_t copied_generation = generation;
          const ViewOrigin copied_origin = snapshot.origin();
          const bool copied = snapshot.copy_sector(
              sector, world_pixels, std::span(sector_buffer, DrawingSnapshot::kSectorPixels));
          xSemaphoreGive(mutex);
          if (!copied) {
            break;
          }
          const bool written = commit_sector(
              sector, std::span<const std::uint16_t>(sector_buffer, DrawingSnapshot::kSectorPixels),
              copied_origin);
          xSemaphoreTake(mutex, portMAX_DELAY);
          if (written && copied_generation == generation &&
              snapshot.sector_matches(
                  sector, world_pixels,
                  std::span<const std::uint16_t>(sector_buffer, DrawingSnapshot::kSectorPixels))) {
            snapshot.acknowledge_sector(sector);
            ++sectors_written;
          }
          xSemaphoreGive(mutex);
          if (!written) {
            vTaskDelay(pdMS_TO_TICKS(100));
          }
          continue;
        }

        if (snapshot.metadata_pending()) {
          const ViewOrigin origin = snapshot.origin();
          const std::uint32_t copied_generation = generation;
          xSemaphoreGive(mutex);
          const bool written = commit_metadata(origin);
          xSemaphoreTake(mutex, portMAX_DELAY);
          if (written && copied_generation == generation && snapshot.origin() == origin) {
            snapshot.acknowledge_metadata();
          }
          xSemaphoreGive(mutex);
          if (!written) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
          }
        } else {
          xSemaphoreGive(mutex);
          break;
        }
      }
      if (sectors_written > 0U) {
        std::printf("TINYDRAW_AUTOSAVE sectors=%lu elapsed_us=%lld\n",
                    static_cast<unsigned long>(sectors_written),
                    static_cast<long long>(esp_timer_get_time() - started));
      }
    }
  }

  const esp_partition_t* partition = nullptr;
  std::uint16_t* sector_buffer = nullptr;
  DrawingSnapshot snapshot;
  std::span<std::uint16_t> world_pixels{};
  StaticSemaphore_t mutex_storage{};
  SemaphoreHandle_t mutex = nullptr;
  TaskHandle_t task = nullptr;
  SnapshotHeader active_header{};
  std::size_t active_bank = 0U;
  std::uint32_t last_activity_us = 0;
  std::uint32_t generation = 0;
  bool saved_drawing = false;
  bool suspended = false;
  bool ready = false;
};

DrawingStore::DrawingStore() : impl_(new (std::nothrow) Impl) {}

DrawingStore::~DrawingStore() { delete impl_; }

bool DrawingStore::ready() const { return impl_ != nullptr && impl_->ready; }

bool DrawingStore::restore(WorldCanvas& world, std::span<std::uint16_t> committed,
                           std::span<std::uint16_t> visible) {
  if (!ready()) {
    return false;
  }
  xSemaphoreTake(impl_->mutex, portMAX_DELAY);
  impl_->world_pixels = world.pixels();
  bool loaded = impl_->world_pixels.size() >= WorldCanvas::kRequiredPixels;
  if (loaded && impl_->saved_drawing) {
    for (std::size_t sector = 0; sector < DrawingSnapshot::kSectorCount; ++sector) {
      if (esp_partition_read(impl_->partition, data_offset(impl_->active_bank, sector),
                             impl_->sector_buffer, DrawingSnapshot::kSectorBytes) != ESP_OK ||
          !impl_->snapshot.load_sector(
              sector,
              std::span<const std::uint16_t>(impl_->sector_buffer, DrawingSnapshot::kSectorPixels),
              impl_->world_pixels)) {
        loaded = false;
        break;
      }
    }
  }
  if (!loaded) {
    static_cast<void>(world.clear(committed, visible));
  } else {
    static_cast<void>(world.show(impl_->snapshot.origin(), committed, visible));
  }
  xSemaphoreGive(impl_->mutex);
  return loaded;
}

void DrawingStore::activity() {
  if (ready()) {
    impl_->note_activity();
  }
}

void DrawingStore::suspend() {
  if (ready()) {
    impl_->pause();
  }
}

void DrawingStore::include_segment(Point from, Point to, float radius, ViewOrigin origin) {
  if (!ready()) {
    return;
  }
  xSemaphoreTake(impl_->mutex, portMAX_DELAY);
  impl_->last_activity_us = static_cast<std::uint32_t>(esp_timer_get_time());
  impl_->snapshot.include_segment(from, to, radius, origin);
  xSemaphoreGive(impl_->mutex);
}

void DrawingStore::save_stroke(WorldCanvas& world, std::span<const std::uint16_t> viewport) {
  if (!ready() || viewport.size() < WorldCanvas::kViewportPixels) {
    return;
  }
  xSemaphoreTake(impl_->mutex, portMAX_DELAY);
  impl_->world_pixels = world.pixels();
  const ViewOrigin origin = world.origin();
  for (std::size_t tile = 0; tile < DrawingSnapshot::kTileCount; ++tile) {
    if (!impl_->snapshot.tile_included(tile)) {
      continue;
    }
    const int world_x =
        static_cast<int>(tile % static_cast<std::size_t>(DrawingSnapshot::kTilesAcross)) *
        DrawingSnapshot::kTileSize;
    const int world_y =
        static_cast<int>(tile / static_cast<std::size_t>(DrawingSnapshot::kTilesAcross)) *
        DrawingSnapshot::kTileSize;
    static_cast<void>(world.capture_rect(
        viewport,
        {world_x - origin.x, world_y - origin.y, world_x - origin.x + DrawingSnapshot::kTileSize,
         world_y - origin.y + DrawingSnapshot::kTileSize}));
  }
  static_cast<void>(impl_->snapshot.schedule(origin));
  impl_->request_save();
  xSemaphoreGive(impl_->mutex);
}

void DrawingStore::save_viewport(WorldCanvas& world, std::span<const std::uint16_t> viewport) {
  if (!ready() || viewport.size() < WorldCanvas::kViewportPixels) {
    return;
  }
  xSemaphoreTake(impl_->mutex, portMAX_DELAY);
  if (!world.capture(viewport)) {
    xSemaphoreGive(impl_->mutex);
    return;
  }
  impl_->world_pixels = world.pixels();
  impl_->snapshot.include_viewport(world.origin());
  static_cast<void>(impl_->snapshot.schedule(world.origin()));
  impl_->request_save();
  xSemaphoreGive(impl_->mutex);
}

void DrawingStore::save_all(WorldCanvas& world) {
  if (!ready()) {
    return;
  }
  xSemaphoreTake(impl_->mutex, portMAX_DELAY);
  impl_->world_pixels = world.pixels();
  impl_->suspended = false;
  impl_->snapshot.include_all();
  static_cast<void>(impl_->snapshot.schedule(world.origin()));
  impl_->request_save();
  xSemaphoreGive(impl_->mutex);
}

void DrawingStore::save_origin(const WorldCanvas& world) {
  if (!ready()) {
    return;
  }
  xSemaphoreTake(impl_->mutex, portMAX_DELAY);
  static_cast<void>(impl_->snapshot.schedule(world.origin()));
  impl_->request_save();
  xSemaphoreGive(impl_->mutex);
}

}  // namespace tinydraw::esp32
