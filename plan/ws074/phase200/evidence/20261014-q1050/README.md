# q1050-i01 evidence — Flexbox distributed spacing rounding

Fixed WPT checkout `2d66b9b7998bb58c336138c178323ddee857b586`; local render at 800×600 with 5 s per case. [report.json](report.json) records **8 PASS / 2 FAIL / 0 timeout of fixed 10** (q1049: 5 PASS / 5 FAIL). `vert-001a`, `vert-002`, `vert-006` changed to exact pixel PASS. Remaining `vert-001b` has 22,520 different pixels, `wmvert-001` has 393,560. The runner output is local evidence, not official WPT certification.

Diagnosis: `vert-001a` test space-around positions y14.66/37.98/101.31 vs reference calc margins y14.67/38.00/101.33. Integer division truncated each distributed gap below the nearest 1/64 px. `vert-006` column-reverse retained a 1/64 px opposite-edge remainder after nearest rounding; using the residual trailing gap makes the reversed position agree with its authored reference.

Implementation: positive distributed free space in `layout/flex.c` uses nearest layout-unit division for space-between/around/evenly. Reversed distributed lines put the rounding remainder at the ending gap. Signed overflow handling from q1047 remains intact. No WPT or public ABI edits.

Verification: `sh plan/ws074/tests/host-build.sh plain` exit0, warning0; `python3 plan/ws074/tests/run-physical-justify-tests.py` 12/12; native CSS rule model44/44, CSSOM12/12, CSSOM mutation12/12; `git diff --check` PASS. WPT command: `python3 plan/ws074/tests/run-wpt-reftests.py --root css/css-flexbox --selection-manifest /tmp/ws074-q1046-selection.json --render-timeout 5 --limit 0 --out plan/ws074/phase200/evidence/20261014-q1050`. Final Acid report `build/ws074-acid-q1050-final/report.json`: Acid3 100/100, pixel diff0/480000; Acid2 diff1865/120000 unchanged.

q1050-i01 and p200 whole cleared. p197/p198/p199/p190/p173 still require separate remaining outcomes. Shared Master/Queue/GitHub publication pending with Q1; `plan/master.md` unchanged, no push.
