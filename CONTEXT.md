# TinyDraw Drawing Model

TinyDraw treats physical drawing intent as the user-facing authority while allowing bounded internal representations to split work for real-time rendering.

## Language

**Stroke**:
One uninterrupted drawing gesture from finger-down through finger-up. A Stroke is the logical unit for export, Undo, and Redo.
_Avoid_: Operation, chunk

**Stroke chunk**:
A bounded internal fragment of one Stroke. Multiple Stroke chunks may share one stroke identity, but a chunk is never a user-facing export or history unit.
_Avoid_: Stroke, gesture

**Journal commit**:
One completed saved change to drawing authority or drawing-session state. An incomplete save attempt is not a Journal commit.
_Avoid_: Snapshot, cache write

**Recovery point**:
The newest complete Journal commit TinyDraw can restore exactly after restart. An unfinished later save never replaces the Recovery point.
_Avoid_: Latest bytes, partial save
