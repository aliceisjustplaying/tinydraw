# SVG eraser transparency — 2026-08-19

Status: **fixed; host authority suite green.**

## Defect

SVG export encoded erasers as opaque white paths. The result only looked erased
on white paper, destroyed transparency, and could not preserve later background
changes in an SVG editor.

## Fix

Each logical eraser gesture is now emitted once in `<defs>` as a black geometry
path over a white, world-bounded SVG mask. Content groups are nested in reverse
eraser order and closed at the original painter-order position. This gives the
required sequence semantics: an eraser removes all preceding ink, never later
ink, and a later pen can draw back into the erased area. No document-sized
geometry storage was added; export remains a bounded streaming walk.

The regression covers pen/erase/pen/erase/pen ordering, true transparent holes,
the absence of white eraser paths, logical path counts, exact XML, and random
well-formed documents. The authority suite passes 85 cases / 25,702 assertions.
The hardware export gate will recheck file CRC, path count, and bounded sink
fragments in the release battery.
