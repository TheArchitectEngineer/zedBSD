# q1035 first summary UA display

The HTML Standard's [details and summary UA rules](https://html.spec.whatwg.org/multipage/rendering.html#the-details-and-summary-elements) give the first direct `summary` child of `details` `display: list-item`. The local UA stylesheet had only the generic `summary { display: block }` default. q1035 added the specific UA display rule; author declarations still override it.

The pinned [`details-display.html` result](details-display.json) changed from 7/8 to **8/8 subtest PASS**. The test covers the first summary, later summary, summary outside details, and author overrides. `sh plan/ws074/tests/host-build.sh plain` passed with zero compiler warnings/errors; `host-css-rule-model` 44/44 and `host-cssom` 12/12 passed. The fixed Acid run in `build/ws074-acid-q1035/report.json` retained Acid3 score100, inventory100/100, 0/480000 pixel difference, and Acid2 1865/120000 pixel difference. `git diff --check` passed. One UA string line was reviewed against the C and browser component standards; clang-format is unavailable.

This partial correction does not implement the disclosure marker, `::details-content`, or grid blockification. p193 whole and the 3,805-ID goal remain uncleared. WPT/Master/public ABI were not changed. Shared Queue/Master/GitHub publication is Q1 owned; no push.
