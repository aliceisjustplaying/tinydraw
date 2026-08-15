# Block B optical protocol — pre-registered

Registered before capture: 2026-08-15. Product gate per SHIP decision:
**≥24 FPS tear-free pan**. Full-frame TE-synced streaming delivers 29.4 FPS
(`HARDWARE_LIMITS.md` §5), so a clean cell 1 satisfies the product gate with
margin.

Rules of this protocol:

1. Every cell's hypothesis and pass criterion is written **here, before
   flashing**. Outcomes get appended, never rewritten.
2. The camera judges **correctness only** (tears/notches). Cadence and
   throughput come from serial receipts; no FPS claims from footage.
3. Cell 4 is a positive control. If cell 4 does not produce TEAR verdicts,
   the instrument (camera + classifier) is not sensitive enough and **no
   CLEAN verdict from any other cell is valid**.

## Cells

Flash: `./scripts/esp32 panel-probe /dev/cu.usbmodem101 40 CELL`
Each cell runs 45 s after boot, then prints `TINYDRAW_PANEL_PROBE_DONE`.

| Cell | Name | What it does | Hypothesis | Pass criterion |
|---|---|---|---|---|
| 1 | boundary-rising-full | Full 448-row sweep started at TE rising edge (29.4 FPS) | Rising edge = blanking start; 578 µs (~15 row) head start + writer/beam parity (27.2 vs 26.7 rows/ms) keeps writer ahead → clean | 0 tear events, 0 anomalies in ≥1,000 panel frames |
| 2 | boundary-falling-full | Same sweep started at TE falling edge | Falling edge = active-scan start; writer and beam start level → tears likely near top | Informational: clean OR torn both discriminate TE phase |
| 3 | midframe-wrap-rising | Mid-frame start (row 166 after 8 ms delay) + wrapped top band | Reproduces the beam-race shape at real 40 MHz rates → expected to tear like the falsified V2 policy | Informational: torn confirms mechanism; clean falsifies the mid-frame-tear theory |
| 4 | freerun-unsynced | Full-frame streams back-to-back, no TE wait (~55 FPS unsynced) | **Must tear** — positive control | Classifier reports TEAR; otherwise instrument invalid |
| 5 | boundary-rising-canvas368 | 368-row region, one sweep per TE period (58.8 FPS) | Same safety as cell 1 at single-period cadence | 0 tear events, 0 anomalies |

Run order: **4 first** (validate instrument), then 1, 5, 2, 3.

## Interpretation matrix (pre-registered)

- 4 torn + 1 clean + 5 clean → boundary-rising policy is the product pan
  path; proceed to Wave 2 with 29.4 FPS full-frame (or 58.8 canvas-region).
- 4 torn + 1 torn → rising-edge sweep unsafe; check 2. If 2 clean, TE
  polarity is inverted from the datasheet reading — adopt falling.
- 4 torn + 1 torn + 2 torn → no edge-synced sweep is safe as built;
  investigate controller write-to-visible semantics before any product work.
- 4 clean → instrument failure. Fix capture/classifier before any verdict.
- Any white guard-column notch in any cell → log a notch event with its
  cell + video frame; notches are a separate open defect.

## Camera recipe (X-T5 + Sigma 56/1.4)

- Movie high-speed mode **1080/240**, manual focus (magnify on panel pixels)
- Shutter **1/800** (the X-T5 locks HS-mode shutter to 1/320–1/800; 1/800
  smears the scan boundary ~33 rows, which the classifier tolerates; 1/320
  would smear ~83 rows — do not use)
- **f/2.8–f/4**, ISO to expose the pattern without clipping white fiducials
- Fixed WB (daylight), room dimmed, no lamp reflections on the glass
- Camera propped ~40 cm square-on; panel fills as much of frame as focus
  allows; all four fiducials must be visible and sharp
- One clip per cell, ≥30 s of the 45 s run; a couple seconds of wobble is
  fine (the classifier re-registers every frame)

## One-take procedure (preferred)

Cell 6 cycles all optical cells in one flash — order 4, 1, 5, 2, 3 —
30 s each with 2 s solid-blue interstitials (total ~2:40). Film the whole
run as one continuous handheld clip; the classifier segments by the
on-screen cell ID (debounced 3 frames) and emits one verdict per cell,
with cell 4 gating instrument validity.

```sh
# flash the cycle; the sequence starts a few seconds after flashing
./scripts/esp32 panel-probe /dev/cu.usbmodem101 40 6

# serial receipt in parallel (optional but nice)
./tools/esp32-capture.py /dev/cu.usbmodem101 \
  benchmark-results/blockB-optical/cycle-serial.log 220 \
  --end-marker TINYDRAW_PANEL_PROBE_DONE

# classify the single clip
./tools/classify-tearing.py path/to/cycle.mov \
  --out benchmark-results/blockB-optical/cycle.classified
```

Handheld at f/4 is acceptable: registration is per-frame and the
pattern elements are ≥20 panel px.

## Per-cell run procedure (fallback)

```sh
# 1. flash the cell (waits at boot ~2 s, then runs 45 s)
./scripts/esp32 panel-probe /dev/cu.usbmodem101 40 4   # cell number

# 2. capture serial receipt in parallel with filming
./tools/esp32-capture.py /dev/cu.usbmodem101 \
  benchmark-results/blockB-optical/cell4-serial.log 70 \
  --end-marker TINYDRAW_PANEL_PROBE_DONE

# 3. copy footage off the camera, then classify
./tools/classify-tearing.py path/to/cell4.mov \
  --out benchmark-results/blockB-optical/cell4.classified
```

Name clips `cellN-YYYYMMDD.mov`. Keep raw footage off git; commit serial
logs, `frames.csv`, `verdict.txt`, and evidence PNGs only.

## Results (append-only)

_(empty — to be filled per cell after classification)_
