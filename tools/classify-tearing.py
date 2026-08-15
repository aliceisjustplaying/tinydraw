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

Physics encoded here (see HARDWARE_LIMITS.md):
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
STATIC_TOLERANCE_ROWS = 8
STATIC_MIN_FRAMES = 3
NORMAL_SWEEP_MIN, NORMAL_SWEEP_MAX = 40, 200  # rows/video-frame downward


def find_panel_quad(frame_bgr):
    """Locate the bright emissive panel; return 4 corner points or None."""
    gray = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2GRAY)
    blur = cv2.GaussianBlur(gray, (9, 9), 0)
    _, mask = cv2.threshold(blur, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    if not contours:
        return None
    contour = max(contours, key=cv2.contourArea)
    if cv2.contourArea(contour) < 0.01 * frame_bgr.shape[0] * frame_bgr.shape[1]:
        return None
    rect = cv2.minAreaRect(contour)
    box = cv2.boxPoints(rect).astype(np.float32)
    # Order: top-left, top-right, bottom-right, bottom-left.
    center = box.mean(axis=0)
    ordered = sorted(box, key=lambda p: np.arctan2(p[1] - center[1], p[0] - center[0]))
    return np.array(ordered, dtype=np.float32)


def warp_panel(frame_bgr, quad):
    target = np.array(
        [[0, 0], [PANEL_W - 1, 0], [PANEL_W - 1, PANEL_H - 1], [0, PANEL_H - 1]],
        dtype=np.float32,
    )
    matrix = cv2.getPerspectiveTransform(quad, target)
    return cv2.warpPerspective(frame_bgr, matrix, (PANEL_W, PANEL_H))


def orient(warped):
    """The cell-ID strip (dark band) sits at the top; flip if it is not."""
    gray = cv2.cvtColor(warped, cv2.COLOR_BGR2GRAY)
    top = gray[8:40, 110:260].mean()
    bottom = gray[PANEL_H - 40 : PANEL_H - 8, 110:260].mean()
    if bottom < top:
        return cv2.rotate(warped, cv2.ROTATE_180)
    return warped


def read_cell_id(warped):
    gray = cv2.cvtColor(warped, cv2.COLOR_BGR2GRAY)
    strip = gray[12:36, 120:248]
    if strip.mean() > 140:  # strip missing / washed out
        return None
    value = 0
    for bit, (x, y, w, h) in enumerate(ID_BLOCKS):
        block = gray[y : y + h, x : x + w]
        value = (value << 1) | (1 if block.mean() > 128 else 0)
    return value


def classify_rows(warped):
    """Per-row A(red)/B(green) classification over the central field band.

    Returns (labels, confidence) where labels[row] in {1: red, 2: green,
    0: uncertain} for rows in the field range.
    """
    band = warped[FIELD_Y0:FIELD_Y1, FIELD_X0:FIELD_X1].astype(np.int32)
    blue, green, red = band[:, :, 0], band[:, :, 1], band[:, :, 2]
    red_score = (red - green).mean(axis=1)
    labels = np.zeros(FIELD_Y1 - FIELD_Y0, dtype=np.uint8)
    labels[red_score > 25] = 1
    labels[red_score < -25] = 2
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


def guard_notch_pixels(warped):
    count = 0
    for x0, x1 in GUARD_COLUMNS:
        guard = warped[:, x0:x1].astype(np.int32)
        blue, green, red = guard[:, :, 0], guard[:, :, 1], guard[:, :, 2]
        white = (red > 180) & (green > 180) & (blue > 180)
        count += int(white.sum())
    return count


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("video")
    parser.add_argument("--out", default=None, help="output directory")
    parser.add_argument("--start-frame", type=int, default=0)
    parser.add_argument("--max-frames", type=int, default=0)
    args = parser.parse_args()

    video_path = pathlib.Path(args.video)
    out_dir = pathlib.Path(args.out or video_path.with_suffix("")).with_suffix(".classified")
    out_dir.mkdir(parents=True, exist_ok=True)

    capture = cv2.VideoCapture(str(video_path))
    if not capture.isOpened():
        sys.exit(f"cannot open {video_path}")
    if args.start_frame:
        capture.set(cv2.CAP_PROP_POS_FRAMES, args.start_frame)

    rows_csv = open(out_dir / "frames.csv", "w", newline="")
    writer = csv.writer(rows_csv)
    writer.writerow(
        ["frame", "cell_id", "dominant", "boundaries", "notch_pixels", "flag"]
    )

    quad = None
    frame_index = args.start_frame - 1
    history = []  # (frame_index, [boundary rows])
    tear_events = []
    anomaly_events = []
    notch_events = []
    cell_votes = {}
    analyzed = 0
    evidence_saved = 0

    while True:
        ok, frame = capture.read()
        if not ok:
            break
        frame_index += 1
        if args.max_frames and analyzed >= args.max_frames:
            break

        # Re-locate the panel every frame (handheld drift), fall back to the
        # previous quad when detection fails.
        located = find_panel_quad(frame)
        if located is not None:
            quad = located
        if quad is None:
            continue
        warped = orient(warp_panel(frame, quad))
        cell_id = read_cell_id(warped)
        if cell_id is not None:
            cell_votes[cell_id] = cell_votes.get(cell_id, 0) + 1

        labels = classify_rows(warped)
        known = labels[labels != 0]
        if known.size == 0:
            continue
        analyzed += 1
        red_fraction = float((known == 1).sum()) / known.size
        dominant = "red" if red_fraction > 0.5 else "green"
        bounds = boundaries_of(labels)
        notches = guard_notch_pixels(warped)

        flag = ""
        if len(bounds) > 1:
            flag = "multi_boundary"
            anomaly_events.append((frame_index, tuple(bounds)))
        if notches > 12:
            flag = (flag + "+notch").strip("+")
            notch_events.append((frame_index, notches))

        # Static-split tear rule and upward-motion rule against history.
        if bounds and history:
            previous_index, previous_bounds = history[-1]
            if previous_bounds and previous_index == frame_index - 1:
                delta = bounds[0] - previous_bounds[0]
                static_run = 1
                for back_index in range(len(history) - 1, -1, -1):
                    h_index, h_bounds = history[back_index]
                    if not h_bounds or h_index != frame_index - static_run:
                        break
                    if abs(h_bounds[0] - bounds[0]) <= STATIC_TOLERANCE_ROWS:
                        static_run += 1
                    else:
                        break
                if static_run >= STATIC_MIN_FRAMES:
                    flag = (flag + "+static_tear").strip("+")
                    tear_events.append((frame_index, bounds[0], static_run))
                elif delta < -STATIC_TOLERANCE_ROWS:
                    flag = (flag + "+upward_boundary").strip("+")
                    anomaly_events.append((frame_index, tuple(bounds)))

        history.append((frame_index, bounds))
        if len(history) > 16:
            history.pop(0)

        writer.writerow(
            [frame_index, cell_id, dominant, ";".join(map(str, bounds)), notches, flag]
        )
        if flag and evidence_saved < 40:
            annotated = warped.copy()
            for row in bounds:
                cv2.line(annotated, (0, row), (PANEL_W, row), (255, 255, 255), 1)
            cv2.imwrite(str(out_dir / f"evidence-{frame_index:06d}-{flag}.png"), annotated)
            evidence_saved += 1

    rows_csv.close()
    capture.release()

    cell = max(cell_votes, key=cell_votes.get) if cell_votes else None
    if analyzed == 0:
        verdict = "INCONCLUSIVE"
    elif tear_events:
        verdict = "TEAR"
    elif anomaly_events:
        verdict = "ANOMALY"
    else:
        verdict = "CLEAN"

    summary = out_dir / "verdict.txt"
    with open(summary, "w") as handle:
        handle.write(
            f"video={video_path.name}\n"
            f"cell_id={cell}\n"
            f"frames_analyzed={analyzed}\n"
            f"tear_events={len(tear_events)}\n"
            f"anomaly_events={len(anomaly_events)}\n"
            f"notch_events={len(notch_events)}\n"
            f"verdict={verdict}\n"
        )
        for event in tear_events[:20]:
            handle.write(f"tear frame={event[0]} row={event[1]} persisted={event[2]}\n")
        for event in anomaly_events[:20]:
            handle.write(f"anomaly frame={event[0]} boundaries={event[1]}\n")
    print(summary.read_text())


if __name__ == "__main__":
    main()
