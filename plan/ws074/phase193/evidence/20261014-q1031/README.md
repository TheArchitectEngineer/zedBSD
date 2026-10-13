# q1031 details toggle task

`details.open` IDL changes and fragment ancestor reveal now queue a non-bubbling `toggle` event through the existing zero-delay task timer. The event runs after the synchronous script, and repeated changes to one details element before delivery coalesce into one notification. The full ToggleEvent state fields, content-attribute mutation entry points, and all details tests remain outside this partial Queue.

[Local fixture](toggle-fixture.html) / [output](host-output.txt) verifies no synchronous toggle, one notification after three `open` assignments in one task, non-bubbling event, and final open state. The pinned `hidden-until-found-and-details.html` [result](test-01.json) now PASS (1/1 subtest). `beforematch-attribute-removal-001.html` [result](test-02.json) remains PASS.

Host build warning0, native property65/65 and realm30/30 PASS; `git diff --check` PASS. Manual C and browser component review performed; clang-format unavailable. [Acid summary](acid-summary.json): Acid3 score100/100 and 0/480000 different pixels, Acid2 1865/120000 different pixels, unchanged. p193 whole and the 3,805-ID goal remain uncleared. Shared Queue/Master/GitHub publication is Q1 owned; no push.
