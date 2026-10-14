# q1165-i01 — bounded vertical Grid absolute left and right

The fixed 2×2 vertical-lr/rl positioned inline-grid absolute placement now maps physical `left`/`right` in the block axis and `justify-self:right` to the column's physical bottom edge. `align-self:left/right` is invalid in CSS, so the existing parser rejects it; its `auto` → parent `normal` fallback maps to the Grid static start edge. This keeps invalid declarations from acquiring a made-up physical alignment while using the same bounded static area as q1163/q1164.

Fixed Interop 2025 [column-axis003](wpt-column-003.json), [004](wpt-column-004.json), [row-axis003](wpt-row-003.json), [004](wpt-row-004.json) changed **4/4 whole FAIL→PASS**, each 4/4 callbacks. [Baseline JSONs](baseline-column-003.json) record the previous failures. Earlier column/row-axis [001](wpt-column-001.json), [002](wpt-column-002.json), [005](wpt-column-005.json) remain 6/6 whole PASS; all ten pages and 40 callbacks pass. Fixed WPT commit `2d66b9b7998bb58c336138c178323ddee857b586`, q1012 manifest, localhost HTTP, fixed fonts, 5 s settle, 20 s process bound.

[Warning-free build](build.log), host CSS rule model 51/51, JS 14/14 and Grid inline-axis 48/48 PASS. [Acid report](acid-report.json): Acid3 score100/inventory100/100 and 0/480,000 different pixels; Acid2 1,865/120,000 differences, unchanged. `git diff --check` PASS; `clang-format` unavailable.

q1165-i01 partial scope cleared. p217 still needs flex/self alignment, auto lines, RTL row-axis017, baseline and other cases; p189/p173 all-focus100% remain unmet. WPT, runner, public ABI and Master unchanged; shared projection/GitHub publication pending with Q1, no push.
