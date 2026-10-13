# q1046: top-level script-free XHTML loading

Fixed WPT: `2d66b9b7998bb58c336138c178323ddee857b586`. Interop 2025 manifest q1012. `q1044` static census identified ten `css/css-flexbox/flexbox-justify-content-*.xhtml` tests that timed out at five seconds.

The top-level page now keeps HTTP Content-Type through synchronous and asynchronous navigation. `application/xhtml+xml` uses the existing strict XML parser and DOM projection, then adopts the complete XML children into the active Document so Window identity and hooks remain stable. Local `.xhtml`/`.xht` files use extension metadata. Script-free XHTML uses the existing load event and stylesheet steps; script execution in top-level XHTML is residual work.

`python3 plan/ws074/tests/run-wpt-reftests.py --root css/css-flexbox --selection-manifest /tmp/ws074-q1046-selection.json --render-timeout 5 --limit 0 --out plan/ws074/phase196/evidence/20261014-q1046`: ten fixed IDs, **1 local PASS / 9 FAIL / 0 timeout / 0 error** at 800×600 with fixed fonts. Before this change these same ten were 10 timeout. [Report](report.json) and [checkpoint](progress.jsonl) contain per-ID results. PNGs in `failures/` show the remaining visual differences, which belong to the flexbox rendering work, not the XHTML parser timeout.

`python3 plan/ws074/tests/run-top-level-xhtml-tests.py`: 4/4. Both HTTP `application/xhtml+xml; charset=utf-8` and local `.xhtml` produce sibling self-closed `div` elements; HTTP `text/html` retains HTML nesting; XHTML declaration is no HTML comment. This exercises the asynchronous HTTP path separately from the reftest runner's headless path.

`sh plan/ws074/tests/host-build.sh plain`: warning 0. `host-xml-dom` 54/54; `run-resource-tests.py` 105/105; `run-frame-load-tests.py` 52/52. `git diff --check` PASS. `clang-format` unavailable. `run-acid-tests.py --out build/ws074-acid-q1046`: Acid3 score100/inventory100/100/pixel diff0/480000; Acid2 diff1865/120000, unchanged from q1044. Host report: `build/ws074-acid-q1046/report.json`.

Limits: the local reftest renderer does not yet certify official WPT environment equivalence. Nine flexbox pixel comparisons still fail. Script-bearing XHTML, other XML MIME top-level documents and XML parse error pages are outside this Queue. Shared Queue/Master/GitHub publication are pending Q1; `plan/master.md` and WPT files were not changed.
