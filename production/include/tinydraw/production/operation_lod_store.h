#ifndef TINYDRAW_PRODUCTION_OPERATION_LOD_STORE_H
#define TINYDRAW_PRODUCTION_OPERATION_LOD_STORE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/production/operation_log.h"

namespace tinydraw::production {

inline constexpr std::size_t kLodZoomCount = 4;
inline constexpr std::array<ZoomLevel, kLodZoomCount> kLodZooms{
    ZoomLevel::k50Percent,
    ZoomLevel::k100Percent,
    ZoomLevel::k200Percent,
    ZoomLevel::k400Percent,
};

struct CompactLodSample {
  std::uint16_t x_quarter = 0;
  std::uint16_t y_quarter = 0;
  std::uint16_t radius_256 = 0;
  bool operator==(const CompactLodSample&) const = default;
};

struct LodSpan {
  std::uint32_t first_sample = 0;
  std::uint16_t sample_count = 0;
  std::uint16_t flags = 0;
};

struct OperationLodAppend {
  OperationLogEpoch epoch{};
  OperationIdentity identity{};
  std::array<std::span<const CompactLodSample>, kLodZoomCount> zoom_samples{};
};

struct StoredOperationLod {
  OperationLogEpoch epoch{};
  OperationIdentity identity{};
  ZoomLevel zoom = ZoomLevel::k50Percent;
  std::span<const CompactLodSample> samples{};
};

class OperationLodStore;

// Move-only preparation for one operation's four committed zoom LODs. It must
// not outlive its owning store. Samples are copied into unused caller storage
// during prepare, while publish is an infallible authority change. Destruction
// cancels an unpublished preparation.
class PreparedLodAppend {
 public:
  ~PreparedLodAppend();
  PreparedLodAppend(const PreparedLodAppend&) = delete;
  PreparedLodAppend& operator=(const PreparedLodAppend&) = delete;
  PreparedLodAppend(PreparedLodAppend&& other) noexcept;
  PreparedLodAppend& operator=(PreparedLodAppend&& other) noexcept;

  [[nodiscard]] OperationIdentity identity() const;
  void publish();
  void cancel();

 private:
  friend class OperationLodStore;
  PreparedLodAppend(OperationLodStore& owner, OperationIdentity identity, std::uint32_t token);

  OperationLodStore* owner_ = nullptr;
  OperationIdentity identity_{};
  std::uint32_t token_ = 0;
};

// Fixed-capacity append-time ownership for zoom-specific simplified centerline
// samples. Span metadata and samples are caller-owned and must outlive the
// store. The span storage contains four equal zoom tables in kLodZooms order.
// Callers must serialize every method with the matching OperationLog.
class OperationLodStore {
 public:
  OperationLodStore(std::span<LodSpan> spans, std::span<CompactLodSample> samples);

  [[nodiscard]] bool ready() const;
  [[nodiscard]] OperationLogEpoch epoch() const;
  [[nodiscard]] DocumentRevision current_revision() const;
  [[nodiscard]] std::size_t operation_count() const;
  [[nodiscard]] std::size_t sample_count() const;
  [[nodiscard]] std::size_t operation_capacity() const;
  [[nodiscard]] std::size_t sample_capacity() const;
  [[nodiscard]] bool can_reset() const;

  // Identity must be the exact next operation from the matching log epoch.
  // Every committed zoom requires at least one valid sample. Preparation is
  // atomic with respect to visible counts and queries.
  [[nodiscard]] std::optional<PreparedLodAppend> prepare(const OperationLodAppend& append);
  [[nodiscard]] std::optional<StoredOperationLod> lod(OperationLogEpoch requested_epoch,
                                                      OperationIdentity identity,
                                                      ZoomLevel zoom) const;
  // Discards all LODs and adopts a new matching log snapshot identity. The
  // epoch must differ from the current epoch so stale query identities cannot
  // become valid for replacement content.
  [[nodiscard]] bool reset(OperationLogEpoch new_epoch, DocumentRevision revision);

 private:
  friend class PreparedLodAppend;
  void publish_prepared(const PreparedLodAppend& prepared);
  void cancel_prepared(const PreparedLodAppend& prepared);
  [[nodiscard]] static std::optional<std::size_t> zoom_index(ZoomLevel zoom);
  [[nodiscard]] std::size_t span_index(std::size_t zoom, std::size_t operation) const;

  std::span<LodSpan> spans_;
  std::span<CompactLodSample> samples_;
  std::array<LodSpan, kLodZoomCount> pending_spans_{};
  std::size_t operation_capacity_ = 0;
  std::size_t operation_count_ = 0;
  std::size_t sample_count_ = 0;
  OperationLogEpoch epoch_{};
  DocumentRevision base_revision_{};
  DocumentRevision revision_{};
  std::uint32_t next_prepare_token_ = 1;
  std::uint32_t pending_token_ = 0;
  bool append_pending_ = false;
};

static_assert(sizeof(CompactLodSample) == 6);
static_assert(sizeof(LodSpan) == 8);

}  // namespace tinydraw::production

#endif  // TINYDRAW_PRODUCTION_OPERATION_LOD_STORE_H
