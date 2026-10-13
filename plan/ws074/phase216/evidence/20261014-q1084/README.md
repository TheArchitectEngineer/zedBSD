# q1084-i01 — module import metadata

`js_module_import_count` and `js_module_import_at` expose each static named import's specifier, imported name and local name as borrowed, read-only UTF-16 spans stable for the module instance's lifetime. `js_module_link_import` now checks the requested specifier as well as the imported name before connecting a local binding. The future graph loader is responsible for associating a resolved URL with the correct target instance; this API does not imply URL identity by itself. All APIs remain private to libbrowser.

| Check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, no warning/error (`build/ws074-q1084-build.log`) |
| `build/ws074-host/plain/host-module-import` | PASS: same specifier with two names, separate specifier, out-of-range import, wrong specifier/name rejection, existing live binding/GC/TDZ/assignment checks |
| `python3 plan/ws074/tests/run-js-tests.py --program build/ws074-host/plain/browser` | classic JS 14/14 PASS |
| `python3 plan/ws074/tests/run-acid-tests.py --out build/ws074-acid-q1084` | Acid3 score100, inventory100/100, pixel diff0/480000; Acid2 diff1865/120000 unchanged |
| `nm -D build/ws074-host/plain/libbrowser.so` | no `js_module_` or `vm_import_binding` exported symbol |
| `git diff --check` | PASS |

q1084-i01 partial cleared. URL fetch/cache, graph evaluation, HTML module loading, fixed Grid32 callback and all 3,805 PASS remain open. Shared projection/GitHub publication is pending with Q1; no push.
