# q1079-i01 — standalone JavaScript module scope

The pre-change `browser --js --module` probe leaked top-level `var` and `function` onto `globalThis` (`1 undefined function f()` for `var a=1; let b=2; function f(){}`). `js/scope.c` now binds module `var`, function, `let`, `const`, and class declarations in the module program's scope rather than the realm's global declaration lists. `js/compile.c` emits module function declarations to those bindings and calls module code with `this=undefined`; classic scripts continue to use the global object. No public ABI, HTML loader, import/export compiler, WPT, runner or Master change.

Verification on `build/ws074-host/plain/browser`:

| Check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit 0, no warnings |
| `browser --js --module plan/ws074/tests/module/scope.js` vs `scope.expected` | exact output match: strict top-level `this`, hoisted `var`/function, in-module closures, class/let/const access, no `globalThis` or global `Function` visibility |
| `python3 plan/ws074/tests/run-js-tests.py --program build/ws074-host/plain/browser` | 14/14 classic JS suites PASS |
| Classic top-level var/function probe | `7 9` from `globalThis`, pre-existing behavior maintained |
| `python3 plan/ws074/tests/run-acid-tests.py --out build/ws074-acid-q1079` | Acid3 score100/inventory100/100/pixel diff0/480000; Acid2 diff1865/120000, unchanged |
| `git diff --check` | PASS |

This is one finite foundation for p216. A module with `import`/`export` still fails JS compilation, and HTML `<script type="module">` is still skipped; therefore the fixed Grid 32 `no_output` IDs are not counted as PASS or remeasured here. Next scope is named import/export binding and module graph/HTML loading. q1079-i01 partial cleared, p216 whole uncleared, p189/p173 all-test 100% uncleared. Shared projection/GitHub publication pending with Q1; no push.
