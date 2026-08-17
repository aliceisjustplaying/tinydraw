# Measurement artifact retention

Commit the short receipt for each accepted measurement: commit SHA, build flags,
hardware/setup, commands, decisive counters, verdict, and the smallest useful
counterexample. Keep canonical input traces and approved deterministic snapshots
when they are active regression fixtures.

Store raw serial logs, videos, extracted capture frames, profiler dumps, and
build transcripts outside Git. A receipt must name that external artifact and
record its checksum when reproducing the verdict depends on it. Promote a raw
artifact into Git only when it becomes a compact regression fixture.

Existing historical evidence remains available; this policy governs new runs.
