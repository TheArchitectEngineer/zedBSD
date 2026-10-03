<!-- awesome-plan project=zedbsd record=queue-q507 -->

# Queue q507: WS074 Acid2 exact rendering

Status: finished (2026-09-30)
Approval: current user, “Run ws074.” Exact scope was ws074-p099 only; Acid3,
the full WPT CSS2 run, ES modules, HAL, and toolchain source changes were
excluded.

- q507-i01 / [ws074-p099](../ws074/phase099/phase.md): cleared. The pinned WPT
  Acid2 result improved from the reproduced 90.56% baseline to a byte-identical
  100.00% match with no crash or timeout.
- General corrections cover conflicting height constraints and indefinite
  percentages, background shorthand/attachment, `<object>` image fallback,
  Adam7 PNG reconstruction, CSS point borders and float painting order, inline
  replaced backgrounds, and whole-image alpha compositing. No Acid2-specific
  coordinates or pixel exceptions were added.
- Plain and ASan host builds completed without warnings. Acid2 passed at
  100.00% in each; golden dumps passed 62/62 in each; object tests passed
  100/100 in each; the PNG suite, CPU/GPU comparisons, style check, Python
  compilation, and diff check passed.
- Acid3 remains score 9 / 100 and 40.35% pixel agreement with its existing
  TypeError. That work belongs to p100 and was not authorized in q507.
- The user-requested `make toolchain-cache` and `make toolchain` completed;
  stale generated caches were preserved under `/tmp` and the toolchain was
  relocked read-only. Aggregate `make check`, guest boot, and GitHub publication
  were not performed for this host rendering Phase.

Upcoming outlook: ws074-p100 (Acid3 100/100), then p101 (full fixed WPT CSS2),
in the recorded order. Neither is authorized by this finished Queue.
