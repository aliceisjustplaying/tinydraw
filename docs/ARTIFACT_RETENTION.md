# Measurement artifact retention

Commit the short receipt for each accepted measurement: commit SHA, build flags,
hardware/setup, commands, decisive counters, verdict, and the smallest useful
counterexample. Keep canonical input traces and approved deterministic snapshots
when they are active regression fixtures.

The current tree excludes raw serial logs, hardware console transcripts, videos,
extracted capture frames, profiler dumps, build transcripts, and generated
benchmark binaries. A receipt must name the external artifact and record its
checksum when reproducing the verdict depends on it. Promote a raw artifact into
Git only when it becomes a compact regression fixture.

Large review bundles do not belong in the source tree once their review text is
archived. A real device image may remain when it exercises a parser or recovery
path that synthetic fixtures cannot cover; store it losslessly compressed and
record the decompressed hash. Keep photographs cropped to the pixels needed to
show the defect.

Pre-cleanup raw evidence is preserved at tag
[`v2-feature-complete-pre-cleanup`](EVIDENCE_ARCHIVE.md). This policy applies to
the current tree and all new runs.
