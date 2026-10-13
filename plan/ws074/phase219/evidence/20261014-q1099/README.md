# q1099-i01 — fixed Grid style-changes eight-ID recensus

The fixed Interop 2025 `/css/css-grid/alignment/grid-alignment-style-changes-001.html` through `008.html` ran with the same WPT commit, localhost, three fixed fonts, 5 s settle and 20 s process bound as q1097. [Analysis](analysis.json) compares all 48 callbacks with the eight q1097 raw reports; this directory holds [eight current raw reports](style-001.json) through `style-008.json`.

| Fixed eight IDs | Whole PASS | Whole FAIL | PASS subtests | FAIL subtests |
| --- | ---: | ---: | ---: | ---: |
| q1097 after q1095–q1096 | 2 | 6 | 20 | 28 |
| q1099 after q1098 | **4** | **4** | **26** | **22** |

IDs 005/006 each changed 3/6→6/6 subtests and whole FAIL→PASS. Six subtests changed FAIL→PASS, zero PASS→FAIL. IDs 001/002 remain whole PASS. IDs 003/004 remain 1/6 with vertical-rl horizontal offsets; IDs 007/008 remain 0/6 with vertical-lr text width first failing at expected20/40/30 versus actual100. Thus vertical writing work remains distinct from the completed horizontal first-line baseline subset.

q1099-i01 read-only measure cleared. p219/p217/p189/p173 whole remain uncleared; q1093 103 PASS/399 FAIL remains the latest **uniform** Grid502 measure. Browser, runner, WPT, public ABI, Master and Acid output were unchanged. Shared projection/GitHub publication pending with Q1; no push.
