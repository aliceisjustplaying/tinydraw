# Undo/Redo performance experiments — 2026-08-19

Budget: at most five distinct measured hypotheses. Three used. The full hardware
battery is the authority; lower wall time is better.

## Baseline

The pre-campaign holdback maximum was 117.216 ms at 400% and 66.338 ms at
200%. Authority/overview movement reached 31.7 ms and the first retained-frame
presentation reached 87.5 ms in the diagnostic per-publication policy.

## Attempts

1. **Focused history-control presentation — accepted.** `can_undo` and
   `can_redo` only alter the first two dock cells, but every history-state
   synchronization transferred the complete 368x76 dock. The product now
   presents a guarded 124x76 region. An alternating four-run device A/B was
   5.982 ms full versus 3.003 ms focused (49.8% less wall, 66.3% fewer
   transferred pixels). The full battery remained green; its holdback maxima
   were 117.247 ms at 400% and 66.340 ms at 200%, unchanged because that
   authority benchmark intentionally excludes product chrome.
2. **Newest-first finalized-mask replay — accepted.** History reconstruction
   now paints the target authority newest-first into the existing overview
   scratch. Once a newer operation decides a pixel, older operations skip it;
   this is painter-order exact for both ink and erasers. The alternating host
   benchmark remained pixel-exact and improved its dense 4,000-operation case
   from 0.172 to 0.143 ms per move (17%). On device, authority/overview maxima
   fell from 31.6 to 27.6 ms (12.7%). Holdback maxima fell 117.247 → 114.246 ms
   at 400% and 66.340 → 63.332 ms at 200%. Authority and rendering suites
   passed 89,463 assertions; the full battery remained green.
3. **Row-saturation summary and early completion — accepted.** Reusing the
   rasterizer's exact per-row counters lets history skip saturated row ranges
   and stop once every damaged pixel has a newest writer. The dense 4,000-op
   host case improved again from 0.143 to 0.033 ms per move (4.3x over attempt
   2, 5.2x over forward replay), with exact output. The device torture corpus
   does not saturate its broad damage rectangle and remained flat: 27.65 ms
   move, 114.26/63.34 ms holdback at 400/200%. All host suites and the full
   battery stayed green, so the large dense-stack win has no measured cost.

Two experiments remain.
