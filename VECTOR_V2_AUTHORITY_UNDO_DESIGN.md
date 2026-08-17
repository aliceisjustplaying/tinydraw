# Vector V2 drawing history design

Status: implementation in progress, 2026-08-17.

Progress:

- [x] Slice 1: coherent active/retained authority read views with generation-checked reads.
- [x] Slice 2: whole-Stroke Undo/Redo and exact damage.
- [x] Slice 3: branch replacement after Undo.
- [ ] Slice 4: localized canvas transition.
- [ ] Slice 5: product wiring.
- [ ] Slice 6: device gate.

This design covers the in-memory authority and whole-Stroke Undo/Redo. It does
not define the autosave journal; autosave will consume the read view defined
here after this seam is proven.

## Plain-English model

The drawing is one ordered list of small vector-operation chunks. A physical
finger-down through finger-up may produce several chunks, but all adjacent
chunks with the same nonzero `gesture_id` are one **Stroke**. Undo and Redo
move only across complete Strokes.

The list can contain a redo tail. `active_operation_count` says how much of the
list is currently visible. Undo moves that boundary left; Redo moves it right.
Drawing after Undo replaces the inactive redo tail.

Raster overview and tile pixels remain rebuildable copies. They are never the
history authority.

## One owner, not two

`OperationLog` will be deepened into the complete in-memory drawing authority.
A second `StrokeAuthority` wrapper would create two objects that could disagree
about the active prefix, generation, or redo tail. Existing append, replay,
SVG, producer, and canvas code already depend on `OperationLog`, so keeping one
owner also gives migration a narrow compatibility path.

`OperationLog` will own these separate values:

1. **Retained prefix:** chunks and samples physically retained, including Redo.
2. **Active prefix:** chunks and samples in the current visible drawing.
3. **Document generation:** the existing monotonic `DocumentRevision`; each
   published chunk, Undo, Redo, and New/Clear advances it once. A chunk that
   replaces a Redo branch is still one publication and advances it once.
4. **Epoch:** identifies one valid operation-index/revision mapping. Undo,
   Redo, branch replacement, and reset advance it so queued replay or producer
   work from an older mapping is rejected.

Normal forward chunk appends preserve the epoch. After any history move,
`base_revision` is rebased so the active operation sequence still maps
linearly to the new monotonic generation. That preserves the existing bounded
`replay_range()` contract for later forward appends while preventing stale
work from crossing a history transition.

## Public seam

Appending keeps the existing prepared-then-publish transaction. The added
surface is intentionally small:

```cpp
struct AuthorityReadView {
  OperationLogEpoch epoch;
  DocumentRevision generation;
  std::size_t active_operation_count;
  std::size_t retained_operation_count;
};

struct HistoryChange {
  DocumentRevision generation;
  std::size_t previous_active_operation_count;
  std::size_t active_operation_count;
  PixelRect affected_world_bounds;
};

class OperationLog {
 public:
  bool can_undo() const;
  bool can_redo() const;
  std::optional<PreparedHistoryChange> prepare_undo();
  std::optional<PreparedHistoryChange> prepare_redo();
  AuthorityReadView read_view() const;
  bool unchanged(const AuthorityReadView&) const;
  std::optional<StoredOperation> operation(const AuthorityReadView&,
                                           std::size_t active_index) const;
  std::optional<StoredOperation> retained_operation(
      const AuthorityReadView&, std::size_t retained_index) const;
};
```

`PreparedHistoryChange` is move-only, like `PreparedAppend`. It exposes the
target prefix, next generation, and union of every chunk bound in the affected
Stroke. Destruction or `cancel()` changes nothing. `publish()` is infallible
after the caller has prepared the replacement raster pixels.

The app remains single-threaded and serializes mutation. A read view does not
add a lock; each read checks the captured epoch, generation, and prefixes. SVG,
PNG, and autosave must recheck `unchanged()` before promoting output.

## Required behavior

- A nonzero `gesture_id` groups only adjacent chunks. This remains correct when
  the 16-bit ID eventually wraps. A legacy/test chunk with ID zero is one
  history item by itself.
- Undo selects the final active Stroke. Redo selects the first inactive Stroke.
- The damage rectangle is the union of all chunks in that Stroke.
- A history request fails without mutation while an append/history change is
  prepared, at a generation limit, or when no matching history exists.
- Preparing a new append after Undo validates against the active prefix without
  overwriting retained Redo samples. The prepared object temporarily refers to
  the caller-owned input samples; publication copies them into the active tail,
  truncates Redo, and stores the new record. Canceling preparation leaves Redo
  intact. Callers must therefore keep append input alive and unchanged until publish/cancel,
  which every current prepared-append caller already does.
- Reads used for normal rendering expose only the active prefix. Persistence
  can separately serialize the retained prefix plus the active boundary so
  Redo survives restart.
- New/Clear clears active and retained history as part of its existing
  serialized log/canvas reset.

## Canvas transition

Undo is subtractive, so the forward-only pending overlay cannot represent it.
The app must first drain ordinary pending appends until log and canvas are in
lockstep.

For Undo or Redo:

1. Prepare the history change without mutating authority.
2. Rebuild only the affected overview rectangle from paper by replaying active
   operations in painter order, clipped to that rectangle.
3. Validate and commit one next-generation canvas revision. Intersecting tile
   identities are invalidated; unaffected tiles carry forward.
4. Publish the prepared history change infallibly.
5. Refresh the visible damage, minimap when needed, and Undo/Redo dock state.

No composition occurs between steps 3 and 4. The same order already used by a
prepared append—derived pixels first, infallible authority publication
second—keeps log and canvas from becoming partially updated.

## Test-first landing slices

1. **Authority state:** active/retained prefixes, monotonic generation, epoch,
   and generation-checked reads; no product behavior change.
2. **Whole-Stroke history:** single- and multi-chunk Undo/Redo, exact damage,
   cancel, limits, and at least ten levels.
3. **Branching:** new append after Undo removes Redo only on publication;
   canceled/capacity-rejected appends retain Redo.
4. **Canvas transition:** mixed pen/eraser replay into localized damage matches
   a full replay pixel-for-pixel; pending work must be drained first.
5. **Product wiring:** toolbar enablement, stale/disabled tap guards, New/Clear,
   minimap refresh, and exact localized presentation.
6. **Device gate:** multi-chunk strokes, repeated Undo/Redo, branch-after-Undo,
   zoom/pan between changes, SVG/PNG export, and latency/regression receipts.

Each slice lands as a separate green commit. Autosave begins only after slices
1–3 freeze the serialized authority shape.
