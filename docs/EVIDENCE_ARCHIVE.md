# Raw measurement evidence archive

The current tree keeps concise receipts, active regression fixtures, and compact
structured data. It omits 377 raw serial/build/flash logs, hardware transcripts,
generated benchmark binaries, videos, and extracted frames (68,217,490 bytes)
from `benchmark-results/`, `vector_v2/hardware-receipts/`, and
`second_review_hardware_ab/`.

Every removed artifact remains byte-for-byte available at tag
[`v2-feature-complete-pre-cleanup`](https://github.com/aliceisjustplaying/tinydraw/tree/v2-feature-complete-pre-cleanup).
Retrieve one without restoring it to the working tree:

```sh
git show v2-feature-complete-pre-cleanup:path/to/artifact > /tmp/artifact
```

Paths named by preserved historical receipts refer to that tag.
