# Receipt-check dispatch list — for GPT-5.6 subagents (DISPATCH APPROVED 8/23)

Draft under check: `this-is-a-story-of-project-management-v4.md`. Write all findings to `writing-kits/FACTCHECK-RESULTS-v4.md` under the item's heading. NEVER edit the draft itself.

Each item is a self-contained task. Output format for all: verdict + exact file/line/timestamp receipt, appended to this file under the item. No prose suggestions, no edits to blogpost files.

## D1 — Coda timestamp reconciliation [✎ CHECK in skeleton slot 11]
Two versions on record: "waiting for my 8 p.m. Fable reset... like 20 minutes... 90% left" vs. dictated "6:45 p.m. and 19% of quota left" and "Sent at 6:48 p.m." Find the actual 8/22 assembly-run kickoff session (Codex, ~18:48 local) and any rate-limit/quota lines near it. Deliver: the real send time, and whatever quota evidence exists. If quota % isn't recoverable, say so plainly.

## D2 — Birth certificate caveat [skeleton slot 3]
Candidate "first prompt": 8/9 5:31 PM in `.pro/ChatGPT-Tiny tldraw on ESP32-S3`. Known: first Pi session starts 8/9 18:08 BST (msg 43135de2). Question: does ANY local session (Pi/Codex/Claude/Grok) or repo artifact predate 8/9 17:31 BST with TinyDraw content? Check `~/.codex/history.jsonl`, session stores, first git commit (18:20 local per .codex-archaeology/session-history.md). Deliver: "Pro prompt is first" confirmed or the earlier artifact.

## D3 — The 1,024-sample cap [skeleton slot 6, flagged in slot6 pass sheet]
Draft says the disappearing evil hairline was "a 1,024-sample cap" discovered
via color-switch. Quote pack (§6) says remembered caps are unsupported and the
confirmed mechanism was an every-64-samples ~70 ms tile-copy pause. Search the
8/14–8/19 sessions + docs for any actual 1,024 limit (samples, points, chords,
buffer). Deliver: receipt or "unsupported — recommend telling it at remembered
depth."

## D4 — P.S. section [FACT-CHECK]/[NEEDS] tags [blogpost-edited.md P.S. + skeleton slot 13]
Resolve every remaining tag against `.codex-archaeology/docs-performance.md`, `.codex-archaeology/git-history.md`, `docs/PERFORMANCE_CHRONICLE.md`, `docs/receipts/`. Deliver: per-tag verdict + receipt, no rewrites.

## D5 — Landed/rejected experiments list (unfinished from 8/22)
An agent was compiling the full landed vs. rejected optimization list when the session wrapped. Finish it. Primary sources: `docs/receipts/vector-v2/ESP32S3_NATIVE_DISASSEMBLY_PERFORMANCE_2026_08_22.md` (the 8/22 assembly run), `.pi/plans/2026-08-21-cold-optimization-inventory/scout-context.md`, `docs/PERFORMANCE_CHRONICLE.md`. Deliver: two lists with receipts. Feeds P.S. + possibly one coda line.

## D6 — SVG export fix verification (fixed yesterday per Sarah)
Confirm the fix landed: find the commit/session, run the export test if one exists, and note whether export CRC/bit-exactness claims in the 8/22 receipt still hold post-fix. Also verify draft claim "our SVG exports have proper paths for all shapes." Deliver: commit hash + test result.

## D7 — 3×3 canvas at the meetup? [v4 draft]
Draft: "I had an app with a 3x3 screen size canvas, pen and eraser, twelve colors and 4 line sizes" presented at the Monday meetup. Quote pack chronology says the presentation build had optimized ink, panning, undo, unreliable Wi-Fi PNG export, and "the completed 3×3 V1 arrive[d] the following day" [.codex-archaeology/origin-v1-v2-inking-quote-pack-2026-08-22.md, Chronology anchors]. Determine what the meetup build actually contained: canvas size, colors count, line sizes, undo levels. Sources: 8/10 session before 17:17 departure, git log up to 8/10 ~17:30.

## D8 — Device arrival day [v4 draft]
Draft: "the device would not arrive until Sunday and I wouldn't be able to start testing on it until monday morning." Logs: first physical-board message is 8/10 (Monday) 08:22 "good morning it arrived" [quote pack msg 8717f762]. Question: any evidence of Sunday delivery (unopened) vs Monday delivery? If unknowable, say so — she can phrase at remembered depth.

## D9 — 16 vs 18 tricks [v4 draft]
Draft says "we stacked 18 different tricks... rejecting 22 others." The 8/21 fact-check verified "16 optimizations stacked, 22 rejected" [EDITOR_HANDOVER_2026_08_21.md]. Skeleton also uses "18 tricks." Does 18 = 16 + post-8/21 additions (e.g., 8/22 assembly round)? Produce the definitive count with the list. Overlaps D5 — do together.

## D10 — Memory-map numbers [v4 draft]
Draft: "320kb SRAM (fast) 200kb IRAM (also fast; moving the rasterization core here gave us an immediate 7-11% boost) and 8MB PSRAM." Verify each against ESP32-S3 datasheet reality + this project's actual config (sdkconfig, size reports, docs/receipts). Known nearby receipt: whole-rasterizer IRAM A/B = -6.93% to -11.68% compute, median -8.70%, ~13 KiB internal heap, 8/18 [docs/receipts/vector-v2/F24_RASTER_IRAM_AB_2026_08_18.md; two-episodes-writing-memory.md ledger]. Flag: "immediate" and "boost" phrasing vs compute-not-wall distinction.

## D11 — Performance-targets list on record [v4 draft]
Draft: targets were "cold renders under 500ms at all zoom levels (25%-400%), tear free panning... targeting 24 fps with the stretch goal of 30; infinite undo and redo, anti-aliasing and proper SVG export." Verify each was a stated target contemporaneously (esp. the 24/30 fps framing and "infinite" undo — V1 had ten levels; what was V2's actual undo depth/claim?). Deliver per-target: receipted / retrospective framing.

## D12 — "9 days" arithmetic + tag date [v4 draft]
Start: 8/9 (Sunday) evening. Draft: "9 days after starting we... hit those targets" and coda: "tagged the codebase as v2 on Wednesday." Get the actual v2 tag date/time from git, compute elapsed days, and give her the honest phrasing options (e.g., "nine days of work" vs "on day N").

## D13 — Two phantom-dot bugs [v4 draft]
Draft describes (a) phantom dots on SVG export top-of-document + tiny squares on screen, absent from PNG, tap-at-edge related; (b) a second phantom-dot bug from touch-down-then-decide-not-to-draw. Find receipts for both. Known: "late phantom-dot/hairline artifact" 8/18-19 [.codex-archaeology/session-history.md §9].

## D14 — Xtensa GCC "has *none*" vectorization [v4 draft]
Draft: "the xtensa gcc built-in vectorization as it currently has *none*." This is currently an agent claim per skeleton notes. Verify what's actually true of Xtensa GCC upstream (auto-vectorization for ESP32-S3 PIE?) well enough to either state it flat or require "as far as my agents and I can tell" hedging. Cite sources.
