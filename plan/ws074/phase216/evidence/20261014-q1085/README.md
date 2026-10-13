# q1085-i01 — canonical URL module graph

The private graph in `js/module_graph.c` owns one rooted module instance per canonical URL. Its host resolver receives the importing module's URL and its exact UTF-16 specifier; the host supplies the canonical target URL. Its fetch callback supplies that URL's source. The graph links all named imports and evaluates dependencies before each importer, once per entry. Link and evaluation visit states stop cycle recursion. A failed import name returns an error before the importing module executes.

The graph's callback contract leaves HTTP fetch, MIME, origin policy and redirects to the page loader in the next Queue. The host must return a canonical URL for alias deduplication. Current cycle handling prevents repeat evaluation but has not established full ECMAScript cyclic instantiation semantics; tests cover a cycle without early binding reads.

| Check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, no warning/error (`build/ws074-q1085-build.log`) |
| `build/ws074-host/plain/host-module-graph` | PASS: relative URL, shared dependency fetched/evaluated once, `SLRM` order and value7, missing export rejected before side effect, `BA` cycle once |
| `python3 plan/ws074/tests/run-js-tests.py --program build/ws074-host/plain/browser` | classic JS 14/14 PASS |
| `python3 plan/ws074/tests/run-acid-tests.py --out build/ws074-acid-q1085` | Acid3 score100, inventory100/100, pixel diff0/480000; Acid2 diff1865/120000 unchanged |
| `git diff --check` | PASS |

q1085-i01 partial cleared. HTML module loading, HTTP policy, fixed Grid32 callback and all 3,805 PASS remain open. Shared projection/GitHub publication is pending with Q1; no push.
