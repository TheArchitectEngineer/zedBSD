# Standards and automation coverage

Full standard: [C coding style](../coding-style.md). Concise version not created;
load the full applicable sections before editing. Non-C areas use their established
local conventions and Phase requirements.

| Rules / check | Command / observed version | Coverage and limit |
| --- | --- | --- |
| Formatting (§3, §5, §8) | `clang-format-19 '-style={BasedOnStyle: InheritParentConfig, ColumnLimit: 0}' FILE`; 19.1.7, root `.clang-format` | Tabs, spacing and braces. Scope only new / moved implementations or edited ranges. Restore definition arguments and compound-condition clause lines required by the full standard; keep static prototypes on one physical line. Formatter output alone does not establish conformance |
| Mechanical C rules (§3–§8, §10, §14) | `python3 plan/tools/style-check.py FILE... --summary`; Python 3 | Calls in conditions, declaration placement, missing paragraph / forward-declaration structure, multiline bodies, comment form, names, goto. Read the tool's scope and false positives; it cannot judge comment purpose, object lifetime, ownership, or every return shape |
| Whitespace | `git diff --check -- <changed-paths>` | Diff whitespace only |
| Build | `make -j64 disk-image` for WS104 amd64; GNU Make 4.4.1 | Compile/link; record configuration and classify project vs external-package warnings. Other Phases use their approved command |
| Boot | `OUTPUT=build/<W>/boot plan/tools/boot-test.sh IMAGE` | Login prompt in PNG, inspect and present it. Console / serial logs are not boot evidence |
| Runtime | Named host / guest suites in the approved Phase | Actual bounded behavior only; QEMU / Venus is separate from physical acceptance. Serialize image builds |
| Full standard (§1–§14) | Agent / human review of all WS changes, including complete new / moved implementations | File organization, lifetime / ownership comments, meaningful names and paragraphs, evaluation order, allocation / initialization, call and branch shape, success / refusal paths, licenses and test controls. Record exact source scope, exceptions and limits |
| Plan sync | `python3 plan/tools/sync.py status` | State / outbox visibility only. This checkout's GitHub publication is deferred; local records are not remotely synchronized |

No aggregate `make check`. No unrelated mass formatting. clang-format 19.1.7 was
installed on the host for WS104 (2026-10-01), without changing the locked target
toolchain or the checked-in formatter configuration. Versions here are observations,
not a new project-wide pin. The full document prevails over formatter defaults;
scoped formatting and manual restoration were recorded in WS104's final review.

HAL authority: the 2026-09-25 user decision permits implementation changes to
existing HAL declarations. Changes to `include/hal/hal.h`, its API / contract or HAL
responsibility require approval of the exact diff. This narrows the earlier
2026-09-12 instruction; [Guardrail](../guardrail.md) records the current rule.
A formatter or build never supplies that approval.
