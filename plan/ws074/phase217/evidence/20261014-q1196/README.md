# q1196-i01 — framed Grid auto-inset diagnosis

The [minimal four-parent page](framed-grid-auto-inset-first-child.html) reproduces the first-child patterns of fixed [non-static positioned 012](wpt-012.json): parent horizontal LTR/RTL or vertical-lr/rl, fixed 2×2 tracks, non-symmetric parent margin/border/padding, framed child, all four physical insets auto. Its own content uses monospace, so dimensions differ from WPT's Ahem; the placement differences are comparable.

| Parent | Independent Chromium | Browser | Difference |
| --- | --- | --- | --- |
| Horizontal LTR | 81,8,26,46 | 19,8,52,20 | vertical text fallback; static X |
| Horizontal RTL | 231,8,26,46 | 205,8,52,20 | vertical text fallback; static X |
| Vertical-lr | 19,80,52,20 | 19,8,52,20 | static Y |
| Vertical-rl | 205,80,52,20 | 19,8,52,20 | static X and Y |

The current fixed WPT012 callbacks still first fail with horizontal Grid width26→76 and vertical Grid `offsetTop 75→3` / `offsetLeft 173→11`, matching q1190. In `layout/inline.c`, `inline_vertical_absolute_grid` first requires explicit left/right for the horizontal Grid mapping, then rejects any parent border/padding; the all-auto framed case does not use the vertical text path. In `layout/grid.c`, `grid_static_position` handles horizontal parents but its orthogonal child X alignment is limited; `grid_static_vertical_start` rejects the framed parent with orthogonal horizontal child. These are separate layout changes: first isolate the horizontal parent's bounded all-auto vertical child measurement and placement, then the vertical parent's framed static area. Other children in WPT012 need later checks because each callback stops at its first failure.

q1196-i01 read-only diagnosis cleared. p217/p189/p173 whole remain unmet. Browser, runner, WPT, public ABI and Master unchanged; shared projection/GitHub publication pending with Q1, no push.
