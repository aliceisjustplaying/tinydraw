# Wave 1A panel optical evidence

The Git-tracked optical evidence is:

- `probe-window-proxy.mp4` — compressed review proxy.
- `contact-sheet-1fps.jpg` and `frames/` — extracted still evidence.
- `boundary-rising-50-2026-08-15.log` — matching device log.
- `boundary-rising-50-2026-08-15.sha256` — checksum of the original camera file.

The original `boundary-rising-50-2026-08-15.mov` is 196,509,392 bytes, above
GitHub's 100 MiB per-object limit, so it is intentionally excluded from Git.
The local working copy is retained under
`out/unpushed-large-artifacts/boundary-rising-50-2026-08-15.mov`; verify it with:

```sh
shasum -a 256 out/unpushed-large-artifacts/boundary-rising-50-2026-08-15.mov
```

Expected SHA-256:
`a8c08c4e241517aa4dedc0467f430bdd792d2b214ad56b70a12f929624db1ad4`.
