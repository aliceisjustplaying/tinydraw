#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["opencv-python-headless>=4.9", "numpy>=1.26"]
# ///
"""Optical tear classifier for TinyDraw panel-probe Block B cells.

Input: video of the panel running one probe cell (pattern: alternating
red/green field, four corner fiducials, 5-bit cell ID strip, blue guard
columns). Output: per-frame CSV, verdict summary, and annotated evidence
frames.

Physics encoded here (see docs/receipts/hardware/CO5300_PANEL_LIMITS_2026-08-15.md):
- Panel refresh 59.62 Hz; scan-in sweeps top->bottom in ~16.2 ms.
- At 240 fps capture, a NORMAL frame transition appears as one red/green
  boundary moving DOWN ~90-130 panel rows per video frame across ~4 frames.
- A TEAR is a boundary that stays at the same row across >=3 consecutive
  video frames, a boundary moving upward, or >1 simultaneous boundary.
- White pixels inside the blue guard columns are edge-notch events.
"""

import argparse
import csv
import pathlib
import sys

import cv2
import numpy as np

PANEL_W, PANEL_H = 368, 448
FIELD_X0, FIELD_X1 = 96, 272
FIELD_Y0, FIELD_Y1 = 48, 388
ID_BLOCKS = [(124 + i * 24, 12, 20, 24) for i in range(5)]
GUARD_COLUMNS = [(0, 8), (360, 368)]
# Tolerance sized for 1/800 s exposure: the scan boundary smears ~33 rows
# during 1.25 ms, giving ~+/-15 rows of localization jitter, still far below
# the ~111 rows/frame normal sweep motion at 240 fps.
STATIC_TOLERANCE_ROWS = 20
STATIC_MIN_FRAMES = 3
NORMAL_SWEEP_MIN, NORMAL_SWEEP_MAX = 40, 200  # rows/video-frame downward


DETECT_SCALE = 4  # panel detection runs on a downscaled frame for speed


def find_panel_quad(frame_bgr):
    """Locate the bright emissive panel; return 4 corner points or None.

    The CO5300 sweeps a rolling emission-off band with the scan, so at fast
    shutter the panel appears as 2-3 bright slabs separated by dark bands.
    A tall morphological close bridges the bands before contouring. Runs on
    a 1/DETECT_SCALE image; corners are scaled back up.
    """
    small = cv2.resize(
        frame_bgr,
        (frame_bgr.shape[1] // DETECT_SCALE, frame_bgr.shape[0] // DETECT_SCALE),
        interpolation=cv2.INTER_AREA,
    )
    gray = cv2.cvtColor(small, cv2.COLOR_BGR2GRAY)
    blur = cv2.GaussianBlur(gray, (5, 5), 0)
    # Fixed low threshold: the panel is emissive in a dim room; Otsu splits
    # bright slabs from each other instead of panel from background.
    _, mask = cv2.threshold(blur, 35, 255, cv2.THRESH_BINARY)
    kernel = cv2.getStructuringElement(cv2.MORPH_RECT, (5, 31))
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel)
    # Trim bloom/glow so the quad hugs the emissive area.
    mask = cv2.erode(mask, cv2.getStructuringElement(cv2.MORPH_RECT, (3, 3)))
    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    if not contours:
        return None
    contour = max(contours, key=cv2.contourArea)
    if cv2.contourArea(contour) < 0.005 * small.shape[0] * small.shape[1]:
        return None
    contour = contour.astype(np.float32) * DETECT_SCALE
    rect = cv2.minAreaRect(contour)
    box = cv2.boxPoints(rect).astype(np.float32)
    # Deterministic, mirror-free ordering in image space:
    # TL = min(x+y), BR = max(x+y), TR = min(y-x), BL = max(y-x).
    sums = box.sum(axis=1)
    diffs = box[:, 1] - box[:, 0]
    ordered = np.array(
        [
            box[np.argmin(sums)],
            box[np.argmin(diffs)],
            box[np.argmax(sums)],
            box[np.argmax(diffs)],
        ],
        dtype=np.float32,
    )
    return ordered


class QuadTracker:
    """EMA-smoothed quad with jump rejection: handheld drift is slow at
    240 fps, while emission bands crossing panel edges cause brief shrunken
    detections that must not yank the registration."""

    def __init__(self, alpha=0.15, tolerance=0.15):
        self.alpha = alpha
        self.tolerance = tolerance
        self.quad = None

    def update(self, detected):
        if detected is None:
            return self.quad
        if self.quad is None:
            self.quad = detected
            return self.quad
        previous_area = cv2.contourArea(self.quad.astype(np.int32))
        detected_area = cv2.contourArea(detected.astype(np.int32))
        if previous_area > 0 and abs(detected_area - previous_area) / previous_area > self.tolerance:
            return self.quad  # reject the jump, keep tracking
        self.quad = (1 - self.alpha) * self.quad + self.alpha * detected
        return self.quad


def warp_panel(frame_bgr, quad):
    target = np.array(
        [[0, 0], [PANEL_W - 1, 0], [PANEL_W - 1, PANEL_H - 1], [0, PANEL_H - 1]],
        dtype=np.float32,
    )
    matrix = cv2.getPerspectiveTransform(quad, target)
    return cv2.warpPerspective(frame_bgr, matrix, (PANEL_W, PANEL_H))


def orient(warped, state):
    """Self-calibrating orientation: camera rotation metadata is invisible to
    cv2.VideoCapture, so try decoding the cell-ID strip both ways and lock
    onto the orientation that consistently yields valid IDs (1..5)."""
    if state.get("locked") is not None:
        return cv2.rotate(warped, cv2.ROTATE_180) if state["locked"] else warped
    upright = read_cell_id(warped)
    rotated = cv2.rotate(warped, cv2.ROTATE_180)
    flipped = read_cell_id(rotated)
    if upright is not None and flipped is None:
        state["votes"] = state.get("votes", 0) + 1
    elif flipped is not None and upright is None:
        state["votes"] = state.get("votes", 0) - 1
    votes = state.get("votes", 0)
    if abs(votes) >= 10:
        state["locked"] = votes < 0
    return rotated if votes < 0 else warped


def panel_scale(warped):
    """Per-frame brightness scale: p99 of gray. The video is TV-range and
    exposure-dependent; every threshold is relative to this."""
    gray = cv2.cvtColor(warped, cv2.COLOR_BGR2GRAY)
    return float(np.percentile(gray, 99)), gray


def read_cell_id(warped, scale_gray=None):
    scale, gray = scale_gray if scale_gray is not None else panel_scale(warped)
    if scale < 40:  # mostly emission band; nothing readable
        return None
    strip = gray[12:36, 120:248]
    # A readable strip has true-black background and >=1 bright block.
    if strip.min() > 0.25 * scale or strip.max() < 0.5 * scale:
        return None
    value = 0
    for bit, (x, y, w, h) in enumerate(ID_BLOCKS):
        block = gray[y : y + h, x : x + w]
        value = (value << 1) | (1 if block.mean() > 0.4 * scale else 0)
    return value if 1 <= value <= 5 else None


def classify_rows(warped, scale):
    """Per-row A(red)/B(green) classification over the central field band.

    labels[row] in {1: red, 2: green, 0: uncertain/dark}; thresholds are
    relative to the per-frame brightness scale so emission-band rows and
    exposure changes land in 'uncertain' instead of flipping colors.
    """
    band = warped[FIELD_Y0:FIELD_Y1, FIELD_X0:FIELD_X1].astype(np.int32)
    blue, green, red = band[:, :, 0], band[:, :, 1], band[:, :, 2]
    red_score = (red - green).mean(axis=1)
    threshold = max(12.0, 0.2 * scale)
    labels = np.zeros(FIELD_Y1 - FIELD_Y0, dtype=np.uint8)
    labels[red_score > threshold] = 1
    labels[red_score < -threshold] = 2
    return labels


def boundaries_of(labels):
    """Rows (panel coords) where the classified color flips."""
    result = []
    previous = 0
    previous_row = None
    for index, label in enumerate(labels):
        if label == 0:
            continue
        if previous != 0 and label != previous:
            result.append(FIELD_Y0 + index)
        previous = label
        previous_row = index
    return result


def guard_notch_pixels(warped, scale):
    count = 0
    threshold = 0.45 * scale
    for x0, x1 in GUARD_COLUMNS:
        # Exclude fiducial row ranges: registration jitter bleeds the white
        # rings into the guard band and fakes notches.
        guard = warped[64:384, x0:x1].astype(np.int32)
        blue, green, red = guard[:, :, 0], guard[:, :, 1], guard[:, :, 2]
        # Guard is saturated blue; a notch is bright in red AND green too.
        white = (red > threshold) & (green > threshold) & (blue > threshold)
        count += int(white.sum())
    return count


def is_blue_frame(warped, scale):
    """Interstitial detector: the whole field band is guard-blue."""
    band = warped[FIELD_Y0:FIELD_Y1, FIELD_X0:FIELD_X1].astype(np.int32)
    blue, green, red = band[:, :, 0], band[:, :, 1], band[:, :, 2]
    blueness = blue - np.maximum(green, red)
    blue_rows = (blueness.mean(axis=1) > 0.2 * scale).sum()
    return blue_rows > 0.7 * (FIELD_Y1 - FIELD_Y0)


CYCLE_ORDER = [4, 1, 5, 2, 3]

# Cadence-aware anomaly rules. A new sweep resets the boundary to the top:
# large negative delta is normal; a small upward move is not.
NEW_SWEEP_DELTA = -250


def analyze_segment(records, evidence):
    """Tear/anomaly/notch analysis over one cell segment.

    records: list of per-frame dicts with keys frame, bounds, notches.
    Returns stats dict; appends (frame, flag, bounds) to evidence for
    frames that should be re-warped and saved.
    """
    stats = {"analyzed": 0, "tears": [], "anomalies": [], "notches": []}
    history = []
    for record in records:
        stats["analyzed"] += 1
        bounds = record["bounds"]
        flag = ""
        if len(bounds) > 2:
            flag = "multi_boundary"
            stats["anomalies"].append((record["frame"], tuple(bounds)))
        if record["notches"] > 12:
            flag = (flag + "+notch").strip("+")
            stats["notches"].append((record["frame"], record["notches"]))
        if bounds and history:
            previous = history[-1]
            if previous["bounds"] and previous["frame"] == record["frame"] - 1:
                delta = bounds[0] - previous["bounds"][0]
                static_run = 1
                for back in reversed(history):
                    if not back["bounds"] or back["frame"] != record["frame"] - static_run:
                        break
                    if abs(back["bounds"][0] - bounds[0]) <= STATIC_TOLERANCE_ROWS:
                        static_run += 1
                    else:
                        break
                if static_run >= STATIC_MIN_FRAMES:
                    flag = (flag + "+static_tear").strip("+")
                    stats["tears"].append((record["frame"], bounds[0], static_run))
                elif NEW_SWEEP_DELTA < delta < -STATIC_TOLERANCE_ROWS:
                    flag = (flag + "+upward_boundary").strip("+")
                    stats["anomalies"].append((record["frame"], tuple(bounds)))
        history.append(record)
        if len(history) > 16:
            history.pop(0)
        record["flag"] = flag
        if flag:
            evidence.append((record["frame"], flag, bounds))
    return stats


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("video")
    parser.add_argument("--out", default=None, help="output directory")
    parser.add_argument("--start-frame", type=int, default=0)
    parser.add_argument("--max-frames", type=int, default=0)
    parser.add_argument("--max-evidence", type=int, default=60)
    args = parser.parse_args()

    video_path = pathlib.Path(args.video)
    out_dir = (
        pathlib.Path(args.out) if args.out else video_path.with_suffix(".classified")
    )
    out_dir.mkdir(parents=True, exist_ok=True)

    capture = cv2.VideoCapture(str(video_path))
    if not capture.isOpened():
        sys.exit(f"cannot open {video_path}")
    if args.start_frame:
        capture.set(cv2.CAP_PROP_POS_FRAMES, args.start_frame)

    # ---- pass 1: extract per-frame observations --------------------------
    tracker = QuadTracker()
    orientation = {}
    records = []
    quads = {}
    frame_index = args.start_frame - 1
    processed = 0
    while True:
        ok, frame = capture.read()
        if not ok:
            break
        frame_index += 1
        processed += 1
        if args.max_frames and processed > args.max_frames:
            break
        if processed % 4000 == 0:
            print(f"pass1: {processed} frames", file=sys.stderr, flush=True)
        quad = tracker.update(find_panel_quad(frame))
        if quad is None:
            continue
        warped = orient(warp_panel(frame, quad.astype(np.float32)), orientation)
        scale, gray = panel_scale(warped)
        if scale < 40:
            continue
        blue = is_blue_frame(warped, scale)
        cell_id = None if blue else read_cell_id(warped, (scale, gray))
        labels = None if blue else classify_rows(warped, scale)
        bounds = [] if blue else boundaries_of(labels)
        known = 0 if blue else int((labels != 0).sum())
        records.append(
            {
                "frame": frame_index,
                "blue": blue,
                "id": cell_id,
                "bounds": bounds,
                "notches": 0 if blue else guard_notch_pixels(warped, scale),
                "known_rows": known,
            }
        )
        quads[frame_index] = quad.copy()
    capture.release()

    # ---- pass 2: segment by blue interstitials ---------------------------
    segments = []  # (start_idx, end_idx) into records, exclusive end
    in_blue = True
    start = None
    blue_run = 0
    for index, record in enumerate(records):
        if record["blue"]:
            blue_run += 1
            if blue_run >= 30 and start is not None:
                segments.append((start, index - blue_run + 1))
                start = None
        else:
            if record["known_rows"] > 100:
                if start is None and blue_run >= 30:
                    start = index
                blue_run = 0
    if start is not None:
        segments.append((start, len(records)))
    # Drop tiny fragments (aiming, focus hunts).
    segments = [s for s in segments if s[1] - s[0] > 400]

    # Label segments: majority ID vote, then fill by cycle order around
    # anchored neighbors.
    labels = []
    for start, end in segments:
        votes = {}
        for record in records[start:end]:
            if record["id"] is not None:
                votes[record["id"]] = votes.get(record["id"], 0) + 1
        labels.append(max(votes, key=votes.get) if votes else None)
    for index, label in enumerate(labels):
        if label is None:
            continue
        position = CYCLE_ORDER.index(label)
        for offset in range(1, len(labels)):
            for direction in (1, -1):
                other = index + direction * offset
                if 0 <= other < len(labels) and labels[other] is None:
                    labels[other] = CYCLE_ORDER[
                        (position + direction * offset) % len(CYCLE_ORDER)
                    ]

    # ---- pass 3: per-segment analysis ------------------------------------
    per_cell = {}
    evidence = []
    for (start, end), label in zip(segments, labels):
        if label is None:
            continue
        stats = analyze_segment(records[start:end], evidence)
        if label in per_cell:
            for key in ("tears", "anomalies", "notches"):
                per_cell[label][key].extend(stats[key])
            per_cell[label]["analyzed"] += stats["analyzed"]
        else:
            per_cell[label] = stats

    # ---- outputs ---------------------------------------------------------
    with open(out_dir / "frames.csv", "w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["frame", "blue", "cell_id", "boundaries", "notches", "flag"])
        for record in records:
            writer.writerow(
                [
                    record["frame"],
                    int(record["blue"]),
                    record["id"],
                    ";".join(map(str, record["bounds"])),
                    record["notches"],
                    record.get("flag", ""),
                ]
            )

    with open(out_dir / "segments.txt", "w") as handle:
        for (start, end), label in zip(segments, labels):
            handle.write(
                f"cell={label} frames={records[start]['frame']}..{records[end - 1]['frame']} "
                f"count={end - start}\n"
            )

    # Save evidence by re-seeking the flagged frames.
    if evidence:
        capture = cv2.VideoCapture(str(video_path))
        for frame_number, flag, bounds in evidence[: args.max_evidence]:
            if frame_number not in quads:
                continue
            capture.set(cv2.CAP_PROP_POS_FRAMES, frame_number)
            ok, frame = capture.read()
            if not ok:
                continue
            warped = orient(
                warp_panel(frame, quads[frame_number].astype(np.float32)), orientation
            )
            for row in bounds:
                cv2.line(warped, (0, row), (PANEL_W, row), (255, 255, 255), 1)
            cv2.imwrite(
                str(out_dir / f"evidence-{frame_number:06d}-{flag}.png"), warped
            )
        capture.release()

    def cell_verdict(stats):
        if stats["analyzed"] < 100:
            return "INCONCLUSIVE"
        if stats["tears"]:
            return "TEAR"
        if stats["anomalies"]:
            return "ANOMALY"
        return "CLEAN"

    control = per_cell.get(4)
    instrument_valid = control is not None and cell_verdict(control) == "TEAR"

    summary = out_dir / "verdict.txt"
    with open(summary, "w") as handle:
        handle.write(f"video={video_path.name}\n")
        handle.write(
            f"instrument_valid={instrument_valid} "
            "(cell 4 positive control must be TEAR)\n\n"
        )
        for cell in sorted(per_cell):
            stats = per_cell[cell]
            verdict = cell_verdict(stats)
            qualified = verdict
            if verdict == "CLEAN" and not instrument_valid:
                qualified = "CLEAN_BUT_INSTRUMENT_INVALID"
            handle.write(
                f"cell={cell} analyzed={stats['analyzed']} "
                f"tears={len(stats['tears'])} anomalies={len(stats['anomalies'])} "
                f"notches={len(stats['notches'])} verdict={qualified}\n"
            )
            for event in stats["tears"][:10]:
                handle.write(
                    f"  tear frame={event[0]} row={event[1]} persisted={event[2]}\n"
                )
            for event in stats["anomalies"][:10]:
                handle.write(f"  anomaly frame={event[0]} boundaries={event[1]}\n")
    print(summary.read_text())


if __name__ == "__main__":
    main()
