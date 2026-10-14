# q1167-i01 — same-mode vertical Grid absolute self alignment

The bounded static area path now accepts `justify-self:self-start/end` for direct absolute children sharing their vertical writing mode with a fixed 2×2 positioned inline-grid. Its existing `static_y_align` placement makes self-start the physical column top and self-end the bottom. Fixed Interop 2025 [row-axis008](wpt-row-008.json) and [009](wpt-row-009.json) changed **2/2 whole FAIL→PASS**. Fixed [column-axis001–009](wpt-column-001.json) and [row-axis001–009](wpt-row-001.json) are **18/18 whole PASS**, 72/72 callbacks. Fixed WPT commit `2d66b9b7998bb58c336138c178323ddee857b586`, q1012 manifest, localhost HTTP, fixed fonts, 5 s settle, 20 s process bound.

[Warning-free build](build.log), host CSS rule model 51/51, JS 14/14 and Grid inline-axis 48/48 PASS. [Acid report](acid-report.json): Acid3 score100/inventory100/100 and 0/480,000 different pixels; Acid2 1,865/120,000 differences, unchanged. `git diff --check` PASS; `clang-format` unavailable.

q1167-i01 partial scope cleared. Opposite-writing-mode absolute child geometry remains separate; p217/p189/p173 all-focus100% unmet. WPT, runner, public ABI and Master unchanged; shared projection/GitHub publication pending with Q1, no push.
