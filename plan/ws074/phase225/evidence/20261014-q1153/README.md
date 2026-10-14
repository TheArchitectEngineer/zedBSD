# q1153-i01 — physical border and padding in bounded vertical Grid auto tracks

The 2×2 implicit auto-track vertical-lr/LTR path now accepts nonnegative pixel border and padding on content-box children. The physical frame contributes to each track minimum, and vertical text gets the column content height after margins, borders, and padding. Final placement still uses the margin edge. Other Grid forms use the existing path.

The independent [border](border-auto-grid.html) and [padding](padding-auto-grid.html) pages each compare four item rectangles with Chromium 153. [Summary](summary.json) records exact 4/4 coordinate-and-size matches for both pages; browser and Chromium outputs are retained alongside them. The fixed Interop 2025 [stretch011](final-wpt-011.json), [012](final-wpt-012.json), [015](final-wpt-015.json), and [016](final-wpt-016.json) changed whole FAIL→PASS. Previously passing stretch001–010 and 013–014 and two older vertical/horizontal Grid cases remained PASS: 18/18. Inputs: fixed WPT `2d66b9b7998bb58c336138c178323ddee857b586`, official fixed manifest, localhost HTTP, fixed fonts, 5 s settle, 20 s process bound.

[Warning-free build](build.log), host CSS rule model 51/51, JS 14/14 and Grid inline-axis 48/48 PASS. [Acid callback report](acid-report.json): Acid3 score100/inventory100/100 and 0/480,000 different pixels; Acid2 1,865/120,000 different pixels, unchanged. `git diff --check` PASS; `clang-format` unavailable.

q1153-i01 partial scope cleared. p225 whole still needs other Grid forms and full conformance. p189/p173 all-focus coverage and 100% remain unmet. WPT, runner, public ABI and Master unchanged; shared projection/GitHub publication pending with Q1, no push.
