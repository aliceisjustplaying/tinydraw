# TinyDraw agent/session archaeology

Generated 2026-08-19. Development evidence cutoff: 2026-08-19 00:33 local-session start. Later archaeology/reconstruction sessions are excluded from the development usage totals.

## Executive finding

**HIGH confidence:** TinyDraw was not built by a single linear agent. It was an extended human-directed loop in which Alice repeatedly supplied the strongest falsifiers: physical feel, glass observations, benchmark-definition challenges, and scope corrections. The agents were most effective after those observations were converted into bounded experiments with receipts. They were least effective when they extrapolated from host behavior, trusted self-reported synchronization, changed several variables at once, or treated a passing internal metric as a product verdict.

The decisive reasoning pivots were:

1. whole-frame reraster/upload → dirty tiles;
2. tile speed → render/present interaction architecture;
3. raster-only → vector authority plus a disposable raster viewport cache;
4. “vector pan should be close” → direct measurement showing raster pan at 25.45 ms and synchronous vector pan 4–8× slower;
5. capacity tweaking → cache identity, tile-class census, and structural storage work;
6. software/TE confidence → an optical positive-control protocol;
7. beam racing → a rising-edge row-zero boundary sweep;
8. average staging speed → a per-strip staging invariant;
9. “reserved” export RAM → modal/evictable memory supporting undo preservation.

## Evidence scope and confidence

### Searched stores

- Pi: the global `~/.pi/agent/sessions/` store, including sessions launched from `~`, project directories, temporary review worktrees, and nested agent sessions.
- Claude Code: the global `~/.claude/projects/` store plus project memory.
- Codex: the global `~/.codex/sessions/2026/08/` store and prompt index, including sessions launched outside the repository.
- Grok Build: the global Grok session store exposed by `ccusage`, including direct review sessions that the first pass missed.
- Codex prompt index: `$HOME/.codex/history.jsonl` (useful for discovery, no usage accounting).
- Cursor: local stores were searched; no TinyDraw-linked development sessions were found. **MEDIUM confidence** because Cursor’s local formats and retention can vary.

Working directory was used only to locate candidate logs. Attribution was made from substantive user prompts and assistant responses; sessions begun in `~` or another directory count when their content is about TinyDraw, and smoke tests or unrelated work inside the repository do not.

### What is not available

- No provider invoice or project-level payment ledger was found. Therefore actual money paid for TinyDraw is **not recoverable** from local evidence.
- Pasted “GPT 5.6 pro/oracle” web reviews survive as local artifacts or copied text, but their original web-session tokens, settings, and billing do not.
- Raw Fuji footage `$HOME/Desktop/DSCF0665.MOV` was referenced in-session; it is not in the repository evidence directory. Its durable verdict and capture metadata survive in the protocol receipt.

## Chronology of reasoning and handoffs

### 1. Aug 9: Mac-first prototype and dirty tiles

The opening brief asked for a micro-tldraw on a Waveshare ESP32-S3 before the hardware arrived. When the largest pen became “VERY LAGGY” on an M1 Pro, Alice proposed dirty-tile stroke rasterization. Sol identified the actual whole-frame behavior: every primitive was rerasterized and the entire framebuffer uploaded each frame, so dirty tiles would bound work to stable coverage plus the changing tail. This is the earliest clear example of the user proposing the architectural direction and the model supplying the mechanism.

Source: [origin Pi session]($HOME/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:4), messages `43135de2` (2026-08-09 17:08:12), `e0e4e22d` (20:09:52), `ba8f2b05` (20:11:08), `c92a84eb` (20:11:14). Model: `gpt-5.6-sol`. **HIGH confidence.**

### 2. Aug 10: hardware invalidated the Mac mental model

The physical CO5300/CST820 board arrived and immediately felt “slow, angular.” Sol first attributed touch trouble to long render-blocking intervals. Alice then described the visible failure more precisely: tile-at-a-time updates and an interaction that felt wrong even after micro-timing improvements. Sol revised the model from “optimize tile work” to “compose the dirty frame offscreen, present a contiguous region, and service touch independently.”

A diagonal tile-rejection optimization made performance worse; Sol acknowledged that its math cost exceeded the work skipped on ESP32 and reverted it. Later, 1 kHz polling produced only 19–35 distinct points in a fast stroke versus 134 slow points, establishing the CST820 report cadence as the source of angularity; stronger streamline introduced lag and was also reverted.

Source: [origin Pi session]($HOME/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:2590), messages `8717f762`, `a2c8855b`, `83cbaff3`, `ad26fed9`, `b3fef0b8`, `b313dbb2`, `456f981d`, `2f0dde03`, `da58c522`. **HIGH confidence.**

### 3. Aug 11: vector feasibility changed from ideology to measurement

Alice asked for a real infinite vector canvas, then explicitly called out that the agent was flailing. Sol stopped speculative changes and asked Fable for a pass/fail feasibility verdict. Fable reported 2.1–5.1× benchmark improvements but ruled a full rebuild per frame dead; vector storage passed only with incremental tile caching.

Phase 2 then exposed three important corrections:

- The first device prototype overcommitted PSRAM with a third ~330 KB viewport.
- Its apparent pan evidence was invalid because the strips were accidentally empty; zoom preview was ~550 ms and handwriting mismatched.
- After correction, pan was 95–105 ms, handwriting 197–208 ms, zoom preview ~65 ms, and touch stayed under 2.03 ms.

Sol then made the most consequential wrong extrapolation of the project: it suggested a roughly 45 ms display floor and implied raster pan should be similar. Alice rejected that based on physical feel. Direct measurement showed the raster product path did no reraster or framebuffer shift at all: it changed origin and streamed a strided AMOLED window. The exact raster pan was min/median/max 25.445/25.452/25.458 ms (~39 FPS); synchronous vector pan was 4–8× slower. The architecture changed to **vector document authority + disposable raster WorldCanvas pan cache**. Fable agreed and flagged zoom and cache-outrun as the remaining unproved cases.

Source: [long origin Pi session]($HOME/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:8063), messages `721b54f2`, `ae121db9`, `4efd7e50`, `1f039f30`, `4f40ca42`, `363aae75`, `72942deb`, `10ec0659`, `e0687923`, `edad0868`, `d7019ae2`, `03ef3bca`, `214edc1d`, `9b8448f0`, `85e5239a`, `1f9256dd`, `bd78f5b4`. **HIGH confidence.**

The measuring firmware itself crashed because an ~8 KB report was put on a 6 KB task stack; Sol owned the mistake (`9b8448f0`/`85e5239a`). This is a useful reminder that the benchmark harness repeatedly became part of the failure surface.

### 4. Aug 11–12: compaction loss and the “production island”

The next session begins with Alice explicitly saying model switching caused compaction to fail and context to be lost. She pasted the prior result into the successor session. Later, the interactive cache benchmark showed widespread misses and ~5 s zoom switches, disproving a premature “feasible” reading.

Alice then requested a five-day production push and external second eyes. One delegated session blocked while the root waited; Alice had to cancel it. External feedback correctly identified a global LOD fallback bug and likely PSRAM pressure, but overstated that the hot loop was “entirely” PSRAM; Sol separated verified facts from unmeasured speedup claims.

By Aug 12 evening a second eye reported that the branch still shipped the raster app plus a vector prototype rather than a production module. Alice chose a “clean production island inside the repo,” which became the seam for the production work.

Sources: [successor Pi session]($HOME/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-12T14-32-39-195Z_019ff663-bc1b-7d26-b1cc-79a9ff33db2a.jsonl:4), messages `5d70ae75`, `c0fa1ebc`, `2786f6a8`, `99bf77be`; [evening continuation]($HOME/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-11T21-25-39-963Z_019ff2b7-7ffb-7360-bc57-0e0b7b448998.jsonl:2293), `3475e71d`/`5509d5a9`; [overnight production]($HOME/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-12T20-46-27-703Z_019ff7b9-f777-7cda-b390-3e9e14444f44.jsonl:114), `35775812`/`a33904fa`. **HIGH confidence.**

### 5. Aug 12–14: reviewer specialization and cache architecture

Alice authorized two Fable fix/review exchanges and two Grok rounds. The local evidence supports a stable division of labor:

- **Fable:** architecture, invariant review, performance-campaign ownership, and careful “safe to continue” gates.
- **Grok 4.6:** adversarial bug finding. It found the raster-work budget unsigned underflow (up to 95 extra segments) and a >16-tile XL case. At xhigh it also repeatedly overthought/hung; Alice later requested high.
- **Sol:** long-running implementation, hardware loops, integration, and handoffs.

Fable’s Claude Code memory preserves concrete review outcomes: a proven slot-metadata alias exploit was closed, an output alias gap remained; OperationLodStore’s two findings were fixed with 221 ASan cases; overview publication was clean with a latent halo-margin coupling. See [alias review memory]($HOME/.claude/projects/-Users-alice-src-tries-2026-08-09-espdraw/memory/tinydraw-alias-review-status.md:11), [LOD review memory]($HOME/.claude/projects/-Users-alice-src-tries-2026-08-09-espdraw/memory/tinydraw-lod-store-review-status.md:11), and [overview review memory]($HOME/.claude/projects/-Users-alice-src-tries-2026-08-09-espdraw/memory/tinydraw-overview-publication-review-status.md:11).

Manual Gate 1 panning still repeatedly rerendered. Alice’s friend challenged the slot-growth response as a “skill issue” and asked for mechanical sympathy/demoscene thinking. This changed the work from “make the cache larger” to tile-class census, raw/paper catalogs, identity analysis, and structural reuse. Later manual testing reported no repeated rerenders and acceptable panning.

The improvement was not monotonic. Night navigation reduced tearing but the next morning 400% cold paths took up to 10 s. Later checked receipts supported a p95 trajectory from 23.66 s to 0.971 s, but the project repeatedly learned that a win on one corpus or zoom could regress drawing or another zoom.

Source: [overnight production Pi session]($HOME/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-12T20-46-27-703Z_019ff7b9-f777-7cda-b390-3e9e14444f44.jsonl:417), messages `c4175333`, `0d6d894f`, `8d1926ae`, `c6d093f7`, `46ebd483`, `321eb75e`; the user’s later role summary says Fable was stronger at architecture and Grok was good at bugs but overthought. **HIGH confidence for use/roles; MEDIUM confidence that model identity caused the difference, because task mix differed.**

### 6. Aug 14–15: priority order and benchmark-definition disputes

Alice formalized product priority as drawing latency first, cold render second, pan third, export fourth. She accepted known regressions temporarily to work on UI but required them to be documented. Fable’s performance sessions were repeatedly redirected by physical observations: a claimed warm-pan improvement conflicted with another agent using the same board; historical “<500 ms” memories turned out to use subtly different metrics; and the test battery lacked the “evil crosshair/hairline” corpus that later exposed pathological cache behavior.

Source sessions include `2026-08-14T13-50-13-181Z_01a00089-...` (Fable xhigh cold/long-stroke campaign), `2026-08-14T17-48-49-844Z_01a00164-...` (Sol priority/handoff), and `2026-08-14T20-51-07-663Z_01a0020a-...` (Fable round two). Messages `500fd477`, `6f4ca3c2`, `a519f2dd`, `faa98b3a`, `4b5d30a1`, `902d4b6f`, `96fc7c68`. **HIGH confidence.**

### 7. Aug 15: Fuji X-T5 tearing experiment — exact reconstruction

This is the best-preserved scientific episode in the project.

#### Why software evidence was insufficient

The session opened by auditing a prior tearing attempt. Fable found it scientifically compromised: no control arm ran; policy, TE edge, and requested clock changed together; the 51 ms number measured the probe’s own serial wait/compose path; and the 120 fps footage had never been classified. The internal `tear_synchronized` signal only proved that software had observed an edge, not what the glass displayed.

Alice’s wording at 19:35:08 (`6b70e446`, line 40) was explicit: the earlier camera/agent had calculated “something like 20 fps,” she had objected that this was not a clean measurement, was frustrated with agent flailing, and wanted to finish scientifically. Fable separated correctness from pacing: camera for tears/notches, software timestamps for cadence.

The software-only characterization then showed:

- TE period 16.773 ms, ±16 µs; high 578 µs; ISR→task p50 9 µs; 0/2,100 timeouts. TE jitter/scheduling was not the cause.
- Requested 40/50/60 MHz all produced the same ~18.0 ms full-frame wall; the real bus was 40 MHz.
- Writer 27.2 rows/ms and modeled beam ~26.7 rows/ms were near parity, making a rising-edge sweep optically uncertain.
- GETSCANLINE `0x45` and six nonzero control reads all returned zero. The QSPI read path was unusable, so no software beam-position oracle existed.

The zero-read evidence is in the session tool result `aa3acfd1` at line 166 and the durable [hardware limits receipt](docs/receipts/hardware/CO5300_PANEL_LIMITS_2026-08-15.md:103). The distinction between software physics and optical verdict is explicit at receipt lines 10–12 and 131–134.

#### User’s camera offer and protocol

At 19:41:57, Alice said: “actually i just realized i have a fuji xt5 … i think it can do 240fps at 1080p. i have a 85mm on it and no tripod.” This is message `59d9de80`, line 50. She clarified it was a Sigma 56 mm f/1.4 (85 mm equivalent) at `fd95a1b5`, line 54.

Fable’s proposed discriminator was persistence: at 240 fps the 16.8 ms panel period spans about four camera frames, while a camera straddle should be transient. The final one-take protocol was preregistered:

- X-T5 1080/240 high-speed, manual focus, f/2.8–f/4.
- Alice found the flicker-free setting and offered 1/1024 at 20:42:17 (`426a5808`, line 237); this became the preferred shutter.
- Full-field red/green alternation, corner fiducials, cell ID, blue interstitials, and guard columns.
- Run order 4 → 1 → 5 → 2 → 3.
- Cell 4 was an unsynchronized positive control that **must tear**; if it did not, no clean result counted.
- Cell 1 was rising-edge, row-zero, full 448-row sweep at a software-measured 29.4 FPS.
- Camera judged correctness only; serial receipts judged cadence.

The exact preregistration survives at [PROTOCOL.md](benchmark-results/blockB-optical/PROTOCOL.md:1), especially lines 8–16, 23–41, and 45–66. Session messages: `474fbcf0` line 41, `5fcc77d3` line 53, `7a9f4166` line 157, `2056ccea` line 194.

#### Capture and classifier observations

At 20:53:42 Alice supplied `$HOME/Desktop/DSCF0665.MOV`: “this is over 2 minutes fingers crossed it has what you need do your thing” (`cbea275b`, line 251). The clip was 1080p, 37,920 frames, ~158 s at 240 fps.

The automated full-video path initially flailed for ~40 minutes on registration, rotation metadata, ID-strip location, and a frame-counter bug. Alice called this out at 21:07 and again at 21:14–21:15: “we've been at this for... over an hour?” and “now its 10:15pm” (`4368124c` line 335; `d916e623` line 338). Fable then used the cheaper sufficient instrument: two 24-frame contact sheets.

Observed result at 21:16:35 (`2c26ce36`, line 345):

- Cell 4: every frame a torn red/green sandwich, split position wandering. The positive control proved the instrument could say TEAR.
- Cell 1: solid field → one boundary moving monotonically downward over ~4 camera frames → solid field; no frozen split, double boundary, or notch.
- An automated 1,495-frame cell-1 slice agreed: 0 tears, 0 anomalies.

The append-only durable result is [PROTOCOL.md lines 104–128](benchmark-results/blockB-optical/PROTOCOL.md:104). The raw clip itself is not repository-local; **HIGH confidence in the verdict, MEDIUM confidence in later reanalysis without the raw clip.**

#### Hypothesis falsified and model that followed

The footage falsified the need for the complex beam-race/wrap/heal policy as the product correctness mechanism. A plain **wait for TE rising edge, start at row zero, stream top-to-bottom** sweep was optically clean at 29.4 FPS. It also falsified the idea that software synchronization alone was an optical oracle: the unsynchronized positive control and glass were necessary.

Fable (`anthropic/claude-fable-5`, thinking level `high`) owned this session. It immediately flashed the already-existing boundary-top-sweep/rising/40 MHz product path. Alice reported that tearing appeared fixed on glass but found a zoom-control stroke-distortion bug (`6d934d90`, line 354). PANSEQ then showed product panning was only 19.9 FPS because app composition was not the minimal probe.

Wave 2’s staging compositor recovered ~29.5 FPS but introduced a fixed tear. Alice’s exact observation at 22:59:32 (`1ff48c60`, line 509) was that tearing was back at one predictable spot near the top grey edge of the minus button. Reverting burst pacing moved, rather than removed, the tear: at 23:06:19 (`b726fd7e`, line 526) it shifted to roughly two-thirds down the minimap. That pair falsified the simpler “burst pacing alone” explanation and identified a deterministic writer/beam crossover caused by expensive dynamic minimap strips.

Alice then explicitly requested “a new gpt 5.6 sol high subagent” (`fc08b05f`, line 528). The handoff constrained the work to: instrument each strip → prebuild the minimap patch → prove every strip stages faster than its wire time → only then recover pacing. The Sol subagent landed and reverted experiments according to that invariant. At 23:41:11 Alice reported “yes tearing seems to be gone” and asked the agent to stop trying to put her to sleep (`bb799818`, line 567). Fable recorded the glass closure (`466433a0`, line 568).

Primary session: [Fuji/tearing Pi session]($HOME/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-15T19-28-19-209Z_01a006e5-8109-7f08-85ca-ae3f19b8d435.jsonl:40). Model declaration at session start: `claude-fable-5`, `high`. **HIGH confidence.**

Later Codex product confirmation on Aug 16: Alice said “as far as I can tell… tearing is fixed”; Sol recorded product pan optically clean and noted the positive control validated the method. Source: [Codex Aug 16 session]($HOME/.codex/sessions/2026/08/16/rollout-2026-08-16T08-55-46-01a00991-d148-7d72-b0ef-7b390e7a9462.jsonl:790), ordinals 790/795, timestamps 08:29:03.607/08:29:09.755. **HIGH confidence.**

### 8. Aug 16–18: Codex finish-line work and human scope corrections

Codex/Sol became the main finish-line implementation/review harness. Three particularly revealing corrections survive:

- Alice stopped a chrome-cache rabbit hole and required critical fixes and measurements first. [Aug 16 Codex]($HOME/.codex/sessions/2026/08/16/rollout-2026-08-16T08-55-46-01a00991-d148-7d72-b0ef-7b390e7a9462.jsonl:1815), ordinals 1815/1820.
- She required glass evidence for every ink regression and later rejected “angle-ey” circles despite acceptable metrics. Same session, ordinals 2281/2286 and 2471/2476.
- Codex accidentally removed Raster V1 as “superseded.” Alice corrected that classification; Sol restored it and confirmed it on glass. [Aug 17 Codex]($HOME/.codex/sessions/2026/08/17/rollout-2026-08-17T20-17-26-01a01128-433d-7660-94c0-d9c17784c655.jsonl:1817), ordinals 1817/1822 and 2211/2216.

A handoff packet was prepared for “GPT 5.6 pro,” then Alice’s note overrode parts of it. The next Sol session implemented the resulting split: occupancy correctness, ring-local presentation, and touch urgency. Sources: [Aug 17 handoff]($HOME/.codex/sessions/2026/08/17/rollout-2026-08-17T22-13-15-01a01192-49d7-73b2-9fdc-93db5e34880c.jsonl:3350), ordinals 3350/3355 and 4071/4080; [Aug 18 execution]($HOME/.codex/sessions/2026/08/18/rollout-2026-08-18T09-22-51-01a013f7-542c-7790-bb7d-2539d9d2c888.jsonl:9), ordinals 9/14 and 162/167. **HIGH confidence.**

### 9. Aug 18: undo forced a memory-model revision; AA forced better torture tests

During the final Fable performance round, undo exposed a jarring rerender/hourglass. Alice reframed export RAM as a modal, evictable resource and asked to use all remaining RAM—and eventually flash—for undo preservation. This led to copy-on-write preserve tiles and a flash-backed takeover rather than accepting reraster on undo. The conceptual change came directly from the user challenging the “sacred” allocation.

The AA work repeated a familiar warning: host benchmarks on an M1 had often predicted wins that regressed ESP32. A late phantom-dot/hairline artifact finally caused the “evil hairline” corpus to become a permanent torture test, days after Alice first noticed that the battery lacked it.

Source: `2026-08-18T19-04-33-169Z_01a01642-...` messages `8aeabae0`, `05b24f5e`, `ae3b4b56`, `7eb4dbf5`, `93f510aa`, `aad820d4`, `0c0e83fd`; `2026-08-18T22-06-31-518Z_01a016e9-...` messages `de3f2654`, `c23f7244`, `c187784b`. Models: Fable xhigh/high. **HIGH confidence.**

## Model and agent roles

| Model/tool | Observed role | Evidence-backed assessment |
|---|---|---|
| `gpt-5.6-sol` via Pi/Codex | Primary implementation, hardware flashing/capture, integration, long autonomous sessions, subagents | Largest implementation footprint. Strong after a measured discriminator existed; prone to long context and occasional confident extrapolation before direct measurement. |
| `claude-fable-5` via Pi/Claude Code | Architecture, performance campaigns, independent review, preregistered experiments, invariant gates | Produced the vector conditional-pass verdict and Fuji protocol; also overbuilt the video classifier before falling back to contact sheets. |
| `claude-opus-5` via Pi | Initial research and early review/presentation | Small footprint relative to Sol/Fable; no evidence it drove the production phase. |
| `grok-4.6` via Pi/Grok Build | Adversarial bug finding | Found concrete issues such as unsigned budget underflow. Xhigh repeatedly hung/overthought; user moved it to high. |
| “GPT 5.6 pro/oracle/big brother” | External second opinion and handoff target | Survives only through copied local artifacts and user labels. Original session settings and usage are inaccessible. |

Reasoning levels `high` and `xhigh` are explicitly recorded and were intentionally requested. **CONFIRMED use; INCONCLUSIVE causal impact.** Tasks, prompts, context sizes, and hardware conditions changed too much to attribute better outcomes to effort level alone.

## Failed proposals and reversals

| Proposal/assumption | Failure evidence | What replaced it |
|---|---|---|
| Diagonal tile rejection would reduce device work | User reported it “bit worse”; predicate math cost more than skipped work | Revert; focus on present architecture |
| Stronger streamline would cure fast-stroke angularity | Added lag; controller emitted too few distinct samples | Revert; accept/controller-aware smoothing |
| Raster and vector pan shared a ~45 ms display floor | Exact raster path measured 25.45 ms and did no reraster | Vector authority + raster pan cache |
| First Phase 2 prototype proved vector interaction | Third viewport overcommitted PSRAM; pan strips were empty; zoom ~550 ms | Reuse buffers, correct workload, remeasure |
| Larger cache/448 slots was the main answer | Repeated rerenders and adversarial identity patterns | Tile census, identity reuse, paper/raw catalogs |
| 60 MHz panel bus and 15 Mpixel/s writer | 40/50/60 requests measured identically; actual 40 MHz/10 Mpixel/s | Hardware truth table at real clock |
| Software TE synchronization proved no tearing | Visible tearing coexisted with self-validating metrics; QSPI readback all zero | Optical positive control + high-speed footage |
| Beam racing/wrap/heal complexity was necessary | X-T5 showed rising-edge row-zero sweep clean; unsynced control tore | Boundary-synchronized monotonic sweep |
| Average staging > wire speed guaranteed tear safety | Fixed tear moved when pacing changed; expensive strips violated local budget | Per-strip staging-before-wire invariant |
| Replay operation bbox index would cut adversarial cold work ≥25% | 90% fewer scanned ops gave 10.9%; adversarial 1,038 candidates genuinely intersected | Segment-level pruning, faster painter, or checkpoints |
| Raster V1 was superseded | User corrected removal; glass test restored it | Keep V1 as distinct product/reference path |
| Export RAM was permanently unavailable | Undo rerender remained unacceptable | Treat export as modal; use RAM/flash for undo preservation |

## Handoffs and coordination failures

- **Compaction/model-switch loss:** explicit at successor message `5d70ae75`; user had to paste the result back in.
- **Root waiting on subagent:** user canceled a session that had blocked instead of progressing (`99bf77be`).
- **Concurrent device ownership:** warm-pan and cold/long-stroke agents both used the board; Alice stopped them (`a519f2dd`).
- **Shared git index:** multiple Aug 15 night agents staged into one index; pathspec commits were used as a recovery.
- **Provider instability:** Grok xhigh loops/hangs, Fable rate limits, and WebSocket errors are recorded. The work often survived because changes and receipts were already on disk.
- **Human as physical arbiter:** agents could flash and capture serial autonomously, but touch feel, optical tearing, angularity, and UI distortion repeatedly required Alice’s eyes/hands. Claims made before that step were frequently revised.

## Usage accounting

The project total is **4,590,968,837 processed tokens** through the core development/review cutoff. It includes cached context because that is how the agent logs and `ccusage` report processed tokens; it is not unique semantic content. Work belonging to other repositories is excluded.

### Provider totals

| Provider | Pi | Direct agent | Project total | Share | API-equivalent cost |
|---|---:|---:|---:|---:|---:|
| OpenAI | 1,877,369,471 | 1,344,356,467 via Codex | **3,221,725,938** | 70.18% | $2,279.65 |
| Anthropic | 1,201,103,249 | 17,746,237 via Claude Code | **1,218,849,486** | 26.55% | $1,784.55 |
| xAI | 5,939,013 | 144,454,400 via Grok Build | **150,393,413** | 3.28% | $96.39 |
| **Total** | **3,084,411,733** | **1,506,557,104** | **4,590,968,837** | **100%** | **$4,160.59** |

### Reproduction and attribution

The installed provider-aware fork was run as:

```sh
BIN=$HOME/src/tries/ccusage-provider-breakdown/rust/target/debug/ccusage
$BIN daily -s 2026-08-09 --by-provider --summary --breakdown
```

Aug 9 is the correct start date: the first surviving TinyDraw Pi session begins that afternoon and the first repository commit is at 18:20 local; Aug 8 usage contains no TinyDraw content. The end boundary is the Aug 19 release-era development session cutoff stated at the top of this report.

All global session candidates were classified from conversation content, independent of their working directory. Pi totals use the provider-aware fork’s stable message-ID accounting. Direct Claude Code, Codex, and Grok totals use `ccusage` session accounting after content classification. The core total includes nested agents doing TinyDraw work and excludes other repositories, unrelated coding, smoke tests, the later archaeology, and editorial/blog work.

The current archaeology is still accumulating and is intentionally not rolled into the core-development number. GPT-5.6 Pro web reviews are known TinyDraw work but have no recoverable local token records, so every measured total is an undercount of the full project.

The `$4,160.59` is `ccusage`/log-derived **API-equivalent pricing**, not proof of money paid. Subscription and OAuth usage may not incur those per-token charges, and no provider invoice or web-review billing record was found. **HIGH confidence in the locally logged, content-attributed arithmetic; LOW confidence in actual-spend inference.**

## What cannot be concluded

- That one model was intrinsically “best.” Roles and prompts were systematically different.
- That xhigh caused better code. It also caused documented hangs; there is no controlled comparison.
- That all external-review usage is counted. Pasted web reviews lack original logs.
- That the Fuji classifier completed the full 37,920-frame segmented run. The durable receipt explicitly leaves cells 2/3/5 pending; the decisive cell-1 verdict used 1,495 automated frames plus contact-sheet inspection.
- That “tear free” came from the minimal sweep alone in the eventual product. The minimal experiment selected the transport policy; later product correctness also required per-strip staging invariants and optical revalidation.

## Source locator

- Main Aug 9–11 Pi history: `$HOME/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl`
- Post-compaction continuation: `$HOME/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-11T21-25-39-963Z_019ff2b7-7ffb-7360-bc57-0e0b7b448998.jsonl`
- Production-island overnight: `$HOME/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-12T20-46-27-703Z_019ff7b9-f777-7cda-b390-3e9e14444f44.jsonl`
- Fuji/tearing experiment: `$HOME/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-15T19-28-19-209Z_01a006e5-8109-7f08-85ca-ae3f19b8d435.jsonl`
- Durable optical protocol: `benchmark-results/blockB-optical/PROTOCOL.md`
- Hardware truth table: `docs/receipts/hardware/CO5300_PANEL_LIMITS_2026-08-15.md`
- Claude Code project: `$HOME/.claude/projects/-Users-alice-src-tries-2026-08-09-espdraw/`
- Codex tearing/finish-line session: `$HOME/.codex/sessions/2026/08/16/rollout-2026-08-16T08-55-46-01a00991-d148-7d72-b0ef-7b390e7a9462.jsonl`
- Codex V1 restoration session: `$HOME/.codex/sessions/2026/08/17/rollout-2026-08-17T20-17-26-01a01128-433d-7660-94c0-d9c17784c655.jsonl`
- Codex handoff packet: `$HOME/.codex/sessions/2026/08/17/rollout-2026-08-17T22-13-15-01a01192-49d7-73b2-9fdc-93db5e34880c.jsonl`
- Codex packet execution: `$HOME/.codex/sessions/2026/08/18/rollout-2026-08-18T09-22-51-01a013f7-542c-7790-bb7d-2539d9d2c888.jsonl`
