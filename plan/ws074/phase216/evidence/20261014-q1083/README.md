# q1083-i01 — named import live binding

An internal module instance can now compile an explicit named static import as an immutable local binding. `js_module_link_import` connects that local name to a direct named export in another instance before evaluation; an unlinked importer returns `EAGAIN`. The importer's retained environment stores a GC traced indirect binding, and the VM's `GET_IMPORT` reads the export's current slot on every access. An uninitialized lexical export throws `ReferenceError`; assignment to an import throws `TypeError`. Duplicate local names, absent target exports and repeated links are rejected. This is a private `libbrowser` path; `nm -D build/ws074-host/plain/libbrowser.so` shows no `js_module_` or `vm_import_binding` public export.

The [q1081 design](../20261014-q1081/README.md) was refined: a late-linked target environment cannot be embedded as a constant when the importer is compiled. The indirect binding cell and `GET_IMPORT` opcode preserve live access without recompilation. The VM cell traces the target environment, while the importer environment retains the cell. This is general module binding behavior, not a WPT-specific rewrite.

| Check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit 0, no warnings (`build/ws074-q1083-build-final.log`) |
| `build/ws074-host/plain/host-module-import` | PASS: unlinked refusal, lexical TDZ, wrong/missing/duplicate link, assignment rejection, target release + forced GC, live counter1→2 |
| `build/ws074-host/plain/host-module-export .../support/positioned-grid-descendants.js` | PASS: q1082 export and fixed WPT support source retained |
| Prior module scope/export declaration fixtures | exact output maintained |
| `python3 plan/ws074/tests/run-js-tests.py --program build/ws074-host/plain/browser` | 14/14 classic JS suites PASS |
| `python3 plan/ws074/tests/run-acid-tests.py --out build/ws074-acid-q1083` | Acid3 score100/inventory100/100/pixel diff0/480000; Acid2 diff1865/120000, unchanged |
| `git diff --check` | PASS |

The host still must resolve each import's URL, fetch/cache the graph, instantiate dependencies and evaluate them in order. HTML `<script type="module">` is still skipped. The fixed Grid 32 no_output IDs were not remeasured or counted as PASS. q1083-i01 partial cleared, p216 whole and p189/p173 all-test 100% remain uncleared. Shared projection/GitHub publication pending with Q1; no push.
