# q1102-i01 — fixed Grid style-changes eight-ID recensus

The fixed Interop 2025 style-changes 001–008 ran with the same WPT commit, localhost, three fixed fonts, 5 s settle and 20 s process bound as q1099. [Analysis](analysis.json) compares all 48 callbacks against q1099; [raw reports](style-001.json) through `style-008.json` are retained.

| Fixed eight IDs | Whole PASS | Whole FAIL | PASS subtests | FAIL subtests |
| --- | ---: | ---: | ---: | ---: |
| q1099 after horizontal first-line baseline | 4 | 4 | 26 | 22 |
| q1102 after vertical-rl empty baseline | **6** | **2** | **36** | **12** |

IDs 003/004 each changed 1/6→6/6 subtests and whole FAIL→PASS. Ten subtests changed FAIL→PASS; zero PASS→FAIL. IDs 001/002/005/006 remain whole PASS. IDs 007/008 remain 0/6; vertical-lr text items first fail width (expected20/40/30, actual100). q1093 **103 PASS/399 FAIL** remains the latest uniform Grid502 measure.

q1102-i01 read-only measure cleared. p189/p217/p219/p173 whole remain uncleared. Browser, runner, WPT, public ABI, Master and Acid output were unchanged. Shared projection/GitHub publication pending with Q1; no push.
