#include "tinydraw/production/operation_lod_store.h"

#include <algorithm>
#include <limits>

#include "tinydraw/production/storage_overlap.h"

namespace tinydraw::production {
namespace {

bool valid_sample(CompactLodSample sample) {
  return sample.x_quarter <= kWorldWidth * 4 && sample.y_quarter <= kWorldHeight * 4 &&
         sample.radius_256 != 0U;
}

}  // namespace

PreparedLodAppend::PreparedLodAppend(OperationLodStore& owner, OperationIdentity identity,
                                     std::uint32_t token)
    : owner_(&owner), identity_(identity), token_(token) {}

PreparedLodAppend::~PreparedLodAppend() { cancel(); }

PreparedLodAppend::PreparedLodAppend(PreparedLodAppend&& other) noexcept
    : owner_(other.owner_), identity_(other.identity_), token_(other.token_) {
  other.owner_ = nullptr;
  other.identity_ = {};
  other.token_ = 0;
}

PreparedLodAppend& PreparedLodAppend::operator=(PreparedLodAppend&& other) noexcept {
  if (this != &other) {
    cancel();
    owner_ = other.owner_;
    identity_ = other.identity_;
    token_ = other.token_;
    other.owner_ = nullptr;
    other.identity_ = {};
    other.token_ = 0;
  }
  return *this;
}

OperationIdentity PreparedLodAppend::identity() const { return identity_; }

void PreparedLodAppend::publish() {
  if (owner_ != nullptr) {
    owner_->publish_prepared(*this);
    owner_ = nullptr;
    identity_ = {};
    token_ = 0;
  }
}

void PreparedLodAppend::cancel() {
  if (owner_ != nullptr) {
    owner_->cancel_prepared(*this);
    owner_ = nullptr;
    identity_ = {};
    token_ = 0;
  }
}

OperationLodStore::OperationLodStore(std::span<LodSpan> spans, std::span<CompactLodSample> samples)
    : spans_(spans), samples_(samples) {
  if (spans_.size() % kLodZoomCount == 0U) {
    operation_capacity_ = spans_.size() / kLodZoomCount;
  }
}

bool OperationLodStore::ready() const {
  return operation_capacity_ != 0U &&
         operation_capacity_ <= std::numeric_limits<std::uint32_t>::max() && !samples_.empty() &&
         samples_.size() <= std::numeric_limits<std::uint32_t>::max() &&
         spans_.size() == operation_capacity_ * kLodZoomCount &&
         !storage_overlaps(std::as_bytes(std::span(spans_)), std::as_bytes(std::span(samples_)));
}

OperationLogEpoch OperationLodStore::epoch() const { return epoch_; }

DocumentRevision OperationLodStore::current_revision() const { return revision_; }

std::size_t OperationLodStore::operation_count() const { return operation_count_; }

std::size_t OperationLodStore::sample_count() const { return sample_count_; }

std::size_t OperationLodStore::operation_capacity() const { return operation_capacity_; }

std::size_t OperationLodStore::sample_capacity() const { return samples_.size(); }

bool OperationLodStore::can_reset() const { return !append_pending_; }

std::optional<PreparedLodAppend> OperationLodStore::prepare(const OperationLodAppend& append) {
  if (!ready() || append_pending_ || append.epoch != epoch_ ||
      append.identity.revision.value != revision_.value + 1U ||
      append.identity.operation_index != operation_count_ ||
      operation_count_ >= operation_capacity_ ||
      revision_.value == std::numeric_limits<std::uint32_t>::max()) {
    return std::nullopt;
  }
  std::size_t required = 0;
  for (const auto zoom_samples : append.zoom_samples) {
    const auto source = std::as_bytes(zoom_samples);
    if (zoom_samples.empty() || storage_overlaps(source, std::as_bytes(std::span(spans_))) ||
        storage_overlaps(source, std::as_bytes(std::span(samples_))) ||
        !std::all_of(zoom_samples.begin(), zoom_samples.end(), valid_sample) ||
        zoom_samples.size() > std::numeric_limits<std::uint16_t>::max() ||
        zoom_samples.size() > samples_.size() - required) {
      return std::nullopt;
    }
    required += zoom_samples.size();
  }
  if (required > samples_.size() - sample_count_) {
    return std::nullopt;
  }

  std::size_t destination = sample_count_;
  for (std::size_t zoom = 0; zoom < kLodZoomCount; ++zoom) {
    const auto zoom_samples = append.zoom_samples[zoom];
    pending_spans_[zoom] = {.first_sample = static_cast<std::uint32_t>(destination),
                            .sample_count = static_cast<std::uint16_t>(zoom_samples.size())};
    std::copy(zoom_samples.begin(), zoom_samples.end(),
              samples_.begin() + static_cast<std::ptrdiff_t>(destination));
    destination += zoom_samples.size();
  }
  pending_token_ = next_prepare_token_++;
  if (next_prepare_token_ == 0U) {
    next_prepare_token_ = 1U;
  }
  append_pending_ = true;
  return PreparedLodAppend(*this, append.identity, pending_token_);
}

std::optional<StoredOperationLod> OperationLodStore::lod(OperationLogEpoch requested_epoch,
                                                         OperationIdentity identity,
                                                         ZoomLevel zoom) const {
  const auto zoom_slot = zoom_index(zoom);
  if (!ready() || append_pending_ || requested_epoch != epoch_ || !zoom_slot.has_value() ||
      identity.operation_index >= operation_count_ ||
      identity.revision.value != base_revision_.value + identity.operation_index + 1U) {
    return std::nullopt;
  }
  const LodSpan span = spans_[span_index(*zoom_slot, identity.operation_index)];
  if (span.first_sample > sample_count_ || span.sample_count > sample_count_ - span.first_sample) {
    return std::nullopt;
  }
  return StoredOperationLod{.epoch = epoch_,
                            .identity = identity,
                            .zoom = zoom,
                            .samples = samples_.subspan(span.first_sample, span.sample_count)};
}

bool OperationLodStore::reset(OperationLogEpoch new_epoch, DocumentRevision revision) {
  if (append_pending_) {
    return false;
  }
  operation_count_ = 0;
  sample_count_ = 0;
  epoch_ = new_epoch;
  base_revision_ = revision;
  revision_ = revision;
  pending_token_ = 0;
  pending_spans_.fill({});
  return true;
}

void OperationLodStore::publish_prepared(const PreparedLodAppend& prepared) {
  if (!append_pending_ || prepared.token_ != pending_token_) {
    return;
  }
  for (std::size_t zoom = 0; zoom < kLodZoomCount; ++zoom) {
    spans_[span_index(zoom, operation_count_)] = pending_spans_[zoom];
    sample_count_ += pending_spans_[zoom].sample_count;
  }
  ++operation_count_;
  revision_ = prepared.identity_.revision;
  append_pending_ = false;
  pending_token_ = 0;
  pending_spans_.fill({});
}

void OperationLodStore::cancel_prepared(const PreparedLodAppend& prepared) {
  if (!append_pending_ || prepared.token_ != pending_token_) {
    return;
  }
  append_pending_ = false;
  pending_token_ = 0;
  pending_spans_.fill({});
}

std::optional<std::size_t> OperationLodStore::zoom_index(ZoomLevel zoom) {
  const auto found = std::find(kLodZooms.begin(), kLodZooms.end(), zoom);
  if (found == kLodZooms.end()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(found - kLodZooms.begin());
}

std::size_t OperationLodStore::span_index(std::size_t zoom, std::size_t operation) const {
  return zoom * operation_capacity_ + operation;
}

}  // namespace tinydraw::production
