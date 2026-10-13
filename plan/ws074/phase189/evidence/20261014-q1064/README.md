# q1064-i01 — all currently eligible testharness IDs

Pinned WPT `2d66b9b7998bb58c336138c178323ddee857b586`; fixed Interop 2025 manifest contains 1,783 unique testharness IDs. Under the existing single-localhost-HTTP, physical-HTML candidate filter, 1,455 IDs are eligible and 328 are excluded with explicit reasons. q1063 measured offset 0–127. q1064 measured all remaining 1,327 IDs in eleven shards of at most 128 each, with fixed manifest/WPT/browser conditions. Each raw callback report is reversibly gzip-compressed; `progress.jsonl` is fsynced after every result.

| Set | PASS | FAIL | No completion | No output | Harness error | Total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| q1063 first shard | 18 | 93 | 8 | 4 | 5 | 128 |
| q1064 remaining | 142 | 817 | 291 | 69 | 8 | 1,327 |
| **All eligible** | **160** | **910** | **299** | **73** | **13** | **1,455** |

The 328 excluded IDs remain unrun, not PASS: 45 generated contexts, 44 HTTPS, 126 driver/origin/worker requirements, 80 URL variants, 33 worker/substitution paths. `analysis.json` includes per-focus outcomes; 799 measured IDs contribute to `layout` (103 PASS, 382 FAIL, 281 no completion, 33 no output). Focus memberships overlap, so their sums are not a unique-ID denominator. Current results are local WPT callback observations and cannot be called an official Interop score.

Verification: each of the eleven q1064 shards exited 0. Exact selected ID set across q1063+q1064 has 1,455 unique IDs and matches the eligible-list SHA256 in every header/report. All 1,327 q1064 gzip reports were decoded and checked against the same ID/outcome in checkpoint and report; totals agree. `py_compile`, gzip roundtrip, and `git diff --check` PASS. Browser/WPT/Master unchanged, so host build and Acid were not repeated. Command template: `python3 plan/ws074/tests/run-interop2025-census.py --wpt /tmp/zedbsd-interop2025-wpt --manifest plan/ws074/phase173/evidence/20261013-q1012/manifest.json --out plan/ws074/phase189/evidence/20261014-q1064/shard-OFFSET --offset OFFSET --limit 128 --gzip-raw` for OFFSET=128,256,…,1408.

q1064-i01 partial cleared; p189 whole uncleared. p189 needs execution of the 328 excluded IDs and correct outcomes for 1,283 measured non-PASS IDs. p190 needs reftest environment coverage and repair; p173 all 3,805 PASS remains unmet. Shared Master/Queue/GitHub publication pending with Q1; no push.
