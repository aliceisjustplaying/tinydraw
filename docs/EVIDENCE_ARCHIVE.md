# Raw measurement evidence archive

The current tree keeps concise receipts, active regression fixtures, and compact
structured data. It omits 340 raw serial/build/flash logs, videos, and extracted
frames (67,163,799 bytes) from `benchmark-results/` and
`vector_v2/hardware-receipts/`.

Every removed artifact remains byte-for-byte available at tag
[`v2-feature-complete-pre-cleanup`](https://github.com/aliceisjustplaying/tinydraw/tree/v2-feature-complete-pre-cleanup).
Retrieve one without restoring it to the working tree:

```sh
git show v2-feature-complete-pre-cleanup:path/to/artifact > /tmp/artifact
```

Paths named by preserved historical receipts refer to that tag.
