# perfect-freehand reference

The upstream TypeScript implementation is a read-only behavioral reference, not a build dependency.
It is intentionally ignored rather than vendored or added as a submodule.

Pinned reference commit:

```text
176e00f2399f4969e1b0965c5921d96a3e50ce9f
```

Clone it locally with:

```sh
git clone https://github.com/steveruizok/perfect-freehand.git reference/perfect-freehand
git -C reference/perfect-freehand checkout 176e00f2399f4969e1b0965c5921d96a3e50ce9f
```

Relevant files begin in `packages/perfect-freehand/src/`. TinyDraw preserves behavior where useful,
but timestamps and streaming state are deliberate deviations from the whole-stroke JavaScript API.
