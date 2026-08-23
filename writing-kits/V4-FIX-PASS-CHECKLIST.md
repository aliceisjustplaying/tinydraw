# V4 fix-pass checklist — every receipted correction in one sweep

✎ All facts below are verdicted in writing-kits/FACTCHECK-RESULTS-v4.md (line refs there).
✎ Wording of every fix is YOURS. This file only says what the receipts support.
✎ Two kinds of items: [FIX] = receipt contradicts the draft; [DECIDE] = your call.

## Hard fixes (receipt contradicts draft)

1. [FIX] "gcc++26" → the project is **C++20** (CMakeLists.txt:12; all targets cxx_std_20).
2. [FIX] "3x3 screen size canvas" at the meetup → meetup build was **2×2 world**; 3×3 landed next day (commit 582f317). Colors (12), line sizes (4), pen/eraser, ten undo levels all confirmed. (D7)
3. [FIX] "320kb SRAM (fast) 200kb IRAM" → no such fixed banks. Receipted: 512 KB total on-chip SRAM (shared); app's DIRAM pool 341,760 B; 127,587 B executable internal text + dedicated 16 KiB IRAM region. Use KB/KiB capitalized. (D10)
4. [FIX] "immediate 7-11% boost" (IRAM) → measured 6.93–11.68% **compute** (median 8.70%) — rounds to 7–12%, and it's compute, not end-to-end wall. (D10)
5. [FIX] "rejecting 22 others" → 22 is not recoverable. Safe: **18 landed** interventions (curated pre-assembly inventory), **20 rejected rows** listed in same inventory; the 8/22 assembly round separately has **34 accepted / 23 rejected**. Not addable. (D5/D9)
6. [FIX] "infinite undo and redo" (targets list) → contract says **≥10 guaranteed, opportunistically unlimited within document capacity** (4,000 ops). (D11)
7. [FIX] "9 days after starting we hit those targets" → actual: 9 days 18h 48m = calendar day 11. Sol's phrasings: "nine days and change" / "less than ten days" / "eleventh calendar day." (D12)
8. [FIX] "our SVG exports have proper paths for all shapes" → app has only pen/eraser op types; receipt supports "proper paths for everything it can draw" (95/95 tests, 300 randomized docs, 17,496-subpath real file inspected clean). (D6)
9. [FIX] "the xtensa gcc ... has *none*" → scope it: GCC has generic auto-vectorization; the **Xtensa backend generates zero ESP32-S3 PIE SIMD** — verified to GCC source. Stated flat within that scope, no hedge needed. (D14)
10. [FIX] "mechanistical simplicity" → your receipted 8/14 prompt: **"mechanical sympathy and elegance, demoscene mindset are the keywords here"** (msg 500fd477).
11. [FIX] Coda numbers when stitched in: sent **6:48 p.m.**, reset **19:09:36** (21 min later), **18% quota left** (not 8 p.m./90%; your original 6:45/19% dictation was nearly exact). "8 p.m. Fable reset" is unsupported either way — keep as memory or drop. (D1)
12. [FIX] Phantom dots: story confirmed, both bugs distinct with fix commits; but "tiny squares on the screen" → receipts support tiny raw-tile **marks** that appear then vanish, not squares. (D13/D4)

## Confirmed — safe to keep as written
- Birth-certificate Pro prompt 8/9 17:31 BST is genuinely first (D2) — blockquote away.
- "targeting 24 fps with the stretch goal of 30" (D11) — near-verbatim match to the frozen contract.
- Cold <500ms at 25–400%, tear-free as gate, mandatory AA, SVG fidelity contract (D11).
- 40/50/60 MHz all-actually-40, GETSCANLINE zeros, sacred 1.5 MiB vs 291 KB, magenta popup, watchdog, fast-LOD deletions + 14 more P.S. items (D4).
- Fuji timeline as one night; classifier detour + "over an hour?"; moving tear 22:59→23:06→23:41 (D4).
- SVG sharp-reversal fix landed, host suites green (D6). Note: no post-fix DEVICE export CRC run exists — 2-min device export before Monday would close it.

## Typos (from earlier pass, unchanged)
"se stacked"→we · "It the issue"→If · "what refer to"→what I refer to · "penality" · "ot has" · "an project manager" · "1'8""→1.8" · "Fuji XT-5"→X-T5 · "surprising, no"→surprisingly · caps sweep (steve ruiz etc.) at polish time.

## [DECIDE] queue (no receipts needed, just you)
- "projecting managers" — typo or keep-as-pun.
- Funding ask: keep? If yes: reconcile $30k vs coda-block napkin math; placement after coda or in P.S.
- Tail reorder: evil hairlines up beside glass testing; "do not" rant → P.S.; body ends on the project-managers line → coda → P.S.
- Fuji paragraph: keep compressed vs +2–3 receipted beats (positive control, two moving-tear reports; optional: the receipted "stop putting me to sleep" button).
- Three friends (Janka / top-100 systems / 100x PM): one distinguishing clause each; fill "projects like X and Y"; permission pings (screenshot 90%→100%, PM mutual).
- Refrain: plant "you got this" ×2–3 so the coda's typo'd finale closes it — or no refrain at all.
- Device arrival "Sunday": logs only back Monday 08:22 "good morning it arrived" — keep if it's your true memory, else simplify.
- $440 line: still parked; the funding ask may supersede it.
- Disclosure footnote (words-vs-code mirror of your "95% of these words" convention) — construction yours, spot: after P.S.
