# q1055-i01: XHTML 1.0 Strict `nbsp`

Fixed WPT checkout: `2d66b9b7998bb58c336138c178323ddee857b586`. The 47 IDs are exactly the `error` cases in q1054's fixed 359-ID report; see `selection-manifest.json`. Local static reftest comparison is evidence of current rendering, not official Interop scoring.

- `sh plan/ws074/tests/host-build.sh plain`: exit 0, warning 0.
- `build/ws074-host/plain/host-xml`: 249/249. Includes actual attribute and text U+00A0, plus rejection without the exact public ID/root and rejection of an unknown named entity.
- `build/ws074-host/plain/host-xml-dom`, `host-xml-document`, `host-xml-binding`: 54/54, 16/16, 34/34.
- `python3 plan/ws074/tests/run-wpt-reftests.py --root . --selection-manifest plan/ws074/phase204/evidence/20261014-q1055/selection-manifest.json --render-timeout 5 --limit 0 --out plan/ws074/phase204/evidence/20261014-q1055`: 0 local PASS, 47 pixel FAIL, **0 load error**, 0 timeout; `report.json` and `progress.jsonl` contain case evidence.
- Fixed Flex 10-ID regression: 10/10 local pixel PASS, `../20261014-q1055-flex/report.json`.
- `python3 plan/ws074/tests/run-acid-tests.py --out build/ws074-acid-q1055`: Acid3 100/100 inventory, pixel difference 0/480000; Acid2 difference 1865/120000, unchanged. Disposable artifacts under `build/`.

All 47 former load errors now render and fail on writing-mode layout. q1054's earlier 312 load successes are unaffected by the public-ID-gated addition; together these establish p203's fixed 359-ID load-error criterion. p190's pixel and coverage work and p173's 3,805-ID 100% goal remain open. GitHub/shared Queue/Master projection belongs to Q1 and is pending; no push.
