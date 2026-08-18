# Captured owner documents

## owner-torture-2026-08-19

Hand-drawn owner torture document, pulled from the device's `drawing`
partition (offset 0x1D0000) on 2026-08-19 ~00:30 via chunked
`esptool read_flash` over `/dev/cu.usbmodem1101` (one 64 KiB block needed
`--no-stub`). Battery integration is pending — for now these are the
archived corpus blobs.

- `owner-torture-2026-08-19.journal` — raw partition dump trimmed to the
  recovery-consumed length (3,342,336 bytes; the full 4 MiB image hashed
  `994217cd…cc158311`). Production `recover_authority_journal` reports:
  status=recovered, 802 transactions, sequence 864, discarded_tail=0,
  **102 active operations, 2,706 samples**.
  sha256 `2864bfb07936f9f2c7e3a39edb26cca5983315fa22c8c1e18421fd94df073e3e`.
- `owner-torture-2026-08-19.tdoc` — compact battery-corpus form (22,170
  bytes): `"TDOC"` magic, u32 op count, u32 sample count, then per-op
  `{tool u8, color u16, sample_count u16}` (packed) followed by all
  `CompactOperationSample` structs in operation order. Consumers replay
  through `log.append` so production validation rebuilds bounds/identity.
  sha256 `2fe696c221f6cc1f5df47b41798b9cb86398cd2eb891d3ef4b0ee7abbaf84768`.

Validate / regenerate either form with:

```sh
out/build/host-release/vector_v2/tinydraw_vector_v2_journal_corpus_check \
  testdata/documents/owner-torture-2026-08-19.journal \
  [testdata/documents/owner-torture-2026-08-19.tdoc]
```

Planned battery use (not yet wired): embed the `.tdoc` in the gate build
via `target_add_binary_data` (the ink-trace pattern in
`esp32/main/CMakeLists.txt`; sample bytes start at offset `12 + 5*ops`,
2-byte aligned, so a `CompactOperationSample` span can view the blob
directly), append into a local log, then run settle/cold instruments over
the 25% full overview grid plus centroid-centered window grids at 50–400%.
Note the one-sample-stroke dot bug
(`docs/receipts/vector-v2/ONE_SAMPLE_STROKE_DOT_BUG_2026_08_19.md`) if
this document contains tap-only operations.
