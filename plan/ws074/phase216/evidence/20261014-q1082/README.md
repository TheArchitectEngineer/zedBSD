# q1082-i01 — retained module export environment

The internal `js_module_create` path now parses a module, marks direct named export declarations as retained bindings, compiles them to stable `vm_env` slots, and owns the environment and program closure with GC roots until `js_module_release`. `js_module_evaluate` executes once; `js_module_export` reads the current slot value, returning `EAGAIN` before evaluation and `ENOENT` for an absent name. Duplicate direct exports fail creation. The existing `js_compile`/`js_run_script` route remains available for classic scripts and the script tool. These are private `libbrowser/js` APIs; `exports.map` still exposes only the public `<browser/browser.h>` symbols.

The module program's prologue reuses its retained closure environment instead of creating an inaccessible child. Exported functions close over the same environment. The host test creates `export let counter=1` and `export function increment(){counter+=1;return counter}`, reads `counter=1`, runs GC with stack scanning disabled while the module's roots retain it, calls `increment`, then reads the live `counter=2`. It checks one-time evaluation, an absent name and a duplicate export. With the pinned WPT support file as an argument it compiles/evaluates the real `export function runTests` source and reads a callable export.

| Check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit 0, no warnings (`build/ws074-q1082-build-final.log`) |
| `build/ws074-host/plain/host-module-export /tmp/zedbsd-interop2025-wpt/css/css-grid/abspos/support/positioned-grid-descendants.js` | PASS, including live mutation, forced GC, duplicate/missing name and pinned WPT support source |
| Prior module scope/export declaration fixtures | exact output maintained |
| `python3 plan/ws074/tests/run-js-tests.py --program build/ws074-host/plain/browser` | 14/14 classic JS suites PASS |
| `python3 plan/ws074/tests/run-acid-tests.py --out build/ws074-acid-q1082` | Acid3 score100/inventory100/100/pixel diff0/480000; Acid2 diff1865/120000, unchanged |
| `git diff --check` | PASS |

The module graph still has no import binding, URL resolution or HTML module loader. `js_module_export` is an internal read path, not a JS module namespace object. Fixed Grid 32 `no_output` IDs have not been remeasured or counted as PASS. q1082-i01 partial cleared, p216 whole and p189/p173 all-test 100% remain uncleared. Shared projection/GitHub publication pending with Q1; no push.
