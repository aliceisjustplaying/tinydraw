# Recorded Puck frames

Run `bun puck/verify.mjs /path/to/puck --write-frames` after building the WebAssembly module.
The files are named `<trace>.t<ms>.png`, which is the Puck 0.2 bundle convention consumed by
`verify-bundle`.
