# q1097-i01 — fixed Grid style-changes eight-ID measure

The eight fixed Interop2025 `/css/css-grid/alignment/grid-alignment-style-changes-001.html` through `008.html` ran with the q1093 WPT commit, localhost, three fixed fonts, 5 s settle and 20 s process bound. [Analysis](analysis.json) compares every callback against the same eight raw q1093 reports; [eight raw results](style-001.json) through `style-008.json` are retained.

| Fixed eight IDs | Whole PASS | Whole FAIL | PASS subtests | FAIL subtests |
| --- | ---: | ---: | ---: | ---: |
| q1093 before shorthand/baseline | 0 | 8 | 0 | 48 |
| q1097 after q1095–q1096 | **2** | **6** | **20** | **28** |

IDs 001 and 002 are now whole PASS with 6/6 subtests each. Across the eight IDs, 20 subtests changed FAIL→PASS, zero changed PASS→FAIL. IDs 003 and 004 use vertical-rl children and `justify-items:baseline`; their remaining failures primarily report horizontal offsets. IDs 005 and 006 have horizontal text lines with differing font sizes; width works after the shorthand, but remaining failures report vertical baseline offsets. IDs 007 and 008 use vertical-lr text and fail physical width/offset checks. These are separate prerequisites; the q1096 empty-box baseline cannot solve line-baseline or vertical writing.

q1097-i01 read-only measure cleared. p219/p217/p189/p173 whole remain uncleared; q1093 103 PASS/399 FAIL is the latest **uniform** Grid502 measure. Browser, runner, WPT, public ABI and Master were unchanged. No Acid or full Grid recensus was in scope. Shared projection/GitHub publication pending with Q1.
