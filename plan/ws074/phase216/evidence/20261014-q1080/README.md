# q1080-i01 — module named export declarations

After q1079 established local module bindings, `export function`, `export class`, `export var`, `export let`, and `export const` still stopped at the JS compiler's generic "modules" error. The `JS_NODE_EXPORT` wrapper now passes declaration hoisting and lexical binding through the same scope pass used for an ordinary declaration, and the compiler evaluates its contained declaration in the module scope. No namespace object or import graph is produced by this partial step. `export {}`, default and re-export forms remain explicitly unsupported; an `export {x}` probe exits 1 with `not supported yet: this kind of module export`.

Verification on `build/ws074-host/plain/browser`:

| Check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit 0, no warnings |
| `browser --js --module plan/ws074/tests/module/export-declarations.js` vs expected | exact output: hoisted function/var, class/lexical use and global isolation |
| `browser --js --module` on pinned WPT `css/css-grid/abspos/support/positioned-grid-descendants.js` | exit 0; its shared `export function runTests` compiles/evaluates without invoking DOM |
| q1079 module scope fixture | exact output maintained |
| `python3 plan/ws074/tests/run-js-tests.py --program build/ws074-host/plain/browser` | 14/14 classic JS suites PASS |
| `python3 plan/ws074/tests/run-acid-tests.py --out build/ws074-acid-q1080` | Acid3 score100/inventory100/100/pixel diff0/480000; Acid2 diff1865/120000, unchanged |
| `git diff --check` | PASS |

Import and HTML module loading are still absent, and exports cannot yet be linked from another module. The fixed Grid 32 no_output IDs remain non-PASS; this Queue did not recensus them. q1080-i01 partial cleared, p216 whole uncleared, p189/p173 all-test 100% uncleared. Shared projection/GitHub publication pending with Q1; no push.
