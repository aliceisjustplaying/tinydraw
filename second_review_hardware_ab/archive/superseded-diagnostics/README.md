# Superseded diagnostic hardware captures

These captures are retained as historical evidence from the disposable
camera-aligned-atlas prototype. They are not current production requirements or
load-bearing validation receipts.

The tracked final receipts remain:

- [`../../8817a88-step0-scratch-ab.log`](../../8817a88-step0-scratch-ab.log) for the scratch-memory A/B;
- [`../../12b70da-diag-auto-hardware.log`](../../12b70da-diag-auto-hardware.log) for the final automated run; and
- [`../../12b70da-manual-diag.log`](../../12b70da-manual-diag.log) for the final interactive run.

## Captures

| Capture | Historical role | Why it is superseded |
|---|---|---|
| `4fc345e-manual-hardware.log` | Short automated run from exact commit `4fc345e`. | Duplicates the tracked exact-commit automated evidence. |
| `blank-canvas-repro.log` | Attempted blank-canvas reproduction from `4fc345e`. | The later tracked diagnostic runs close the prototype evidence. |
| `blank-canvas-instrumented-benchmark.log` | Automated diagnostic run from dirty build `ffdbd9b`. | Replaced by exact-commit `12b70da` telemetry. |
| `blank-canvas-instrumented.log` | Default-firmware diagnostic capture from dirty build `ffdbd9b`. | It is not a production receipt and predates the final telemetry. |
| `manual-live-serial.log` | Early manual interaction capture from dirty build `ffdbd9b`. | Replaced by `12b70da-manual-diag.log`. |
| `manual-fixed-live-serial.log` | Long manual capture used to validate the early `6b1c406` patch. | Preserved for commit history; replaced as final evidence by `12b70da-manual-diag.log`. |

The files were archived without content changes. SHA-256 checksums:

```text
b423e7402935dbe642e9e6ca045e15eeee3fb2e81746754b5beb456a1b84a4ac  4fc345e-manual-hardware.log
769b91fa5519e1aa53e71449a2b84552b9889f4bf019a0233ab0f7274d58499d  blank-canvas-instrumented-benchmark.log
94f5c2ffefd2549b10e88c0fef915a99a59521e40ebf61ff274ac18fa99c89f8  blank-canvas-instrumented.log
35c08ad26753752541ccd673c6b4877fa132fd6f8aee51bb7d3141a6f99b65e3  blank-canvas-repro.log
85bbf2b636752bdff0f22451500c4f88c3d346351244ca38957183076bd20137  manual-fixed-live-serial.log
bf29a2d38556e64a9e63b64c46c1ff998affa775079044a1a4664d9dbcaf0d5b  manual-live-serial.log
```
