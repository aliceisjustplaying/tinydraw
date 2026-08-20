# Recorded Puck frames

Run `bun puck/verify.mjs /path/to/puck --write-frames` only after semantic and
determinism checks pass and an intentional visual change needs new baselines.
Normal verification encodes the current frames and compares the PNG bytes
exactly, which is tolerance 0. Files are named `<trace>.t<ms>.png`.
