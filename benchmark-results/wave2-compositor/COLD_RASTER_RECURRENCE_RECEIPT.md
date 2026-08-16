# Cold tapered-raster recurrence experiment — rejected

Date: 2026-08-16

Corpus: frozen `adversarial_tapered_4x`, 400%, origin `(0,0)`

The candidate rebased projection at every mask byte, advanced the tapered
coverage quadratic across x, and used the existing predicate near clamp and
coverage boundaries. It added no persistent storage. Exactness passed 91,712
host assertions, 4,000 collinear regression cases, 600 mixed-document fuzz
cases, and the frozen-corpus pixel comparison.

| Measurement | Baseline | Candidate | Change |
|---|---:|---:|---:|
| Host debug median, five runs | 52.745 ms | 53.071 ms | +0.6% |
| Device exact compute | 961.073 ms | 989.229 ms | +2.9% |
| Device wall | 1,056.871 ms | 1,091.770 ms | +3.3% |

The original comparison to the 577.667 ms straight-authority receipt was
invalid: curved authority had already changed the product replay cost. Against
the correct current curved-authority baseline, the recurrence is still a
device regression and was removed completely. The later combined tapered +
evil hairline corpus was not measured with this candidate.
