# q1106-i01 — residual Grid alignment classification

The fixed [q1105 Grid502 raw census](../../../phase189/evidence/20261014-q1105/README.md) leaves 200 whole FAIL files under `/css/css-grid/alignment/`, down from 206 at q1093. [Classification](classification.json) includes each ID's first failing subtest and exact final assertion, and groups names without assuming the names identify one root cause.

| Largest filename series | Whole FAIL files |
| --- | ---: |
| column-axis positioned-items | 17 |
| row-axis positioned-items | 17 |
| positioned-items with margin/border/padding | 16 |
| self-alignment stretch vertical-lr | 16 |
| self-alignment stretch vertical-rl | 16 |
| non-static positioned-items | 12 |
| self-alignment stretch | 12 |
| block-axis auto margins | 7 |

First failing assertions are width79, offsetLeft65, offsetTop37, height18 and scrollWidth1. These counts describe the first failure per file only. The two remaining style-changes vertical-lr text IDs depend on [p220](../../../phase220/phase.md). Positioned/orthogonal groups need separate causal investigation.

The seven fixed `grid-block-axis-alignment-auto-margins-001`–`007` IDs are a tractable independent group: each has two failing subtests and first reports offsetTop expected75/90/95 versus actual0. Their WPT source uses 40%/60% rows in a 500px grid, `margin:auto 0` on two children, then changes grid height, item height or font size and checks placement again. `layout/grid.c` currently places `child->y` at row start plus computed top margin (auto resolves to zero), and does not divide positive row free space between top/bottom auto margins. [CSS Grid §11.2](https://drafts.csswg.org/css-grid/#auto-margins) specifies that auto margins are zero during track sizing and absorb positive free space before self-alignment. A bounded next implementation should handle horizontal Grid items in definite rows with one or both block-axis auto margins, resolving free space after final item height and redoing it on style changes. This is likely to improve these seven IDs, but whole PASS requires measurement.

q1106-i01 read-only classification cleared. p217/p189/p173 whole and all3,805 PASS remain uncleared. Browser, runner, WPT, ABI, Master and Acid were unchanged. Shared projection/GitHub publication pending with Q1; no push.
