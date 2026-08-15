# Wave 2 compositor glass observations

Recorded: 2026-08-15

These are the human operator's naked-eye observations from the fixed-position tearing session captured in `manual-glass-postcompositor.log`. They are load-bearing optical evidence; the wording below is preserved verbatim from the overnight work order.

1. WITH burst pacing (b5bdd78, PANSEQ p95 39.9/40.9 ms): tear at ONE fixed row, roughly the zoom-rail minus button's top edge (±5-10 px). Fast pan (~29.5 FPS median).
2. WITHOUT burst (current HEAD): tear MOVED to a fixed row ~2/3 down the minimap region; pan slower.
3. The PRE-compositor build (code at commit c70d4f8) was optically CLEAN at 19.9 FPS — verified on glass and consistent with probe cell 1 (benchmark-results/blockB-optical/PROTOCOL.md).

No software receipt can turn these observations into an optical closure claim. The next optical verdict is pending a human glass check of the final flashed interactive build.
