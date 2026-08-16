# Cold Stage B — session receipt (2026-08-16)

Machine: ESP32-S3 `/dev/cu.usbmodem101`, gate harness, three-run captures per
variant (within-build spread ≤3 µs on strip staging, ≤1.5 ms on cold walls).
Logs in `logs/`. Corpus: frozen `adversarial_tapered_4x+evil_hairlines`.

## Landed

**B1 — strided publish** (commit `f2f6da7`): publish straight from the
supertask surface; `packed_tile_pixels` staging removed from the producer
workspace (harness keeps a private census/edge-ink scratch). All gates held,
verdict vector identical to A.

**B2 — O(1) slot metadata** (candidate, NOT committed; full diff in
`cold-stage-b2-metadata-directory.patch`): per-identity raw-slot directory
(27,384 B, optional caller span, empty = legacy scans, host-debug parity
assert), occupied-slot count (O(1) `resident_raw_tiles`, full-pool
`choose_slot` skip), all occupancy flips centralized in
`release_slot`/`claim_slot`.

## Cold compute, three-run max (µs)

| zoom | A (HEAD e76b98e) | B1 | B1+B2 best | Δ total |
|---|---|---|---|---|
| 50  | 434,462 | 422,719 | 396,296 | −38.2 ms |
| 100 | 468,006 | 455,411 | 429,529 | −38.5 ms |
| 200 | 598,516 | 588,045 | 561,984 | −36.5 ms |
| 400 | 586,606 | 577,454 | 547,928 | −38.7 ms |

Walls at B1+B2: 50% **482.7 ms** (under the ≤500 contract line), 100%
513.1, 200% 651.4, 400% 635.5. Standing guards held on every run of every
variant: cache-tour ledger `amplification=1.000 stale=0 unexplained=0`, all
five `TINYDRAW_INKTRACE pass=1`.

## The B2 blocker: pan_seq strip-8 wire budget

`TINYDRAW_PANSEQ_STRIP` enforces per-strip CPU staging <
`rows × width / 10` µs (20 bytes/µs QSPI payload rate,
`co5300_panel_transport.cpp:730`). It is a **pipelining invariant** (staging
must outrun the wire so queued DMA hides it), not a directly demonstrated
glass tear. Strip 8 (20 rows, budget 736 µs) has the least absolute
headroom; whole-frame pacing (`pacing_pass`) stayed green in every variant.

z100 strip-8 staging (mean / max, µs) across builds:

| variant | mean | max | pan_seq |
|---|---|---|---|
| A baseline | 660 | 694 | green |
| B1 | 661–663 | 699–704 | green |
| B2 internal-mid + device assert | 716–721 | 779–782 | red |
| B2 internal-mid | 707–710 | 745–751 | red |
| B2 PSRAM-mid | 702 | 741–742 | red |
| B2 PSRAM-mid, directory span DISABLED (probe) | 694–695 | 732–735 | green (margin 1–4 µs) |
| B2 PSRAM-tail | 701–703 | 734–738 | 1 red / 3 |
| B2 + per-publish `esp_cache_msync` hook | 705–711 | 750–762 | red (and cost ~12 ms cold) |
| B2 directory-only (count/skip reverted to scans) | 712–716 | 754–762 | red |
| B2 PSRAM-tail padded to 32 KiB | 695–700 | 732–739 | 1 red / 3 |

One additional data point: a raw `Cache_WriteBack_All()` before the strip
sweep dropped the mean to **644** (below baseline) but hit interrupt-WDT
panics — unsafe, reverted. It confirms dirty-line writeback participates,
but source-flushing publishes did not recover it, and neither did restoring
the slot-scan "cache rinse", moving the allocation (internal / PSRAM-mid /
PSRAM-tail), nor 32 KiB way-size padding.

**Empirical law after 8 variants:** every B2-family build pays a uniform
+35–55 µs per-strip staging elevation across ALL strips (~4%); the two
non-B2 builds sit at 660±3. B1's strip-8 headroom was only ~35–40 µs, so B2
turns the gate into a coin flip or worse. Device-assert removal, placement,
and padding each shaved a few µs; nothing recovered the baseline.

## Open decision (owner)

1. **Park B2**, proceed to B3/B4; revisit with transport-level work
   (DMA-fed staging or prefetch) or after understanding the staging tax.
2. **Accept-and-fix forward**: land B2 given whole-frame pacing stays green,
   with a work order on the staging pipeline; requires an explicit owner
   ruling on the strip wire-budget contract (it currently gates the verdict).
3. **Keep grinding variants** (not recommended: 8 tried, diminishing).

Note for B3 (H7 chord sweep): if the staging tax is a general
"producer-memory-behavior during pan frames" coupling, B3 may trip the same
razor; B1 did not, so it is not universal. Measure pan_seq strip 8 on the
first B3 flash.
