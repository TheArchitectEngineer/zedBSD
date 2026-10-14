# q1146-i01 — orthogonal children in fixed vertical Grid

The bounded `layout/grid.c` vertical-lr/LTR inline-grid path now accepts simple horizontal-tb children as well as vertical-lr children. The parent still maps fixed logical columns to physical y and rows to physical x; horizontal child line/intrinsic sizing remains horizontal. The other limits remain: definite px tracks, gap0, single-track in-flow nonreplaced children with auto sizes and start/stretch alignment.

The [2×2 orthogonal fixture](orthogonal-grid.html) changed from [before](before.txt) `text=250,250:0,0,100,12|100,0,150,150|0,150,100,12|100,150,150,100` to [after](after.txt) `text=250,250:0,0,38,100|0,100,150,12|150,0,38,12|150,100,100,150`. Its empty children likewise advance down columns and right across rows. A [span2 fallback fixture](orthogonal-grid-fallback.html) took the previous path without a crash; its [output](fallback.txt) is retained without claiming conformance.

Fixed Interop 2025 `grid-self-alignment-stretch-vertical-lr-005` through `-008` [all PASS](wpt005.json) ([006](wpt006.json), [007](wpt007.json), [008](wpt008.json)); they were whole FAIL in q1145. Old `-001` through `-004` [remain PASS](wpt001.json) ([002](wpt002.json), [003](wpt003.json), [004](wpt004.json)). Fixed old WPT [`grid-alignment-style-changes-003`](wpt-old-vertical.json) and [`grid-auto-placement-implicit-tracks-001`](wpt-old-horizontal.json) remain PASS.

Warning-free `sh plan/ws074/tests/host-build.sh plain`, `host-css-rule-model` 51/51, `run-js-tests.py` 14/14, and `run-grid-justify-tests.py` 48/48 PASS. [Acid summary](acid-summary.json): Acid3 score100/inventory100/100/pixel diff0 of 480000; Acid2 diff1865 of 120000. `git diff --check` PASS; `clang-format` unavailable.

q1146-i01 partial scope **cleared**. p225 whole and p173 all-focus 100% remain uncleared. No WPT source, runner, public ABI, or Master edit. Shared projection/GitHub publication pending with Q1; no push.
