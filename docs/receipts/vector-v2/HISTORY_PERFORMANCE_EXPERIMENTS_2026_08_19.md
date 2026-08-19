# Undo/Redo performance experiments — 2026-08-19

Budget: at most five distinct measured hypotheses. One used. The full hardware
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

Four experiments remain.
