# q1155-i01 — remaining fixed Grid failure classification

[Machine-readable classification](classification.json) covers all 360 whole FAIL from q1154, using each raw callback's first failed subtest, failed/total subtest counts, filename family, and the first failure's trailing assertion. Counts reconcile to the fixed 502 total (142 PASS + 360 FAIL).

| Source family | Whole FAIL |
| --- | ---: |
| alignment | 176 |
| grid-definition | 41 |
| parsing | 31 |
| grid-items | 28 |
| layout-algorithm | 27 |
| abspos | 23 |
| animation | 12 |
| grid-model | 9 |
| subgrid | 7 |
| placement | 2 |
| four single files | 4 |

The first failed assertion concerns width in 155 files, height in 54, offsetLeft in 46, offsetTop in 30, top in 1, and other callback text in 74. These labels describe symptoms, not proven causes. The largest filename groups are positioned column/row alignment (17 each), vertical-rl stretch (16), and positioned item frame (15).

All 16 fixed `alignment/grid-self-alignment-stretch-vertical-rl-001..016.html` remain FAIL; their first failed assertion is width, expecting 10/60/20/80 and observing 100/125/115 depending on case. WPT source 001 differs from passing vertical-lr 001 only in writing-mode and expected physical x offsets. The local source confirms both bounded Grid mappings in `layout/grid.c` accept vertical-lr only, while `layout/inline.c`'s bounded vertical text geometry also accepts vertical-lr only. Therefore enabling Grid axis mirroring alone cannot establish full 16-case success. Next bounded work should first test non-Grid vertical-rl text geometry and painting, then extend explicit fixed Grid tracks and implicit auto tracks in separate Queues. This is an inference from source and first mismatch, not a verified fix.

q1155-i01 read-only classification cleared. p189/p173 all-focus100% remain unmet. Browser, runner, WPT, ABI, Master and Acid unchanged; shared projection/GitHub publication pending with Q1, no push.
