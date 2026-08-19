# TinyDraw V2 release queue

Last updated: 2026-08-19. Feature implementation and the maintainability
cleanup are complete. This file contains only forward work. Current evidence
and accepted results live in [`PROJECT_STATE.md`](PROJECT_STATE.md); detailed
history lives in receipts, tags, and Git history.

Vector V2 is the ESP32 product generation. Raster V1, QEMU, and the macOS host
remain supported targets.

## Execution rules

- Change one measured hot-path hypothesis at a time. Record predicted savings,
  removed work, unchanged guards, and the observation that would falsify it.
- Run final performance receipts with normal product services enabled,
  including autosave. Report requested and effective panel clocks plus free and
  largest PSRAM; preserve the 1.5 MiB export reserve.
- Reopen every dependent gate after a shared-path change. Glass is authoritative
  for visible correctness and feel; software counters provide attribution.
- Keep the renderer's fixed workspaces, allocation order, bounded budgets,
  toroidal staging, cancellation checks, and exact zoom memory unless a device
  measurement justifies changing them.
- Revert a change that pushes a closed metric outside its guard. Do not carry an
  optical verdict across a cadence or staging change.
- Keep accepted receipts in Git. Raw serial logs and captures belong in run
  artifacts. Completed checklists and campaign narrative do not return here.

## 1. Integrate and establish the post-cleanup baseline

- [x] Host debug and release pass 31/31; ASan/UBSan passes 13/13. Changed Vector
      V2 files are formatted and `git diff --check` passes. Whole-tree formatting
      still finds the pre-existing Raster V1 `firmware_canvas.h:28`; clang-tidy
      stops on authority-journal complexity, and cppcheck findings remain open.
- [x] Build ESP32 product, 448-slot gate, Raster V1, QEMU, and the macOS host
      independently; run the QEMU replay.
- [x] Run the automated physical performance battery on the cleanup build with
      the autosave service initialized. Every firmware verdict passed; the
      harness issued no journal writes, and 400% general cold remains above the
      release line at 515.123 ms.
- [ ] Exercise equivalent normal-product workloads with real journal writes and
      compare them with accepted receipts.
- [ ] Normal product firmware boots and passed a cursory drawing sanity check.
      Complete the same-head pan, zoom, minimap, Undo/Redo, authority-only
      recovery, PNG/SVG export, USB return, NTP, and power checklist.

## 2. Final measured optimization round

- [x] Bring the binding `overlap` workload's 50% cold result under the 500 ms
      product limit. Per-chord finalized-window refresh reduced the full-gate
      result from 585.821 ms to 476.969 ms; that receipt's device battery passed.
- [x] **Done 2026-08-18:** deterministic Undo/Redo baseline (`history_latency`
      gate) and the COW preserved-tile swap treatment. Revisit repair fell
      338,998→229 µs; owner glass-accepted. Receipts:
      `benchmark-results/history-latency-2026-08-18/RECEIPT.md`.
- [x] Finish settled-AA performance. Landed 2026-08-18: no_ink fast path,
      internal planes, saturated-destination skip (−43% on the evil corpus),
      and the deterministic `settle_timing` battery gate. Real documents now
      settle 429–552 ms at 400% but 690–922 ms at 50–200%; the designated
      next levers are slice work-charge recalibration and edge-span
      recording (see `docs/HANDOVER_2026_08_18_FINAL_ROUND.md` §1). The owner
      closed the bounded five-experiment campaign on 2026-08-19; remaining
      visual refinement is post-release work.
- [x] Diagnose the transient color-popup byte-swap incident
      (`docs/receipts/vector-v2/COLOR_POPUP_BYTESWAP_INCIDENT_2026_08_18.md`)
      after the AA work, before release closure.
- [ ] Split the remaining 166–184 ms one-shot full-frame refresh class into
      input-pollable bands, preserving exact presentation and pan cadence.
- [ ] Re-run pan cadence, live-ink latency, cold, revisit retention, memory, and
      export timing after each shared renderer or scheduling change.
- [ ] Attribute any remaining unexplained revisit render in a gate-build glass
      session before changing cache policy or capacity; the live ledger is
      intentionally absent from ordinary product firmware.
- [ ] Close optical ink latency with sampler→consume→geometry→submit→DMA
      timestamps and a failing positive control if the final renderer changes
      the accepted ink path.

## 3. Remaining product correctness and UI work

- [ ] Show visible capacity, save, export, storage, and hardware failure states.
- [x] Encode SVG eraser strokes as transparent cutouts while retaining one
      painter-ordered path per physical stroke.
- [x] Center the USB helper text from its measured width.
- [ ] Review every physical touch target and overlap, add pressed feedback, and
      run a missed-tap check on glass. Compact popups now exclusively own their
      input layer, closing the observed Export/pencil and size/history leaks.
- [ ] Capture a physical mount/eject/return receipt for the SVG+PNG volume with
      no watchdog, stale medium, or USB-mode wedge. Mount/eject/automatic serial
      return now passes twice; a longer repeated-export soak remains.
- [ ] Characterize representative long documents, capacity limits, cache
      pressure, and the 25% overview paths.
- [ ] Recheck reset-storm startup presentation timeouts and expose a stable
      recovery/power-cycle verdict.

## 4. Release closure

- [ ] Exercise hairlines, XL strokes, dense overdraw, erasing, long gestures,
      world edges, and every zoom.
- [ ] Soak repeated draw/pan/Undo/Redo/autosave/export/power cycles for hours.
- [ ] Keep deterministic journal truncation and corruption fixtures green.
      Destructive physical power-cut testing remains outside the owner-prioritized
      release work.
- [ ] Run the complete host, sanitizer, firmware, hardware, optical, recovery,
      and export battery on the same release candidate.
- [ ] Physically recover a retained-Redo document using the current
      authority-only journal, derive the next Stroke identity from restored
      authority, and verify focus-centered zoom starts from product defaults.
- [ ] Tag each ship-contract requirement's known-good revision and publish the
      release build.

## Dependency reopen matrix

| Changed area | Gates that reopen |
|---|---|
| Presenter, staging, or TE cadence | Pan optical correctness; ink presentation latency |
| Touch buffering or stroke coordination | Ink latency/fidelity; pan and overlay gestures |
| Authority, generation, or history | Cold exactness; damage; SVG; autosave; recovery |
| Cache eviction or settled AA | Cold; revisit retention; memory reserve; export pixels |
| Autosave or storage scheduling | Pan; ink; cold; memory; recovery; export flush |

## Release definition

Release closure requires every applicable ship-contract gate to pass with
normal services enabled; no unexplained quality regression on revisit; responsive
input during rendering, saving, recovery, and export; clean long-session and
restart behavior; and independent builds for every retained target.

## Post-ship backlog

- Improve minimap navigation and optional visibility controls.
- Demo record/replay, richer semantic SVG, and 800% zoom.
