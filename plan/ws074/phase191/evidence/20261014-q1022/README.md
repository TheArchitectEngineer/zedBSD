# q1022 text-decoration inline CSSOM

Pinned WPT `2d66b9b7998bb58c336138c178323ddee857b586`, manifest [p173](../../../phase173/evidence/20261013-q1012/manifest.json), localhost single-origin runner. [report.json](report.json) contains all 16 per-test reports. Target `text-decoration-valid.html` 17/17 and `text-decoration-shorthand.html` 5/5 subtests PASS. Area total 15 PASS / 1 FAIL, 165 PASS / 14 FAIL subtests. Remaining computed shorthand test fails 14/14.

Implementation: production shorthand parser now provides canonical specified serialization and four longhand values. Inline CSSOM getters read from the stored shorthand; mutating a longhand expands it into four declarations. CSS-wide values retain their keyword. Host build succeeded with warnings 0, `host-cssom` 12/12, `host-css-rule-model` 44/44, `git diff --check` PASS. `clang-format` unavailable; source reviewed against C and browser component full rules. [Acid summary](acid-summary.json): Acid3 score 100/100, pixels exact; Acid2 1,865/120,000 different, unchanged.

Limit: computed shorthand and text-decoration thickness in computed style remain absent; p191 whole and all 3,805 Interop IDs are not cleared. Shared Queue/Master/GitHub publication remains with Q1; no push.
