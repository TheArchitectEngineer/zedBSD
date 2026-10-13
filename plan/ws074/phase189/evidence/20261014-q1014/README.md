# q1014-i01 / at most one testharness case per Interop 2025 focus area

2026-10-14 00:04–00:07 JST. [Approved exact scope](../../next-queue-proposal.md), max90min. This is a bounded runner baseline; it is not a 3,805-test pass rate.

`run-interop2025-batch.py` reads the fixed [manifest](../../../phase173/evidence/20261013-q1012/manifest.json), sorts test IDs, and picks at most one physical plain HTTP HTML testharness page per focus area. It excludes generated worker/variant, HTTPS, substitution and testdriver cases and content with obvious driver/origin/worker requirements; exclusion reasons are in [report.json](report.json). IDs remain in the fixed manifest. The 15 selected IDs are each in their official focus area, with no duplicates. Four areas lacked a candidate under this bounded setup: Storage Access API, URLPattern, WebAssembly, and WebRTC.

Reproduction:

```sh
python3 plan/ws074/tests/run-interop2025-batch.py \
  --wpt /tmp/zedbsd-interop2025-wpt \
  --manifest plan/ws074/phase173/evidence/20261013-q1012/manifest.json \
  --out build/ws074-interop2025-q1014
```

The batch reused q1013's WPT callback runner and current host browser, with 5-second virtual settle and 20-second process timeout per ID. [Raw reports](report.json) `test-01.json`–`test-15.json` retain subtests, completion and stdout. Outcome by unique execution: **0 PASS / 12 test FAIL / 1 harness ERROR / 1 no completion / 1 no output**. The four absent areas are **unrun**, never PASS. `test-01` reports `align-self: anchor-center` rejected; `test-02` reports `main is not defined` and harness ERROR; `test-12` logs `Uncaught ReferenceError: a1 is not defined` before completion. These are observations under the current simple HTTP runner and require root-cause investigation before assigning each failure to browser versus infrastructure.

`PYTHONPYCACHEPREFIX=/tmp/zedbsd-interop2025-pycache python3 plan/ws074/tests/test-interop2025-batch.py`: 2/2 selection tests PASS (stable first ID, overlap once, unsupported context excluded). `py_compile` for the batch runner/tests and `git diff --check` PASS. No browser source, WPT source, public ABI, or `master.md` edit. Acid3 was not rerun because browser source did not change.

Remaining: p189 requires 1,783 testharness results across proper WPT environments. This batch gives results for 15 IDs, of which none is PASS; the other 1,768 remain unrun by this batch. q1013's earlier ID is additional and distinct, so the union actually attempted by this lane is 16 IDs. p190 reftest/crashtest and p173 full baseline remain open. Next Queue requires separate approval.
