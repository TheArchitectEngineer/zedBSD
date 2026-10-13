# q1025 Window unique-id named lookup

The [HTML Standard Window named-property algorithm](https://html.spec.whatwg.org/multipage/nav-history-apis.html#named-access-on-the-window-object) supplies names from connected HTML element IDs. This partial implementation exposes a unique nonempty ID as a live, nonenumerable Window property, leaving an existing ordinary Window property in place. Duplicates, `name`, and child navigables remain for p192.

[Fixture](window-named-id.html) / [output](host-output.txt): `a1` and `window.a1` resolve to the same element; `window.document` retains the existing document; after an ID change the old global is absent and the new one resolves; after removal it is absent. Host build warning0, native property checks65/65, realm checks30/30, `git diff --check` PASS. C and browser component rules were manually reviewed; clang-format unavailable.

Pinned WPT `beforematch-attribute-removal-001.html` [result](test-01.json) changed from `a1 is not defined` to the next failure (`a1.open` undefined), still no completion. `at-scope-parsing.html` [result](test-02.json) changed from `main is not defined` to a separate null/undefined property failure, still harness error. The fixed textdecoration computed [result](test-03.json) remains PASS 14/14. These results do not claim that the two affected WPTs pass.

[Acid summary](acid-summary.json): Acid3 score100/100 and exact 0/480000 pixel difference; Acid2 1865/120000 different, unchanged. WPT/Master/public ABI unchanged. p192 whole and the Interop 2025 all-3,805 objective remain uncleared. Shared Queue/Master/GitHub publication Q1 owned, no push.
