# q1013-i01 / one selected WPT testharness page

2026-10-13 23:59–2026-10-14 00:03 JST. [Approved partial scope](../../phase.md), max90min. This is a runner capability proof, not a browser PASS.

The runner [run-interop2025-harness.py](../../../tests/run-interop2025-harness.py) verifies the fixed WPT commit and [selected manifest](../../../phase173/evidence/20261013-q1012/manifest.json), serves the pinned WPT files from one localhost HTTP origin, and appends callback instrumentation to the HTTP response for `resources/testharnessreport.js`. The WPT checkout and test source are not changed. `browser --run` stdout is parsed for result and completion JSON. Process failure, process timeout, missing output, missing completion, harness error, WPT timeout and failing subtests have distinct non-PASS outcomes.

Selected test: `/css/css-anchor-position/at-position-try-parse.html`. Reproduction:

```sh
python3 plan/ws074/tests/run-interop2025-harness.py \
  --wpt /tmp/zedbsd-interop2025-wpt \
  --manifest plan/ws074/phase173/evidence/20261013-q1012/manifest.json \
  --test-id /css/css-anchor-position/at-position-try-parse.html \
  --out build/ws074-interop2025-q1013/at-position-try-parse.json
```

The runner exits 1 because the actual test FAILs. [Raw report](at-position-try-parse.json): process exit0, WPT harness completion `OK` (status0), 12 result callbacks and 12 completed subtests, **4 PASS / 8 FAIL / 0 TIMEOUT**. One failure says the valid `@position-try --foo { }` rule was not supported; this is a browser CSS result, not a runner failure. The browser binary SHA-256 and stdout are in the report. No test result was forced to PASS.

`PYTHONPYCACHEPREFIX=/tmp/zedbsd-interop2025-pycache python3 plan/ws074/tests/test-interop2025-harness.py`: 5/5 tests PASS for PASS, FAIL, WPT/process timeout, no output/no completion, harness error and nonzero process exit. `py_compile` for runner and tests PASS; `git diff --check` PASS. One actual WPT page was run, matching the approved bound. No browser source, public ABI, WPT test, or Master edits. Acid3 was not rerun because browser source did not change.

Remaining: p189 whole needs reliable results for all 1,783 testharness IDs, including HTTPS, workers, variants and multiple origins. p173 whole baseline and the user's all-test 100% target remain open. Next Queue needs a separate exact-scope approval.
