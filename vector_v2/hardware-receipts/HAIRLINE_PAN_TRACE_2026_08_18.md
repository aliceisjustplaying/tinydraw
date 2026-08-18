# Hairline pan regression trace — 2026-08-18

This is the compact per-slice trace for the minimized 400% white-block
regression. The corpus is 10 thin pens, one medium pen, one thick pen, a 16-axis
eraser grid, and eight warmed shared rows: 67 operations / 759 samples. Host
wall time is diagnostic; the 29-slice budget is anchored to the device receipt.

Command:

```sh
out/build/host-census-debug/vector_v2/tinydraw_vector_v2_raster_census \
  --hairline-pan-trace
```

Columns: slice, group, wall ms, authority operations, deduplicated candidates,
scanned operations, exact hits, rendered operations, raster steps, published
groups/tiles, tiles remaining, constant rows/searches/span pixels, certified
full fills/pixels, complete.

## Before the constant-capsule certificate

```text
 1 [2816,3456,2944,3584] .490 67 41 22 9 8 40 0/0 40 924 2168 10328 0/0 0
 2 [2816,3456,2944,3584] .196  0  0 12 5 5 21 0/0 40 323 4105  3904 0/0 0
 3 [2816,3456,2944,3584] .212  0  0  7 1 2  6 1/4 36 142 2980  2270 0/0 0
 4 [2944,3456,3072,3584] .538 67 41 22 9 8 40 0/0 36 964 2240 10136 0/0 0
 5 [2944,3456,3072,3584] .376  0  0 19 6 7 29 1/4 32 419 2569 11239 0/0 0
 6 [2816,3584,2944,3712] .453 67 41 18 8 7 35 0/0 32 908 2072 10200 0/0 0
 7 [2816,3584,2944,3712] .232  0  0 16 4 4 16 0/0 32 362 2789 10312 0/0 0
 8 [2816,3584,2944,3712] .128  0  0  7 1 2  9 1/4 28   4   24   176 0/0 0
 9 [2944,3584,3072,3712] .460 67 41 18 8 7 35 0/0 28 947 2146  9998 0/0 0
10 [2944,3584,3072,3712] .165  0  0 15 3 3 15 0/0 28 262  658 11786 0/0 0
11 [2944,3584,3072,3712] .118  0  0  0 0 1  3 1/4 24   4    8   504 0/0 0
12 [2816,3328,2944,3456] .442 67 34 24 8 7 35 0/0 24 908 2056 10336 0/0 0
13 [2816,3328,2944,3456] .187  0  0 10 7 8 28 1/4 20 184  369  3240 0/0 0
14 [2944,3328,3072,3456] .447 67 34 24 8 7 35 0/0 20 950 2134 10132 0/0 0
15 [2944,3328,3072,3456] .163  0  0  6 5 5 25 0/0 20 299 3757  3387 0/0 0
16 [2944,3328,3072,3456] .248  0  0  4 2 3  7 1/4 16 157 3577  3319 0/0 0
17 [3072,3456,3200,3584] .309 67 41 33 7 6 30 0/0 16 332  995 13312 0/0 0
18 [3072,3456,3200,3584] .107  0  0  0 0 1  5 1/2 14  48  167  3072 0/0 0
19 [3072,3584,3200,3712] .291 67 41 33 5 4 20 0/0 14 252  750 11520 0/0 0
20 [3072,3584,3200,3712] .134  0  0  0 0 1  5 1/2 12  66  215  4864 0/0 0
21 [2816,3712,2944,3840] .439 67 27 21 7 6 30 0/0 12 887 2192  9566 0/0 0
22 [2816,3712,2944,3840] .170  0  0  1 1 1  1 0/0 12  94  188  9533 0/0 0
23 [2816,3712,2944,3840] .175  0  0  0 0 1 11 1/4  8  34   68  3672 0/0 0
24 [2944,3712,3072,3840] .423 67 27 21 7 6 30 0/0  8 854 1942 10180 0/0 0
25 [2944,3712,3072,3840] .086  0  0  0 0 0  0 0/0  8  79  230  9323 0/0 0
26 [2944,3712,3072,3840] .223  0  0  1 1 2 13 1/4  4 104  395  4856 0/0 0
27 [3072,3328,3200,3456] .296 67 34 30 7 6 30 0/0  4 321 3784  8783 0/0 0
28 [3072,3328,3200,3456] .196  0  0  4 2 2  6 1/2  2 159 2594  5655 0/0 0
29 [3072,3712,3200,3840] .210 67 27 21 1 0  0 0/0  2 129  836  9472 0/0 0
30 [3072,3712,3200,3840] .100  0  0  0 0 1  4 1/2  0  54  108  6912 0/0 1
```

At slice 29: `budget_complete=0 budget_missing_blocks=2
budget_fallback=5376`. Slice 30 finishes exactly with `mismatches=0`.

## After the constant-capsule certificate (`919714b`)

Slices 1–28 retain the same work shape. Host timings varied by less than the
noise floor; the only semantic delta is the final group:

```text
28 [3072,3328,3200,3456] .207  0  0  4 2 2 6 1/2 2 159 2594 5655 0/0     0
29 [3072,3712,3200,3840] .188 67 27 21 1 1 4 1/2 0   0    0    0 1/16384 1
```

At slice 29: `budget_complete=1 budget_missing_blocks=0 budget_fallback=0
steps=29 publications=12 fallback=0 mismatches=0 white_blocks=0 regression=0`.
The guarded certificate replaces the final group’s 183 constant rows, 944
searches, and 16,384 span pixels with one exact full-surface fill.
