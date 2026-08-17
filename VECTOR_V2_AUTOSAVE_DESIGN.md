# Vector V2 autosave and recovery design

Status: accepted implementation plan, 2026-08-17.

This design persists Vector V2 drawing authority without moving flash work into
the ink path. It follows the product contract in `SHIP_CONTRACT.md` §7 and the
finish order in `V2_ROADMAP.md` Phase 5.

## Plain-English model

A completed Stroke, Undo, Redo, New, camera change, or tool change creates one
small **journal commit**. The commit records enough information to reproduce the
new document state, but it does not store overview or tile pixels. A commit is
recoverable only after its final marker reaches flash.

Most drawing commits contain only the newly completed Stroke. Undo, Redo, and
UI-state commits contain no sample payload. A periodic checkpoint contains the
complete retained operation list, including the inactive Redo tail. Checkpoints
bound recovery work and let the writer switch safely between two flash arenas.

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

`VectorV2AutosaveStore` owns the `drawing` partition, two fixed arenas, the
FreeRTOS writer task, queueing, arena selection, bounded sector erase/write,
and promotion of a checkpoint by metadata-last arena commit.

```cpp
class VectorV2AutosaveStore {
 public:
  RestoreStatus restore(OperationLog&, NavigationState&, ChromeState&,
                        std::uint16_t& next_stroke_id);
  bool submit(JournalChange, const OperationLog&, const JournalState&);
  bool checkpoint_needed() const;
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

The 3 MiB `drawing` partition is split into two equal arenas. Each arena
reserves its first 4 KiB sector for metadata and stores transactions
sequentially afterward.

Normal commits append only into erased bytes. When remaining space reaches the
largest possible authority checkpoint reserve, the app submits a checkpoint.
The worker erases the inactive arena one sector at a time, writes and validates
the checkpoint, then writes the new arena metadata last. Until that final
metadata write, startup continues to select the old arena. Later commits append
after the promoted checkpoint.

No destructive physical power-cut test is required. Host fixtures simulate
interruption after every transaction and arena-promotion write phase.

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

1. Validate both arena metadata sectors and choose the newest committed arena.
2. Scan from its checkpoint through the last complete journal commit.
3. Restore retained operations, active prefix, generation, epoch, navigation,
   chrome selections, and next Stroke identity.
4. Replay only the active prefix into a fresh 25% overview.
5. Publish that overview at the recovered generation; all higher-zoom tiles
   begin cold and rebuild through the existing producer.
6. If no arena is valid, start a blank document and schedule its first
   checkpoint. If valid data becomes corrupt after at least one commit,
   recover the prior commit and surface a recovery warning rather than erasing
   it.

## TDD slices

1. One checkpoint round-trips retained operations, Redo boundary, generation,
   epoch, navigation, tool state, and next Stroke identity.
2. A multi-chunk Stroke append after Undo replaces the Redo tail exactly.
3. Undo, Redo, state-only, and New commits replay exactly.
4. Truncation at every byte and corruption of each integrity field recover the
   prior complete commit.
5. OperationLog restore rejects malformed/capacity-exceeding snapshots without
   mutation.
6. Two-arena promotion fixtures select old metadata until the new checkpoint is
   fully committed, then select the new arena.
7. Product startup rebuilds overview pixels exactly and queues later mutations
   without touching the live-ink presentation path.
8. ESP builds, autosave-enabled performance gates, and a normal firmware flash
   close the integration. Physical power-cut testing remains excluded by owner
   direction.
