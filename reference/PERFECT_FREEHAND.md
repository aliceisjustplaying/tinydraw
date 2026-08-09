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

Build the ignored reference checkout and regenerate the committed oracle fixtures with:

```sh
cd reference/perfect-freehand
corepack yarn install --immutable
corepack yarn build:packages
cd ../..
node tools/pf-reference.mjs
```

The generated JSON under `testdata/reference/` is independent expected output for the future C++
port. It records the source commit, options, input points, PF stroke points, and completed outline.

The dependency probe also reruns PF after every appended input. It shows an important constraint:
the first ten inputs mutate earlier radii because PF computes initial pressure from up to ten points.
After that warm-up, this fixture's left-outline prefix advances with a two-point provisional tail.
The report is evidence for the streaming design, not by itself a general proof of the commit horizon.
