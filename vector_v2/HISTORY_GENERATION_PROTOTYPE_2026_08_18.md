# F10 history-generation prototype receipt — rejected

## Decision

**NO-GO. Do not integrate the proposed unified two-generation directory.**

This was a host-only design and measurement prototype. It changed no product
history, `MaterializedCanvas`, presentation, autosave, settling, or tile
production. The disposable probe was removed after its deterministic evidence
was recorded here.

## Deterministic red baseline

`move_history_incrementally()` clears the affected 25% overview rectangle and
visits every operation in the target active prefix. Bounds rejection happens
after each operation is fetched.

The probe filled the 4,000-operation authority capacity with one-operation
gestures, then undid and redid the last four-pixel gesture:

| Corpus | Move | Operations scanned | Intersecting/rasterized |
|---|---|---:|---:|
| Sparse; 3,999 operations outside damage | Undo | 3,999 | 0 |
| Sparse | Redo | 4,000 | 1 |
| Dense; all operations intersect damage | Undo | 3,999 | 3,999 |
| Dense | Redo | 4,000 | 4,000 |

The sparse result confirms that current foreground work is proportional to the
active prefix even when only four overview pixels change. The dense result is
the genuine coverage floor that caching cannot remove.

## Why the prototype was rejected

### Content identity is wrong

`DocumentRevision` changes on Undo and Redo. `OperationLogEpoch` also increments
on every published history move; it deliberately invalidates prepared replay
ranges. The proposed `{epoch, active_operation_count}` key therefore cannot
recognize the old current generation as the immediately reusable Redo target.

A generation cache needs a stable history-lineage token. That token must change
when reset, restore, or a post-Undo append replaces the retained branch, but not
on ordinary Undo/Redo movement within one lineage. No such token exists yet.

### Memory arithmetic fits, state behavior is unproven

The measured product allocations are:

| Allocation | Bytes | Current role |
|---|---:|---|
| Uniform catalog | 54,768 | Live |
| Padded raw-slot directory | 32,768 | Live |
| Rerender-ledger reservation | 27,536 | Inert in product |
| **Aggregate** | **115,072** | |
| Blank-snapshot reservation | 329,728 | Inert in product |

The rejected unified-directory layout used 83,672 bytes, leaving 31,400 bytes
inside the 115,072-byte arithmetic envelope. This proves arithmetic fit only.
The uniform catalog and raw directory are live representations, so their bytes
are not reusable unless that representation is replaced successfully. The
probe did not bind fragmented `AppStorage` allocations or prove transitions,
failure atomicity, or live slot behavior.

The gate build also uses the snapshot and ledger for diagnostics. Any later
device prototype needs an explicit gate-storage plan and must retain export
workspace capacity.

### Canvas and slot invariants block a swap

`MaterializedCanvas` currently couples source validity and quality to its
current `DocumentRevision`. A generation exchange therefore needs exact rules
for revision publication, uniform invalidation, raw-slot retagging, and
fallback reads; swapping spans alone is insufficient.

Absorption edits resident raw tiles in place. If an adjacent generation refers
to the same slot, the current generation must copy on write before the first
partial edit. The rejected prototype did not specify no-free-slot behavior,
allocation failure, restart after partial work, or protection release.

Maintenance must reject log/canvas lag and staged commits, serialize with the
producer, and publish only whole-gesture generations. It cannot retain
`overview_scratch` or producer chord plans across cooperative yields. Adjacent
overview storage may use the product blank-snapshot reservation; absorption
scratch remains separate.

### F21 remains separate

The proposed free stack, CLOCK state, and unified tagged source entries are an
F21 materialization redesign. F10 does not require replacing `choose_slot()` or
the existing uniform catalog to prove a history-generation swap. F21
bookkeeping does not naturally disappear behind the history seam.

The earlier `ViewRequest` on the history-move call also added caller knowledge
without proven leverage. Focus belongs in cooperative preparation if later
measurement shows it is needed.

## Narrower next prototype

1. Add and test a stable lineage identity with exact reset, restore, Undo,
   Redo, and branch-replacement semantics.
2. Build a host-only state model around the current overview, uniform catalog,
   raw directory, and slot pool. Model current/adjacent readiness, protected
   slots, copy-on-write, whole gestures, reverse Redo, branch rejection, slot
   pressure, and partial absorption edits.
3. Keep the external seam small: cooperative `maintain(work_limit)` plus a
   direction-only history move. Exercise failure and cancellation through that
   same interface.
4. Only after the state model passes should a device prototype use the blank
   reservation for an adjacent overview and the 27,536-byte product ledger
   reservation for a second 27,384-byte raw directory, leaving 152 bytes for a
   56-byte slot-protection set and a small header. Adjacent uniform sources may
   fall back to its exact overview.
5. Preserve the current slot-selection policy. Measure the device swap,
   revision publication, uniform clearing, slot retagging, and gate-memory
   multiplexing before product integration.

That narrower shape has no product PSRAM growth by arithmetic. Its state
behavior, failure atomicity, and hardware result remain unproven.
