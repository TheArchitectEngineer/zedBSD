# q1086-i01 — parser-inserted inline HTML modules

Parser-inserted inline `<script type="module">` sources are queued in document order. At the end of HTML parsing, the page-owned graph instantiates, links and evaluates each before DOMContentLoaded; classic scripts retain parser order. A module dependency resolves relative to its importing module URL, then requires the document's HTTP origin, a successful nonredirected response and JavaScript MIME. Syntax/runtime failures go to the usual script console path and do not suppress later modules. The page releases graph GC roots before its realm and heap. External module root, async and dynamic scripts remain outside this partial scope.

The host fixture observes classic `C`, later classic `c`, modules `M` and `N`, then DOMContentLoaded `D` (`CcMND`). An invalid module between the two good ones emits one SyntaxError and does not prevent `N`; explicit GC and page destruction complete.

The fixed WPT checkout is `2d66b9b7998bb58c336138c178323ddee857b586`. [Grid32 summary](grid32-summary.json) and [compressed raw runner reports](grid32-raw.json.gz) preserve each ID, callback and command. The selected 32 IDs exactly match q1077's 32 `no_output` IDs. Every ID now reports PASS with 100 passing subtests; 3,200 subtests total. The initial one-case probe omitted the three fixed fonts and ended with a page-load error; the corrected run used the same three fonts as q1077. All 32 final runs used localhost HTTP, 5 s settle, 20 s process limit, the fixed manifest and `userland/desktop/fonts`.

| Check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, no warnings/errors (`build/ws074-q1086-build-final.log`) |
| `build/ws074-host/plain/host-inline-module` | PASS: defer/order, two modules, SyntaxError recovery, page release |
| `build/ws074-host/plain/host-module-graph` | PASS: q1085 graph behavior maintained |
| `python3 plan/ws074/tests/run-js-tests.py --program build/ws074-host/plain/browser` | classic JS 14/14 PASS |
| 32 × `run-interop2025-harness.py` with fixed WPT/manifest/3 fonts | 32 PASS, each 100 subtests, zero nonPASS; q1077 no_output ID set equal |
| `python3 plan/ws074/tests/run-acid-tests.py --out build/ws074-acid-q1086` | Acid3 score100, inventory100/100, pixel diff0/480000; Acid2 diff1865/120000 unchanged |
| `nm -D build/ws074-host/plain/libbrowser.so` | no new `js_module_` or `page_module` public export |
| `git diff --check` | PASS |

q1086-i01 partial cleared. The other 470 fixed Grid testharness IDs were not rerun in this Queue; no new whole-Grid or all-focus total is claimed. p216 whole, p189/p190/p173 all-test 100% remain uncleared. Shared projection/GitHub publication is pending with Q1; no push.
