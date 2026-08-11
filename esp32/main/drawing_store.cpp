#include "drawing_store.h"

#include <algorithm>
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
constexpr std::uint32_t kHeaderVersion = 1U;
constexpr std::size_t kHeaderBytes = DrawingSnapshot::kSectorBytes;
constexpr std::size_t kDataOffset = kHeaderBytes;
constexpr std::size_t kRequiredPartitionBytes =
    kDataOffset + DrawingSnapshot::kSectorCount * DrawingSnapshot::kSectorBytes;
constexpr std::uint32_t kIdleDelayUs = 500'000U;

struct SnapshotHeader {
  std::uint32_t magic = kHeaderMagic;
  std::uint32_t version = kHeaderVersion;
  std::uint32_t width = WorldCanvas::kWidth;
  std::uint32_t height = WorldCanvas::kHeight;
  std::int32_t origin_x = kCanvasWidth / 2;
  std::int32_t origin_y = kCanvasHeight / 2;
  std::uint32_t checksum = 0;
};

std::uint32_t header_checksum(const SnapshotHeader& header) {
  return header.magic ^ header.version ^ header.width ^ header.height ^
         static_cast<std::uint32_t>(header.origin_x) ^ static_cast<std::uint32_t>(header.origin_y) ^
         0xA5D3'719BU;
}

SnapshotHeader make_header(ViewOrigin origin) {
  SnapshotHeader header;
  header.origin_x = origin.x;
  header.origin_y = origin.y;
  header.checksum = header_checksum(header);
  return header;
}

bool valid_header(const SnapshotHeader& header) {
  return header.magic == kHeaderMagic && header.version == kHeaderVersion &&
         header.width == static_cast<std::uint32_t>(WorldCanvas::kWidth) &&
         header.height == static_cast<std::uint32_t>(WorldCanvas::kHeight) &&
         header.origin_x >= 0 && header.origin_x <= WorldCanvas::kWidth - kCanvasWidth &&
         header.origin_y >= 0 && header.origin_y <= WorldCanvas::kHeight - kCanvasHeight &&
         header.checksum == header_checksum(header);
}

}  // namespace

struct DrawingStore::Impl {
  Impl() {
    partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, kPartitionSubtype, kPartitionLabel);
    snapshot_storage = static_cast<std::uint16_t*>(
        heap_caps_malloc(DrawingSnapshot::kRequiredPixels * sizeof(std::uint16_t),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    sector_buffer = static_cast<std::uint16_t*>(
        heap_caps_malloc(DrawingSnapshot::kSectorBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    mutex = xSemaphoreCreateMutexStatic(&mutex_storage);
    if (partition == nullptr || partition->size < kRequiredPartitionBytes ||
        snapshot_storage == nullptr || sector_buffer == nullptr || mutex == nullptr) {
      return;
    }

    snapshot = new (std::nothrow)
        DrawingSnapshot(std::span(snapshot_storage, DrawingSnapshot::kRequiredPixels));
    if (snapshot == nullptr || !snapshot->valid()) {
      return;
    }
    snapshot->initialize_blank();

    SnapshotHeader header;
    const bool header_loaded =
        esp_partition_read(partition, 0, &header, sizeof(header)) == ESP_OK && valid_header(header);
    if (header_loaded) {
      for (std::size_t sector = 0; sector < DrawingSnapshot::kSectorCount; ++sector) {
        if (esp_partition_read(partition, kDataOffset + sector * DrawingSnapshot::kSectorBytes,
                               sector_buffer, DrawingSnapshot::kSectorBytes) != ESP_OK ||
            !snapshot->load_sector(sector,
                                   std::span(sector_buffer, DrawingSnapshot::kSectorPixels))) {
          return;
        }
      }
      snapshot->load_origin({header.origin_x, header.origin_y});
    } else {
      if (esp_partition_erase_range(partition, 0, kRequiredPartitionBytes) != ESP_OK ||
          !write_header(make_header(snapshot->origin()))) {
        return;
      }
    }

    last_activity_us = static_cast<std::uint32_t>(esp_timer_get_time());
    ready = xTaskCreatePinnedToCore(save_task_entry, "tinydraw_save", 6144U, this, 1U, &task, 0) ==
            pdPASS;
  }

  ~Impl() {
    if (task != nullptr) {
      vTaskDelete(task);
    }
    delete snapshot;
    heap_caps_free(sector_buffer);
    heap_caps_free(snapshot_storage);
  }

  bool write_header(const SnapshotHeader& header) {
    std::fill_n(sector_buffer, DrawingSnapshot::kSectorPixels, 0xFFFFU);
    std::memcpy(sector_buffer, &header, sizeof(header));
    return esp_partition_erase_range(partition, 0, kHeaderBytes) == ESP_OK &&
           esp_partition_write(partition, 0, sector_buffer, kHeaderBytes) == ESP_OK;
  }

  void note_activity() {
    if (!ready) {
      return;
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    last_activity_us = static_cast<std::uint32_t>(esp_timer_get_time());
    xSemaphoreGive(mutex);
  }

  void request_save() {
    last_activity_us = static_cast<std::uint32_t>(esp_timer_get_time());
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
          if (snapshot->sector_pending(candidate)) {
            sector = candidate;
            break;
          }
        }
        if (sector < DrawingSnapshot::kSectorCount) {
          static_cast<void>(snapshot->copy_sector(
              sector, std::span(sector_buffer, DrawingSnapshot::kSectorPixels)));
          xSemaphoreGive(mutex);
          const bool written =
              esp_partition_erase_range(partition,
                                        kDataOffset + sector * DrawingSnapshot::kSectorBytes,
                                        DrawingSnapshot::kSectorBytes) == ESP_OK &&
              esp_partition_write(partition, kDataOffset + sector * DrawingSnapshot::kSectorBytes,
                                  sector_buffer, DrawingSnapshot::kSectorBytes) == ESP_OK;
          xSemaphoreTake(mutex, portMAX_DELAY);
          if (written && snapshot->sector_matches(
                             sector, std::span<const std::uint16_t>(
                                         sector_buffer, DrawingSnapshot::kSectorPixels))) {
            snapshot->acknowledge_sector(sector);
            ++sectors_written;
          }
          xSemaphoreGive(mutex);
          if (!written) {
            vTaskDelay(pdMS_TO_TICKS(100));
          }
          continue;
        }

        if (snapshot->metadata_pending()) {
          const SnapshotHeader header = make_header(snapshot->origin());
          xSemaphoreGive(mutex);
          const bool written = write_header(header);
          xSemaphoreTake(mutex, portMAX_DELAY);
          if (written && snapshot->origin() == ViewOrigin{header.origin_x, header.origin_y}) {
            snapshot->acknowledge_metadata();
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
  std::uint16_t* snapshot_storage = nullptr;
  std::uint16_t* sector_buffer = nullptr;
  DrawingSnapshot* snapshot = nullptr;
  StaticSemaphore_t mutex_storage{};
  SemaphoreHandle_t mutex = nullptr;
  TaskHandle_t task = nullptr;
  std::uint32_t last_activity_us = 0;
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
  return world.replace(
      std::span<const std::uint16_t>(impl_->snapshot_storage, DrawingSnapshot::kRequiredPixels),
      impl_->snapshot->origin(), committed, visible);
}

void DrawingStore::activity() {
  if (ready()) {
    impl_->note_activity();
  }
}

void DrawingStore::include_segment(Point from, Point to, float radius, ViewOrigin origin) {
  if (!ready()) {
    return;
  }
  xSemaphoreTake(impl_->mutex, portMAX_DELAY);
  impl_->last_activity_us = static_cast<std::uint32_t>(esp_timer_get_time());
  impl_->snapshot->include_segment(from, to, radius, origin);
  xSemaphoreGive(impl_->mutex);
}

void DrawingStore::save_stroke(WorldCanvas& world, std::span<const std::uint16_t> viewport) {
  if (!ready() || viewport.size() < WorldCanvas::kViewportPixels) {
    return;
  }
  xSemaphoreTake(impl_->mutex, portMAX_DELAY);
  const ViewOrigin origin = world.origin();
  for (std::size_t tile = 0; tile < DrawingSnapshot::kTileCount; ++tile) {
    if (!impl_->snapshot->tile_included(tile)) {
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
  static_cast<void>(impl_->snapshot->capture(world.pixels(), origin));
  impl_->request_save();
  xSemaphoreGive(impl_->mutex);
}

void DrawingStore::save_viewport(WorldCanvas& world, std::span<const std::uint16_t> viewport) {
  if (!ready() || !world.capture(viewport)) {
    return;
  }
  xSemaphoreTake(impl_->mutex, portMAX_DELAY);
  impl_->snapshot->include_viewport(world.origin());
  static_cast<void>(impl_->snapshot->capture(world.pixels(), world.origin()));
  impl_->request_save();
  xSemaphoreGive(impl_->mutex);
}

void DrawingStore::save_all(WorldCanvas& world) {
  if (!ready()) {
    return;
  }
  xSemaphoreTake(impl_->mutex, portMAX_DELAY);
  impl_->snapshot->include_all();
  static_cast<void>(impl_->snapshot->capture(world.pixels(), world.origin()));
  impl_->request_save();
  xSemaphoreGive(impl_->mutex);
}

void DrawingStore::save_origin(const WorldCanvas& world) {
  if (!ready()) {
    return;
  }
  xSemaphoreTake(impl_->mutex, portMAX_DELAY);
  static_cast<void>(impl_->snapshot->capture(world.pixels(), world.origin()));
  impl_->request_save();
  xSemaphoreGive(impl_->mutex);
}

}  // namespace tinydraw::esp32
