Commands (from the worktree root; WPT 2d66b9b7998bb58c336138c178323ddee857b586 under build/ws074-suites/wpt):

- build: `sh plan/ws074/tests/host-build.sh plain` (warning 0)
- per test: `python3 plan/ws074/tests/run-interop2025-harness.py --manifest <q1012 manifest.json> --test-id ID --out OUT.json --font-dir userland/desktop/fonts`
- set: the 1,455 testharness IDs of q1068 plus the 502 Grid IDs of q1189 (union 1,455), run 32 at a time; lines of summary-*.txt are `ID outcome passed-callbacks callbacks`.
- host: host-css-rule-model, run-js-tests.py, run-grid-justify-tests.py, run-acid-tests.py
