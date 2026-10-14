# q1141-i01 — remaining Grid alignment triage

The [q1140 fixed Grid502 census](../../../phase189/evidence/20261014-q1140/README.md) leaves 192 whole FAIL IDs under `/css/css-grid/alignment/`. [Analysis](analysis.json) extracts each ID's first failed WPT callback from the unchanged gzip raw report and classifies its first reported geometry symptom: width110, `offsetLeft`37, `offsetTop`26, height18, other1. File-name families overlap in cause: positioned63, vertical45, baseline43, self-alignment74, stretch48. These counts show where failures occur, not a proof of one defect.

Three representative source/results:

1. `grid-self-alignment-stretch-vertical-lr-001.html` is first of 16 vertical-lr stretch FAIL files. Its first item expects width10 but gets100. The WPT grid has vertical-lr writing, fixed 100/150px columns and 150/100px rows, and mixed justify-self/align-self. `layout/grid.c` still sizes the first logical column through the physical width path and forces `justify=stretch` whenever the grid or child is non-horizontal. Its logical inline axis must map to physical y; the current path can make a 100px physical width. The corresponding vertical-rl family has 16 more FAIL.
2. `grid-column-axis-alignment-positioned-items-001.html` and `grid-row-axis-alignment-positioned-items-001.html` each first fail in vertical-lr subtest `.grid 3`, expected width10/actual60. Their horizontal subtests were already repaired in p217; simply extending horizontal static x/y positions would not address the physical axis and vertical text width. Together these families leave 34 FAIL files.
3. `grid-align-baseline-flex-002.html` first fails `offsetLeft` expected40/actual10, a separate synthesized baseline issue. The baseline family has 43 FAIL file names, and cannot be counted as a vertical-axis fix without further investigation.

[p220](../../../phase220/phase.md) has a bounded non-Grid vertical inline implementation; its output is not yet a general Grid axis mapping. A finite next implementation candidate is fixed-track vertical-lr Grid with simple empty items: map logical column and row tracks to physical y and x, place/size items for `start`/`stretch`, then verify a minimal 2×2 fixture and selected fixed WPT before expanding to text, absolute children, RTL and vertical-rl. It must preserve the horizontal Grid/Acid3 gates. No browser source, WPT, runner, ABI or Master changed in this read-only Queue.

q1141-i01 diagnosis partial cleared; p217 whole and p189/p173 all-focus goals remain uncleared. Shared projection/GitHub publication pending with Q1; no push.
