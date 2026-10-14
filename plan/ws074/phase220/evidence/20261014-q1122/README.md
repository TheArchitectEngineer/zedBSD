# q1122-i01 — text-orientation CSS state

CSS now parses `text-orientation:mixed/upright/sideways`, retains the inherited value in computed style (initial `mixed`), handles explicit `inherit`/`initial`/`unset`, and rejects invalid values. `getComputedStyle()` returns the value through both `getPropertyValue()` and camel-case access. The related `writing-mode` computed value is now serialized. This is CSS state for later vertical inline layout; it does not change physical text geometry or paint.

The [minimal fixture](text-orientation.html) and `test-text-orientation.py` cover default, parent/child inheritance, explicit override and CSS-wide keywords, invalid stylesheet/DOM values, DOM mutation, and horizontal/vertical-lr/vertical-rl CSSOM output.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log) warning0/error0 |
| `test-text-orientation.py` | PASS |
| `host-css-rule-model`, `run-js-tests.py`, `run-grid-justify-tests.py` | 51/51, 14/14, 48/48 PASS |
| fixed `run-interop2025-harness.py` ×2 with pinned fonts | old whole PASS sentinels maintained |
| `run-acid-tests.py --out build/ws074-acid-q1122` | Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1122-i01 partial cleared. p220/p217/p189/p173 whole and all3,805 PASS remain uncleared. q1105 110 PASS/392 FAIL is the latest uniform Grid502 result. `clang-format` is unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
