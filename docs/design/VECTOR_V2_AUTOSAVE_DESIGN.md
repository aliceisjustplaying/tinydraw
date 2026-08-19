# Vector V2 autosave and recovery contract

Status: the authority-only append journal and host recovery fixtures are
implemented. The earlier physical drawn-document receipt covered the superseded
session-state schema; current-head physical recovery and normal-product
performance with real journal writes remain release work. Safe partition
recycling remains deferred.

Author decision, 2026-08-17: persistence contains drawing authority only. The
earlier session-state journal was deliberately narrowed; navigation and chrome
state are not durable product data.

Autosave persists only `OperationLog` authority. Navigation, selected tool,
brush size, palette state, and chrome state start from product defaults after a
restart. The next Stroke identity is derived from the restored active authority,
not stored as separate session state. Overview pixels,
tiles, settled output, chrome sprites, previews, and export buffers are derived
and are rebuilt.

## Authority model

`OperationLog` remains the only live authority for painter-ordered operations. The
journal records immutable snapshots or deltas from a generation-checked
`AuthorityReadView`:

- `kCheckpoint` carries every retained operation and sample, including the
  inactive Redo tail, plus the active prefix, generation, and epoch.
- `kAppendStroke` carries operations from `first_operation` through the new
  active prefix. The branch point replaces any persisted Redo tail.
- `kUpdate` carries no operation payload. It publishes a changed active prefix,
  generation, or epoch for history changes.

The first transaction is a checkpoint. A blank checkpoint represents New or a
fresh document. No transaction stores cache pixels or UI-only state.

## Portable journal seam

[`authority_journal.h`](../../vector_v2/include/tinydraw/vector_v2/authority_journal.h)
owns the wire format and recovery rules:

```cpp
std::optional<AuthorityJournalPlan> prepare_authority_journal(
    JournalChange change, const OperationLog& log);

bool encode_authority_journal(const AuthorityJournalPlan& plan,
                              const OperationLog& log,
                              std::uint64_t sequence,
                              std::span<std::byte> output);

bool AuthorityJournalStager::start(const AuthorityJournalPlan& plan,
                                   const OperationLog& log,
                                   std::uint64_t sequence,
                                   std::span<std::byte> output);

AuthorityJournalStageResult AuthorityJournalStager::resume(
    const OperationLog& log, std::size_t maximum_payload_bytes);

JournalRecovery recover_authority_journal(
    const AuthorityJournalSource& source, std::size_t bytes,
    std::span<OperationRecord> records,
    std::span<CompactOperationSample> samples);
```

Every staging slice runs under the log's serialized access and revalidates
the complete `AuthorityReadView`: epoch, generation, active and retained
operation counts, and retained samples. A stale view abandons the unpublished
buffer. Recovery writes into caller-owned bounded storage and returns an
`AuthorityReadView`; the caller restores `OperationLog` only after the complete
journal scan succeeds.

## ESP flash adapter

`VectorV2AutosaveStore` owns the `drawing` partition, queue, low-priority writer,
erase/write/readback work, startup scan, and flush:

```cpp
VectorV2AutosaveRestoreStatus restore(OperationLog& log);
bool submit(JournalChange change, const OperationLog& log);
bool submit_checkpoint(const OperationLog& log);
bool checkpoint_required() const;
bool checkpoint_staging() const;
bool flush(std::uint32_t timeout_ms);
```

Stroke and history transactions stage coherent bytes synchronously. Checkpoints
stage at most 16 KiB of payload per idle call, including operation metadata and
sample bytes; one operation can therefore never hide an unbounded copy. The
worker seals the transaction CRC after the buffer handoff, then controls
erase, write, and readback. It never reads the live log or canvas. Allocation,
staging, queue, seal, or write failure requests a later checkpoint and never
publishes a partial delta.

## Wire and interruption rules

- Integers use explicit little-endian encoding; C++ object layout is never
  persisted.
- Headers contain format/kind/length fields, sequence, authority identity and
  counts, and a header CRC.
- Checkpoint and append payloads contain explicit operation metadata followed
  by exact compact samples.
- The final 16-byte marker repeats the sequence and transaction CRC and is
  written last.
- Recovery validates lengths, ordering, authority transitions, operation
  boundaries, payloads, CRCs, and the marker before accepting a transaction.
- An invalid or incomplete tail after a valid transaction is discarded; the
  preceding committed transaction remains the recovery point.

## Flash layout and scheduling

The 3 MiB partition is one sector-aligned append-only journal. Each transaction
occupies whole 4 KiB sectors, so an interrupted tail begins at a sector boundary
and can be erased without touching the preceding commit. Fresh initialization
erases the partition on the worker before publishing the first checkpoint.

A completed physical stroke queues one append after authority publication.
Undo and Redo queue an update; New queues a blank checkpoint. An interrupted
tail or failed delta sets `checkpoint_required()`. Export and other hardware
storage transitions finish an in-progress checkpoint, call `flush()`, and
remain in drawing mode if either step fails. Normal drawing advances only one
checkpoint slice after an idle poll with no touch urgency. Pan, zoom, tool,
color, and other UI-only changes do not write flash.

The worker stays below touch sampling priority and performs bounded erase,
write, marker, and readback units. Final performance evidence must exercise this
store from normal product-loop Stroke/history events; gate-harness restoration
alone does not measure write contention.

## Startup recovery

1. Scan aligned transactions through the newest complete commit.
2. Restore retained operations, samples, active prefix, generation, and epoch.
3. Replay the active prefix into a fresh 25% overview.
4. Start at the default 25% view; higher zooms rebuild through the normal
   producer when first visited.
5. On empty or pre-V2 data, start blank and schedule the first checkpoint.
6. After a discarded tail, erase only that aligned tail on the worker and
   schedule a checkpoint.

## Limits and verification

The journal reports full without erasing committed recovery points. A minimum
transaction consumes 4 KiB; long strokes may consume several sectors. Two-arena
compaction and metadata are deferred until partition recycling is required.

Host tests cover checkpoint, append-after-Undo branch replacement, history
updates, blank reset, bounded restore capacity, truncation at every later-tail
byte, single-byte corruption, sector padding, header-only slice resumption,
maximum-sized single-operation slicing, stale-view abandonment, full-capacity
recovery, and a corrupt full-capacity tail. Release closure additionally
requires product/gate builds, normal-product performance with real journal
writes, a physical authority-only recovery check, and export-flush verification.
Destructive physical power-cut testing remains excluded by author direction.
