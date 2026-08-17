# Vector V2 autosave and recovery design

Status: single-journal baseline and drawn-document hardware recovery accepted
2026-08-17. Autosave-enabled performance joins the final optimization round.
Two-arena compaction and arena metadata are explicitly deferred by owner
direction.

This design persists Vector V2 drawing authority without moving flash work into
the ink path. It follows the product contract in `SHIP_CONTRACT.md` §7 and the
finish order in `V2_ROADMAP.md` Phase 5.

## Plain-English model

A completed Stroke, Undo, Redo, New, camera change, or tool change creates one
small **journal commit**. The commit records enough information to reproduce the
new document state, but it does not store overview or tile pixels. A commit is
recoverable only after its final marker reaches flash.

Most drawing commits contain only the newly completed Stroke. Undo, Redo, and
UI-state commits contain no sample payload. A checkpoint contains the complete retained operation list, including the
inactive Redo tail. The first Journal commit is a checkpoint; a later checkpoint
may resynchronize after a queue/allocation failure. Recycling a full partition
is deferred.

The app copies authority bytes before handing a commit to the flash worker. The
worker never reads `OperationLog`, `MaterializedCanvas`, navigation, or chrome
state. Flash erase/write latency therefore cannot lock or race drawing
authority.

## One durable authority

The persisted state is:

1. painter-ordered retained Stroke chunks and samples, including Redo;
2. the active chunk prefix;
3. monotonic document generation and operation-log epoch;
4. complete navigation state needed for zoom-cycle return positions;
5. selected drawing tool, size, palette page, and color;
6. the next nonzero Stroke identity, so a post-restart Stroke cannot merge with
   the final restored Stroke.

Overview pixels, tile pixels, occupancy, replay indexes, chrome sprites,
settled-AA pixels, and other caches are rebuilt after recovery.

## Deep module seams

### Portable journal seam

`authority_journal.h` owns the versioned wire format, CRC validation, final
commit marker, transaction semantics, interrupted-write recovery, and bounded
caller-owned restore storage.

```cpp
class AuthorityJournal {
 public:
  static std::optional<std::size_t> encoded_size(
      JournalChange, const OperationLog&);
  static bool encode(JournalChange, const OperationLog&, JournalState,
                     std::uint64_t sequence, std::span<std::byte> output);
  static JournalRecovery recover(const JournalSource&, std::size_t bytes,
                                 OperationLog&, JournalState&,
                                 std::span<std::byte> scratch);
};
```

The portable seam is tested with memory-backed sources. Tests truncate every
write phase and corrupt headers, payloads, CRCs, and markers. They observe only
recovered authority and state.

### ESP flash adapter seam

`VectorV2AutosaveStore` owns the `drawing` partition, the FreeRTOS writer task,
queueing, bounded sector erase/write, startup scanning, and visible full/error
status. It never erases a committed Journal entry.

```cpp
class VectorV2AutosaveStore {
 public:
  RestoreStatus restore(OperationLog&, NavigationState&, ChromeState&,
                        std::uint16_t& next_stroke_id);
  bool submit(JournalChange, const OperationLog&, const JournalState&);
  bool submit_checkpoint(const OperationLog&, const JournalState&);
  bool flush(TickType_t timeout);
  SaveStatus status() const;
};
```

Callers submit only after a logical state change. `submit()` copies and encodes
from one generation-checked authority view, then queues immutable bytes. The
worker owns and frees those bytes after writing. A queue/allocation failure
requests a later full checkpoint instead of publishing a partial delta.

## Wire and publication rules

- All integers have an explicit little-endian encoding; raw C++ object layout
  is never persisted.
- Every transaction has magic, format version, kind, total/payload lengths,
  sequence, resulting authority counters, persisted UI state, payload CRC, and
  header CRC.
- Stroke and checkpoint payloads encode operation metadata followed by exact
  compact samples. Sample offsets are rebuilt during recovery.
- The final marker repeats the transaction sequence and whole-transaction CRC.
  It is the last write. Missing or invalid markers leave the prior commit as
  the recovery point.
- Recovery validates a complete transaction before applying it. Structural or
  semantic failure stops the scan and preserves the last valid state.
- Append commits declare the active prefix they replace. This reproduces
  branch-after-Undo without retaining a second history owner.
- Checkpoints replace recovered state; later commits replay normally.

## Flash layout and interruption behavior

The 3 MiB `drawing` partition is one append-only journal. Fresh initialization
erases it once, writes the first checkpoint body, and writes that checkpoint's
commit marker last. Each transaction occupies a whole number of 4 KiB sectors;
its marker is the final 16 bytes of that sector-aligned extent. Later
transactions append into fresh sectors. Startup scans to the newest valid
marker and resumes at its aligned end. An interrupted tail therefore begins at
a sector boundary and can be erased without touching the prior Recovery point.
This deliberately trades packing density for simple, exact tail recovery while
two-arena compaction remains deferred.

When no complete next transaction fits, autosave reports `full` and preserves
all existing Recovery points. It does not erase or compact committed data. A
minimum-size transaction consumes 4 KiB, so the current 3 MiB partition holds
at most 768 Journal commits; multi-sector long Strokes reduce that count.
Two-arena compaction and metadata—which would safely recycle a full partition
while retaining the old Recovery point—are deferred for a later project round.

No destructive physical power-cut test is required. Host fixtures simulate
interruption after every transaction write phase.

## Product scheduling

- A completed Stroke queues one append commit after finger-up. The in-progress
  Stroke is never persisted.
- Undo, Redo, and New queue their authority commit after successful
  publication.
- Pan, zoom, tool, size, and color changes queue state commits; repeated
  state-only changes may be coalesced while an equivalent newer state remains
  queued.
- The flash worker runs below the touch sampler. Its only units of flash work
  are one sector erase, one bounded data write, or one final marker/metadata
  write.
- Export and power-risk transitions call `flush()` before changing hardware
  ownership. A timeout leaves drawing mode active and reports a save failure.
- Product performance gates run with the same store and worker enabled.

## Startup recovery

1. Scan aligned transactions from the beginning of the `drawing` partition
   through the last complete journal commit.
2. Restore retained operations, active prefix, generation, epoch, navigation,
   chrome selections, and next Stroke identity.
3. Replay only the active prefix into a fresh 25% overview.
4. Publish that overview at the recovered generation; all higher-zoom tiles
   begin cold and rebuild through the existing producer.
5. If no valid V2 transaction exists, start a blank document and schedule its
   first checkpoint. If a later transaction is incomplete or corrupt, recover
   the prior commit and schedule a checkpoint after erasing only the aligned
   tail on the background worker.

## TDD slices

1. One checkpoint round-trips retained operations, Redo boundary, generation,
   epoch, navigation, tool state, and next Stroke identity.
2. A multi-chunk Stroke append after Undo replaces the Redo tail exactly.
3. Undo, Redo, state-only, and New commits replay exactly.
4. Truncation at every byte and corruption of each integrity field recover the
   prior complete commit.
5. OperationLog restore rejects malformed/capacity-exceeding snapshots without
   mutation.
6. A single-journal adapter resumes at the first erased byte and reports full
   without erasing any Recovery point.
7. Product startup rebuilds overview pixels exactly and queues later mutations
   without touching the live-ink presentation path.
8. ESP builds, autosave-enabled performance gates, and a normal firmware flash
   close the integration. Physical power-cut testing remains excluded by owner
   direction.
