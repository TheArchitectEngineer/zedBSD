# q1012-i01 / Interop 2025 fixed selection and runner sample

2026-10-13 23:49–23:58 JST. Approved partial scope, max90min. [Phase](../../phase.md), [Queue](../../../queue.md), [pre-execution scope](../../selection-snapshot.md).

Source, reports and planning records were committed on `main` as `4c8aaf484` (`WIP`, required by the repository hook). No push or GitHub Issue/Project publication was performed.

## Inputs and reproduction

- Interop project revision `2d28de9c1e86182c38a7311854ca59cefde7ac04`, [2025 README](https://github.com/web-platform-tests/interop/blob/main/2025/README.md). 19 focus areas; Layout uses five carryover labels, pointer/mouse uses `interop-2023-events` per the official test links.
- `wpt-metadata` revision `018c06b2c0fae50400c71585c73b7f238a2d029a`, [metadata format](https://github.com/web-platform-tests/wpt-metadata). It is an external input under `/tmp/zedbsd-interop2025-metadata` in this environment, not vendored.
- WPT revision `2d66b9b7998bb58c336138c178323ddee857b586`, clean sparse checkout `/tmp/zedbsd-interop2025-wpt`. The sparse selection includes all metadata-selected source paths, WPT `tools`/`docs`, and needed references including `quirks/`. WPT [3-Clause BSD License](https://github.com/web-platform-tests/wpt/blob/master/LICENSE.md) was checked. It remains outside source control.
- WPT's own manifest command was run in its isolated Python venv: `python3 -c 'from tools.manifest.update import main; main()' --no-download`. The clean `MANIFEST.json` SHA-256 is `951619bb1a7c1f25ff0abc6e5ae4f15116a4e8874b053bd93cf0aca681098ac0`.
- Generator: `plan/ws074/tests/interop2025-manifest.py --wpt /tmp/zedbsd-interop2025-wpt --metadata /tmp/zedbsd-interop2025-metadata --out build/ws074-interop2025-q1012/manifest.json`, using WPT's pinned venv Python. [Saved manifest](manifest.json) SHA-256 `c12052922881eff3141aa56ba5e45ddd939c415200eb09e46cc70a5a17177234`.
- Runner inventory: `python3 plan/ws074/tests/interop2025-coverage.py --manifest build/ws074-interop2025-q1012/manifest.json --out build/ws074-interop2025-q1012/coverage.json`. [Saved coverage](coverage.json) SHA-256 `11f8fc7e9abfbdac22ff7ccb0497b400ba987b8e7d90dd8f2a6e41045465cad6`.

The clean and prior shared WPT manifest yielded byte-identical selected ID/type/source lists, although the prior checkout contained unrelated untracked Acid scratch files. The saved selection uses only the clean manifest. No label-selected ID was unresolved.

## Inventory

| Type | IDs | Current runner classification |
| --- | ---: | --- |
| testharness | 1783 | no official result protocol |
| reftest | 1974 | 1561 static candidates; 402 scripted test, 5 scripted reference, 6 variant URL unsupported |
| crashtest | 48 | no official crashtest result protocol |
| **Total** | **3805 unique** | **2244 unrun/unsupported; 1561 static candidates, not yet all run** |

The 19 focus-area memberships total 3811 because six IDs have two area labels. The fixed manifest's 3805 unique IDs are the target set for this revision pair. The WPT dashboard's browser score is not inferred from these counts. [summary.json](summary.json) has all hashes and counts.

## Selected static reftest sample

Command pattern: `python3 plan/ws074/tests/run-wpt-reftests.py --root ROOT --selection-manifest build/ws074-interop2025-q1012/manifest.json --limit N --save-failures 2 --out build/ws074-interop2025-q1012/GROUP`. The test server used localhost HTTP; browser binary SHA-256 `5b6b533edabe77cf74b759e484c3b767bd1689d2879167c0381c7f5b5479aaa8`, 800×600 and the runner's fixed comparison fonts. Every sampled test ID was verified in the saved selection. `--selection-manifest` prevents nearby non-Interop tests from entering the sample. These are runner pixel comparisons, not an official wpt.fyi run.

| Focus/root | PASS | FAIL | error | Report |
| --- | ---: | ---: | ---: | --- |
| anchor positioning / `css/css-anchor-position` | 0 | 10 | 0 | [report](anchor-selected/report.json) |
| Layout Flexbox / `css/css-flexbox` | 0 | 10 | 0 | [report](flexbox-selected/report.json) |
| Layout Grid / `css/css-grid` | 2 | 3 | 0 | [report](grid-selected/report.json) |
| backdrop filter / `css/filter-effects` | 3 | 2 | 0 | [report](backdrop-selected/report.json) |
| writing modes / `css/css-writing-modes` | 0 | 5 | 0 | [report](writing-selected/report.json) |
| **sample total** | **5** | **30** | **0** | |

An initial unfiltered anchor sample included nine non-Interop tests, so it is excluded from these results. A `css/CSS2` selected query found zero Layout tests because the labeled Layout tests reside in `css/css-flexbox` and `css/css-grid`; it is likewise excluded. A selected testharness page (`/css/css-anchor-position/at-position-try-parse.html`) loaded via `browser --run --settle-ms=5000` and returned exit0 with empty stdout/stderr; there is no PASS evidence.

## Validation and limits

`PYTHONPYCACHEPREFIX=/tmp/zedbsd-interop2025-pycache python3 -m py_compile` passed for the two new scripts and changed reftest runner. `git diff --check` passed. The runner's newly fixed symlink path and selection filter were exercised by the 35 actual selected tests. No browser source changed, so Acid3 was not rerun. Full static set, scripted references, variant URLs, HTTPS/multiple origins, testharness and crashtest are not covered by this attempt. The sample PASS/FAIL ratio is not a 3,805-test pass rate.

Next: [p189](../../../phase189/phase.md) captures testharness results; [p190](../../../phase190/phase.md) covers remaining reftest/crashtest execution. p173 whole baseline and the user's 100% goal remain open. GitHub publication and shared Master projection remain pending; `master.md` was not edited.
