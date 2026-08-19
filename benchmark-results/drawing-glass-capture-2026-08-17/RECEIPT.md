# Glass capture receipt — 2026-08-17

## Scope

The author exercised the latest product logic under the dedicated
`TINYDRAW_VECTOR_V2_INK_TRACE_CAPTURE` firmware, including one stroke at 25%,
the hardware zoom cycle, 400% drawing, and minimap navigation. The device was
then restored to the ordinary `vector-v2` firmware. No mass-storage command was
run.

## Capture-firmware stall

The author observed a 2–3 second hang after drawing at 25% and pressing the
hardware zoom button. The serial timeline attributes that hang to the
diagnostic capture dump:

- the 50% zoom presentation completed at 12:37:41 (`author-actions.log`, line 8);
- `TINYDRAW_INKTRACE_CAPTURE_BEGIN events=1933` followed at 12:37:41 (line 10);
- the synchronous 1,933-row CSV print ended at 12:37:44 (line 1947);
- the next 100% zoom presentation ran immediately afterward (line 1948).

`InkTraceCaptureRing::dump_and_reset()` emits every row with `std::printf`
(`esp32/main/vector_v2/vector_v2_ink_trace_capture.cpp`, lines 92–126). The app
calls it inline after two touch-idle seconds
(`esp32/main/vector_v2/vector_v2_app.cpp`, lines 1565–1572). Hardware-button
activity is not touch activity, so the comment's assumption that the serial
burst cannot perturb an interaction is false. The entire call is compile-gated
by `TINYDRAW_VECTOR_V2_INK_TRACE_CAPTURE`; ordinary product firmware does not
contain this dump path.

Verdict: the observed multi-second zoom hang belongs to the diagnostic firmware,
not to the ordinary product. Future subjective timing sessions must not use this
capture build unless dumping is moved off the product interaction loop or made
explicitly host-triggered.

## 400% ink observation

The author reported that 400% drawing still felt laggy under the capture build.
Two unblocked 400% strokes reported:

| Stroke log line | submit avg / max | DMA-complete avg / max | event-age max | failures |
|---|---:|---:|---:|---:|
| `author-actions.log:2004` | 1.791 / 3.641 ms | 3.019 / 5.479 ms | 6.230 ms | 0 |
| `author-actions.log:2013` | 1.868 / 4.598 ms | 3.110 / 7.956 ms | 1.240 ms | 0 |

These counters do not explain the glass verdict and do not include optical
scan-to-pixel latency. The first post-dump stroke is contaminated by the dump:
`poll_max_us=3550173`, 14 old events, and `touch_event_age_max_us=1345935`
(`author-actions.log:1986`), so it is excluded from the clean pair above.

A second controlled capture isolated one medium curved stroke after the author
had reached 400% with the hardware button. The author then remained hands-off
until the explicit end marker. It captured one Down, 2,379 Moves, and one Up
over 2.576 seconds with no overflow (`author-400-controlled.log`, lines 11–2396).
Only 190 events changed coordinate: the controller's distinct-position cadence
was 73.758 Hz, with 12/13/14 ms minimum/median/p95 intervals. Position jumps
were 9.220 px median and 28.000 px p95
(`drawing-400-controlled-analysis.txt`). The product consumed 1,044 events,
coalesced 1,337 redundant Moves, and presented 192 changed ink samples. Event
age was at most 1.241 ms; submit was 1.527/2.541 ms average/max and DMA completion
was 2.353/3.810 ms average/max, with zero presentation failure or touch overflow
(`author-400-controlled.log:1`).

The current code already separates smoothed authority from the raw visual tip:
`process_live_ink_move` receives `last_canvas_touch`
(`esp32/main/vector_v2/vector_v2_app.cpp`, lines 1398–1405), and the provisional
ribbon ends at that visual point (`core/src/ribbon_geometry.cpp`, lines
312–317). The measured application/display path is therefore not the dominant
interval after each coordinate becomes available. The remaining observable
cadence is the controller's 12–14 ms distinct-position interval plus unmeasured
optical scan-to-pixel latency. Hiding that would require predictive visual ink,
which is not justified without an glass verdict because prediction can
overshoot corners.

No product ink code changed during either capture.

## Restore

The ordinary firmware image flashed and hash-verified
(`product-restore-flash.log`). Its immediate post-flash boot could not acquire a
tear edge and exhausted all three startup attempts (`product-restore-boot.log`,
lines 37–40). One ordinary serial hard reset then produced
`TINYDRAW_PANEL_HARD_RESET=1`, startup `pass=1`, and
`TINYDRAW_VECTOR_V2_READY` with 6,120 bytes of main-task stack free
(`product-restore-reset-boot.log`, lines 79–89).

After the controlled capture, the ordinary image was flashed again and
hash-verified (`product-final-restore-flash.log`). This restore boot reached
startup `pass=1` and `TINYDRAW_VECTOR_V2_READY` directly, again with 6,120 bytes
of main-task stack free (`product-final-restore-boot.log`, lines 36–37).

The device is currently running the ordinary latest product at commit
`eed37e7`.
