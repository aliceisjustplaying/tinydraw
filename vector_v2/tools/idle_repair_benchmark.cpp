#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <unordered_set>
#include <vector>

#include "tinydraw/vector_v2/idle_repair.h"
#include "tinydraw/vector_v2/memory_layout.h"
#include "tinydraw/vector_v2/navigation_state.h"

namespace v2 = tinydraw::vector_v2;

namespace {

constexpr std::size_t kRepairTileBudget = 64U;

std::uint32_t key_id(v2::TileKey key) {
  return (static_cast<std::uint32_t>(key.zoom) << 24U) |
         (static_cast<std::uint32_t>(key.row) << 12U) | key.column;
}

std::vector<v2::TileKey> visible_keys(const v2::ViewRequest& view) {
  std::vector<v2::TileKey> keys;
  const int first_column = view.level_pixels.x0 / v2::kTileWidth;
  const int last_column = (view.level_pixels.x1 - 1) / v2::kTileWidth;
  const int first_row = view.level_pixels.y0 / v2::kTileHeight;
  const int last_row = (view.level_pixels.y1 - 1) / v2::kTileHeight;
  for (int row = first_row; row <= last_row; ++row) {
    for (int column = first_column; column <= last_column; ++column) {
      keys.push_back(
          {view.zoom, static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(row)});
    }
  }
  return keys;
}

v2::ViewRequest shifted(v2::ViewRequest view, int dx, int dy) {
  const auto origin = v2::NavigationState::clamp_origin(view.zoom, view.level_pixels.x0 + dx,
                                                        view.level_pixels.y0 + dy);
  view.level_pixels = {origin.x, origin.y, origin.x + v2::kOverviewWidth,
                       origin.y + v2::kOverviewHeight};
  return view;
}

struct Rig {
  std::vector<std::uint16_t> overview = std::vector<std::uint16_t>(v2::kOverviewPixels, 0xFFFFU);
  std::vector<v2::MaterializedUniformStorage> uniforms =
      std::vector<v2::MaterializedUniformStorage>(v2::kMaterializedTileIdentityCount);
  std::vector<std::uint8_t> occupancy = std::vector<std::uint8_t>(v2::kOccupancyBytes);
  std::vector<v2::MaterializedSlotStorage> slots =
      std::vector<v2::MaterializedSlotStorage>(v2::kTileSlotCount);
  std::vector<std::uint16_t> tile_pixels =
      std::vector<std::uint16_t>(v2::kTileSlotCount * v2::kTilePixels);
  std::vector<std::uint16_t> directory =
      std::vector<std::uint16_t>(v2::kMaterializedTileIdentityCount);
  std::array<std::uint16_t, v2::kTilePixels> tile{};
  std::vector<std::uint16_t> composed = std::vector<std::uint16_t>(v2::kOverviewPixels);
  v2::MaterializedCanvas canvas{overview, uniforms, occupancy, slots, tile_pixels, {1}, directory};

  bool ready() { return canvas.ready() && canvas.publish_overview({1}, overview); }

  bool resident(v2::TileKey key) const {
    const auto source = canvas.lookup(key);
    return source.has_value() && source->kind == v2::SourceKind::kTileSlot;
  }

  bool publish(v2::TileKey key) {
    return canvas.publish_tile(key, {1}, v2::MaterializationQuality::kImmediate, std::span(tile))
        .has_value();
  }

  bool visit(const v2::ViewRequest& view) {
    return canvas.remember_view(view) && canvas.compose_view(view, composed).has_value();
  }
};

struct PolicyPlan {
  std::array<v2::ViewRequest, 4> views{};
  std::size_t count = 0U;
};

PolicyPlan fixed_runway(v2::ViewRequest active, int distance) {
  PolicyPlan plan;
  const std::array<std::array<int, 2>, 4> offsets{
      {{-distance, 0}, {distance, 0}, {0, -distance}, {0, distance}}};
  for (const auto offset : offsets) {
    const auto view = shifted(active, offset[0], offset[1]);
    if (view.level_pixels != active.level_pixels) {
      plan.views[plan.count++] = view;
    }
  }
  return plan;
}

PolicyPlan prefix(const v2::IdleRepairPlan& source) {
  PolicyPlan plan;
  plan.count = std::min(plan.views.size(), source.count);
  std::copy_n(source.views.begin(), plan.count, plan.views.begin());
  return plan;
}

PolicyPlan directional(v2::ViewRequest active, int dx, int dy) {
  return prefix(v2::plan_idle_repair(active, {}, {.x = dx, .y = dy}));
}

struct Metrics {
  std::size_t repair_published = 0U;
  std::size_t repair_views_completed = 0U;
  std::size_t reused = 0U;
  std::size_t evicted_before_use = 0U;
  std::size_t evicted_unused = 0U;
  std::size_t never_viewed = 0U;
  std::size_t trace_refills = 0U;
  std::size_t refill_avoided = 0U;
  double repair_wall_ms = 0.0;
};

bool fill_view(Rig& rig, const v2::ViewRequest& view, std::vector<v2::TileKey>* published,
               std::size_t* count) {
  for (const auto key : visible_keys(view)) {
    if (rig.resident(key)) {
      continue;
    }
    if (!rig.publish(key)) {
      return false;
    }
    if (published != nullptr) {
      published->push_back(key);
    }
    if (count != nullptr) {
      ++*count;
    }
  }
  return true;
}

bool fill_repair_view(Rig& rig, const v2::ViewRequest& view, std::vector<v2::TileKey>& published,
                      std::size_t& count, bool& complete) {
  complete = true;
  for (const auto key : visible_keys(view)) {
    if (rig.resident(key)) {
      continue;
    }
    if (count == kRepairTileBudget) {
      complete = false;
      break;
    }
    if (!rig.publish(key)) {
      return false;
    }
    published.push_back(key);
    ++count;
  }
  return true;
}

std::vector<v2::ViewRequest> directional_trace(v2::ViewRequest active) {
  std::vector<v2::ViewRequest> trace;
  for (int step = 1; step <= 6; ++step) {
    trace.push_back(shifted(active, step * 64, 0));
  }
  return trace;
}

std::vector<v2::ViewRequest> reverse_trace(v2::ViewRequest active) {
  return {shifted(active, -64, 0), active};
}

std::vector<v2::ViewRequest> random_trace(v2::ViewRequest active) {
  constexpr std::array moves{std::array{0, 64},  std::array{64, 0},  std::array{0, -64},
                             std::array{-64, 0}, std::array{-64, 0}, std::array{0, 64},
                             std::array{64, 0},  std::array{64, 0},  std::array{0, 64},
                             std::array{0, -64}, std::array{-64, 0}, std::array{0, -64}};
  std::vector<v2::ViewRequest> trace;
  auto view = active;
  for (const auto move : moves) {
    view = shifted(view, move[0], move[1]);
    trace.push_back(view);
  }
  return trace;
}

std::vector<v2::ViewRequest> random_navigation_trace(v2::ViewRequest active) {
  constexpr std::size_t kStops = 24U;
  const int maximum_x = v2::kWorldWidth * 4 - v2::kOverviewWidth;
  const int maximum_y = v2::kWorldHeight * 4 - v2::kOverviewHeight;
  std::uint32_t state = 0xC001CAFEU;
  const auto next = [&state]() {
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
  };
  std::vector<v2::ViewRequest> trace;
  for (std::size_t stop = 0; stop < kStops; ++stop) {
    const int x = static_cast<int>(next() % static_cast<std::uint32_t>(maximum_x + 1));
    const int y = static_cast<int>(next() % static_cast<std::uint32_t>(maximum_y + 1));
    trace.push_back(shifted(active, x - active.level_pixels.x0, y - active.level_pixels.y0));
  }
  return trace;
}

std::size_t no_repair_refills(v2::ViewRequest active, const std::vector<v2::ViewRequest>& trace) {
  Rig rig;
  if (!rig.ready() || !fill_view(rig, active, nullptr, nullptr) || !rig.visit(active)) {
    return 0U;
  }
  std::size_t refills = 0U;
  for (const auto& view : trace) {
    if (!fill_view(rig, view, nullptr, &refills) || !rig.visit(view)) {
      return 0U;
    }
  }
  return refills;
}

Metrics measure(v2::ViewRequest active, const PolicyPlan& plan,
                const std::vector<v2::ViewRequest>& trace) {
  Metrics metrics;
  Rig rig;
  if (!rig.ready() || !fill_view(rig, active, nullptr, nullptr) || !rig.visit(active)) {
    return metrics;
  }

  std::vector<v2::TileKey> repaired;
  const auto repair_started = std::chrono::steady_clock::now();
  for (std::size_t index = 0; index < plan.count; ++index) {
    bool complete = false;
    if (!fill_repair_view(rig, plan.views[index], repaired, metrics.repair_published, complete)) {
      return {};
    }
    metrics.repair_views_completed += complete ? 1U : 0U;
    if (!complete) {
      break;
    }
  }
  metrics.repair_wall_ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - repair_started)
          .count();

  std::unordered_set<std::uint32_t> repaired_ids;
  for (const auto key : repaired) {
    repaired_ids.insert(key_id(key));
  }
  std::unordered_set<std::uint32_t> reused;
  std::unordered_set<std::uint32_t> evicted;
  std::unordered_set<std::uint32_t> viewed;
  for (const auto& view : trace) {
    for (const auto key : visible_keys(view)) {
      const auto id = key_id(key);
      if (repaired_ids.contains(id)) {
        viewed.insert(id);
        if (rig.resident(key)) {
          reused.insert(id);
        } else {
          evicted.insert(id);
        }
      }
    }
    if (!fill_view(rig, view, nullptr, &metrics.trace_refills) || !rig.visit(view)) {
      return {};
    }
  }
  metrics.reused = reused.size();
  metrics.evicted_before_use = evicted.size();
  metrics.never_viewed = repaired_ids.size() - viewed.size();
  for (const auto key : repaired) {
    const auto id = key_id(key);
    metrics.evicted_unused += !viewed.contains(id) && !rig.resident(key) ? 1U : 0U;
  }
  const std::size_t cold = no_repair_refills(active, trace);
  metrics.refill_avoided = cold >= metrics.trace_refills ? cold - metrics.trace_refills : 0U;
  return metrics;
}

void print(const char* policy, const char* trace, const Metrics& metrics) {
  std::printf(
      "IDLE_REPAIR_BENCH policy=%s trace=%s budget=%zu repair_views=%zu repair_tiles=%zu "
      "repair_wall_ms=%.3f reused=%zu evicted_before_use=%zu evicted_unused=%zu never_viewed=%zu "
      "trace_refills=%zu refill_avoided=%zu\n",
      policy, trace, kRepairTileBudget, metrics.repair_views_completed, metrics.repair_published,
      metrics.repair_wall_ms, metrics.reused, metrics.evicted_before_use, metrics.evicted_unused,
      metrics.never_viewed, metrics.trace_refills, metrics.refill_avoided);
}

}  // namespace

int main() {
  const v2::ViewRequest active{
      .zoom = v2::ZoomLevel::k400Percent,
      .level_pixels = {2'760, 3'360, 2'760 + v2::kOverviewWidth, 3'360 + v2::kOverviewHeight},
  };
  const auto forward = directional_trace(active);
  const auto reverse = reverse_trace(active);
  const auto random = random_trace(active);
  const auto navigation = random_navigation_trace(active);
  const auto current = prefix(v2::plan_idle_repair(active, {}));
  const auto runway64 = fixed_runway(active, 64);
  const auto hinted = directional(active, 1, 0);

  for (const auto& policy :
       std::array{std::pair{"current", current}, std::pair{"runway64", runway64},
                  std::pair{"hinted", hinted}}) {
    print(policy.first, "forward", measure(active, policy.second, forward));
    print(policy.first, "reverse", measure(active, policy.second, reverse));
    print(policy.first, "random-walk", measure(active, policy.second, random));
    print(policy.first, "random-nav", measure(active, policy.second, navigation));
  }
  return 0;
}
