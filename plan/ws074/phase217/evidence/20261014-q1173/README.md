# q1173-i01 — fixed Grid319 failure classification

[Classification](classification.json) covers all 319 q1172 whole FAIL IDs and their first failed callback. Top-level areas: alignment135, grid-definition41, parsing31, grid-items28, layout-algorithm27, abspos23, animation12, grid-model9, subgrid7 and 6 scattered files. First failure types: width96, height53, offsetLeft51, offsetTop42, other77. These are symptoms, not proven independent causes.

The three largest filename families each have 12 FAIL:

| Family | First failure pattern | Source/path finding |
| --- | --- | --- |
| `grid-self-alignment-non-static-positioned-items-001..012` | 001–006 mostly offset; 007–012 vertical RTL width | WPT 001 gives some absolute children explicit `left/right/top`, while `grid_static_position()` in `layout/grid.c` rejects an absolute child if **any** of four insets is non-auto. `position_place()` already lets an explicit physical inset override static placement on that axis, so it can use Grid static position for the other axis. |
| `grid-self-alignment-positioned-items-with-margin-border-padding-005..016` | 005 offsetTop; 006–010 vertical RTL width; 011–016 offset | WPT 005 has parent/child margin, border and padding; `grid_static_vertical_start()` currently rejects parent border/padding. It needs content-box and physical axis accounting, independently of the first family. |
| `grid-self-alignment-stretch-005..016` | all first fail on height | WPT 005 uses orthogonal in-flow vertical-lr children in a horizontal Grid. `grid_vertical_fixed()` only covers vertical Grid containers and `grid_lay_item/grid_relay_item` require an orthogonal stretch mapping. |

The first finite implementation candidate is horizontal positioned Grid with exactly one specified physical inset: retain that explicit axis and compute the other axis' Grid static area. Target non-static-positioned-items001–006, especially first y expected120/actual0. Existing all-auto positioned001–011, explicit inset behavior, host and Acid are regression controls. Vertical RTL007–012, margin/border/padding and orthogonal stretch remain separate. This is a source-based hypothesis to verify with a minimal page before code changes.

q1173-i01 read-only diagnosis cleared. No browser/runner/WPT/ABI/Master/Acid changes. p217/p189/p173 whole unmet; shared projection/GitHub publication pending, no push.
