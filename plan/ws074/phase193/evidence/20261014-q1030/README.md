# q1030 fragment ancestor attribute identity

DOM attribute mutations compare interned name pointers. The q1028 ancestor reveal used an ordinary new string for `hidden` removal, so removal silently missed the existing attribute. It now uses the heap's atom for both `hidden` and `open`, preserving native attribute identity.

[Local fixture](reveal-fixture.html) / [output](host-output.txt) verifies that inner details opens before inner `beforematch`, inner `hidden` is removed before outer `beforematch`, then outer `hidden` is removed and outer details opens. Pinned WPT `beforematch-attribute-removal-001.html` [PASS](test-02.json) and `beforematch-attribute-removal-002.html` [PASS](test-03.json). `hidden-until-found-and-details.html` [result](test-01.json) moved past its state assertion but emits no completion because a later `<details>` `toggle` event is not delivered. It is not PASS.

Host build warning0, native property65/65 and realm30/30 PASS; `git diff --check` PASS. Manual C and browser component review performed; clang-format unavailable. [Acid summary](acid-summary.json): Acid3 score100/100 and 0/480000 different pixels, Acid2 1865/120000 different pixels, unchanged. p193 whole and the 3,805-ID goal remain uncleared. Shared Queue/Master/GitHub publication is Q1 owned; no push.
