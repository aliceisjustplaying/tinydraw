# The TinyDraw V2 performance chronicle

A running, receipt-backed log of every performance win on the road from
"architecturally accepted" to "feels right on glass." Sources are the
receipt files in [`benchmark-results/`](../benchmark-results/) — every
number below has a device log behind it. Hardware throughout: ESP32-S3 at
240 MHz, 8 MiB octal PSRAM, CO5300 368×448 RGB565 panel at 40 MHz
effective (10 Mpixel/s wire).

## 1. Cold rendering: 1,269 ms → ~500 ms (2.5×)

The frozen adversarial corpus (910 operations, 12,157 samples of tapered
strokes + "evil hairlines") rebuilt from scratch at 400% zoom:

| Milestone | 400% wall | What moved |
|---|---|---|
| Wave-2 baseline | **1,269.157 ms** | starting point (compute 1,165 ms) |
| Wave 3 | **668.980 ms** | stateless windowed span search, device-native arithmetic (no per-row float libcalls), internal-SRAM producer scratch, once-per-endpoint prepared curve units, unit-merged masked row sweeps, caller-split painters (−47.3%) |
| Cold Stage B | **507.0 ms** | strided publish straight from the supertask surface (−10 ms), H7 op-level chord sweep — one y-sorted row pass per operation (−120 ms), O(1) raw-slot metadata directory (−26 ms), honest work-budget slices |
| Today | 503–518 ms | held inside the owner-accepted 520 ms hold-the-line ceiling through five feature landings |

50/100/200% finished under the ≤500 ms product line at Stage B
(437.9/428.4/488.0 ms). Equally important were the *rejected* experiments
with receipts: 4-sample SSAA (808 ms — dead), word-mask window scans
(net +4–5 ms after H7 — windows too short), scanline recurrence (−3.3%),
publication batching (1.6%), flat row-count slice budgets (blew the idle
step contract).

Two measurement laws were paid for in blood: flash-icache layout moves
hot-loop timing ±2–3% per build (fixed for presentation by IRAM-pinning
the transport via linker fragment; still open for the producer), and heap
allocation order shifts PSRAM dcache sets (a 40 KiB workspace placed
mid-heap cost +9 ms cold; placed last, 0 ms).

## 2. Ink commit latency: 19,324 µs → 173 µs (111×)

The mixed-draw gate commits interactive 32-sample chunks over a fully
warm multi-zoom cache. Worst chunk commit on the input path:

| Milestone | worst append | mechanism |
|---|---|---|
| Attributed | **19,324 µs** | synchronous in-place commit: 12 ms visible raw painting + 5 ms uniform materialization + overview replay, all inside the input poll |
| Committed overlay | **173 µs** | commits publish *authority only*; the canvas absorbs in idle slices behind a pending-ink overlay proven bit-exact on host (composed prefix + overlay == full replay, 564 assertions) |

The 15 ms budget gate (`mixed_draw`) went green for the first time in
project history. The work did not disappear — it moved into receipted
idle absorptions (≤25–30 ms each, between input polls) with a high-water
fallback that bounds the pending range at 24 operations.

## 3. The lift hitch: 87–199 ms → 4–5 ms (~30×)

Post-lift next-poll delay (`detected_to_poll_start_us`):

| Milestone | lift tail |
|---|---|
| Pre-overlay glass session | 87–199 ms (synchronous drain + 72–80 ms full refresh) |
| Overlay landed | 10–34 ms (deferred swap refresh; tail = first drain slice riding the lift iteration) |
| Drain gated to empty polls | **4–5 ms typical** |

The one remaining spike class was attributed to the *battery chrome
refresh* (a cosmetic full-frame present, 60–140 ms on dense content)
landing in the lift window — fixed by deferring it and then by
re-presenting only the 124×44 battery region.

## 4. Déjà-vu: 188–326 ms of revisit re-rendering → 0.4 ms (~500×)

Draw at every zoom over a warm multi-zoom cache, then revisit each zoom:

| zoom | missing tiles before | after | refill before | after |
|---|---|---|---|---|
| 50 | 4 | **0** | 188.0 ms | **0.38 ms** |
| 100 | 9 | **0** | 319.8 ms | **0.38 ms** |
| 200 | 16 | **0** | 326.3 ms | **0.37 ms** |

Mechanism: the overlay made cross-zoom retention affordable — idle
absorption now repaints affected resident tiles at *every* zoom in place
(no slot cost) and materializes affected fresh-paper tiles inside the
remembered viewports (exactly the revisit population), under a 25 ms idle
budget. It took three device iterations to land honestly: raw retention
alone didn't move the gate (the drops were uniforms), enumeration had to
be extended to remembered views, and the 10 ms input budget was silently
skipping 150–208 retention tiles per XL stroke until the idle budget was
split from the input budget. Owner glass verdict: "extremely impressed…
way better. I see basically no redraws."

## 5. Optical smoothness: the three-lever stack

1. **Sample quantization (the V1-vs-V2 jaggedness answer).** V2 committed
   samples were quantized to quarter-world units — one full screen pixel
   of centerline resolution at 400%, which is why careful strokes
   zigzagged (joint_max 90°) while Raster V1 (float geometry straight to
   raster) never did. Sixteenth-world units fit the same uint16 — zero
   storage cost, 0.25 px at 400%. joint_p95 fell 30–40%
   (fast-curve max 90°→53°, sharp joints 69→27); regressions measured:
   cold walls unchanged within dice, ink latency ±34 µs, +3–10% samples
   from reduced dedup. Owner: "much better."
2. **Streamline 0.4** (owner-suggested): the dt-adaptive input filter cut
   slow-precise joint_p95 36.9°→22.8° for ~0.13 px of extra trailing lag.
   Measured surprise: at 1 kHz the filter's *real* trailing gap is tens
   of pixels on fast strokes at any setting (p95 37→40 px for
   0.35→0.4) — invisible before only because pipeline latency masked it.
   Remedy queued: draw the provisional tail to the raw finger tip.
3. **Settled analytic-coverage AA** (ship contract §4): host prototype →
   frozen RGB565 blend model → device idle pass in one day. Per-tile
   settle cost 5–11 ms v1 → **1.7–5.4 ms mean / 9.3 ms max** v2
   (sqrt confined to the one-pixel boundary annulus; batched slices with
   one present per batch). 25% gets presentation-only AA (the overview is
   replay authority and must stay hard-edged). Full battery moved zero
   gates.

## 6. Poll-gap hygiene along the way

- Event ages during rapid strokes: worst window 113 stale events → 24.
- The 166–184 ms `poll_max` gaps were attributed (idle-time one-shot full
  refreshes; pre-existing) and their felt variant bounded; band-sliced
  refreshes remain queued.
- Every fallback drop now carries a cause counter
  (`InPlaceRetainDrops`) — the mid-stroke pixelation report was
  *falsified* with all-zero counters and attributed to unwarmed
  benchmark views, not a product defect.

## 7. What guards all of it

Every landing above ran the full gate battery: pan strip wire budgets,
PANSEQ pacing, five recorded-trace INKTRACE replays, the cache-tour
ledger (amplification 1.000, zero unexplained), cold walls against their
ceilings, host exactness/fuzz suites (92k+ assertions), ASan. The
verdict-vector discipline (ship contract process rule 8) now requires
every red to have a named scorecard row — the rule exists because one
red (overlap-50 cold) rode invisibly through an entire campaign.
