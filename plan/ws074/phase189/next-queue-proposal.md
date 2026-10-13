# Proposed q1014-i01 / p189 partial: one selected testharness ID per focus area

Status: proposed, not authorized. Proposed timebox: at most 90 minutes after approval.

## Evidence and reason

[q1013](evidence/20261014-q1013/README.md) captured WPT completion and 12 subtests for one selected HTTP HTML page, with 4 PASS/8 FAIL and correct runner FAIL. The fixed [q1012 manifest](../phase173/evidence/20261013-q1012/manifest.json) has 1,783 testharness IDs in 19 focus areas; 1,659 unique IDs end in plain `.html` and lack an HTTPS marker or query, though worker/generated contexts and test-specific infrastructure still need inspection. One page cannot establish baseline coverage or reveal area-specific runner failures.

## Exact bounded scope and criteria

1. Extend the q1013 runner with deterministic batch selection of **at most one plain HTTP HTML testharness ID per focus area**, sorted by fixed manifest ID (at most 19 executions). Exclude worker/generated/HTTPS/query IDs from this first batch and state the reason; preserve them in the 3,805-ID manifest. A duplicate ID may represent two focus areas but is executed once with both memberships.
2. Run the batch against the pinned WPT checkout and current host browser with per-test process/virtual-time bounds. Store each raw result and a compact per-area report. Distinguish test PASS/FAIL, harness error, timeout, process error, no output/no completion, and infrastructure unsupported; never infer PASS from exit0 alone. Check batch selection membership and deterministic ordering with focused tests.
3. Record each area outcome and runner/environment blockers as evidence for the next p189 scope. No browser engine, public ABI, WPT test, metadata, Acid3 expectation, or `master.md` change. If a test cannot run within single HTTP origin, record it as unsupported/unrun and continue independent areas. Stop by 90 minutes with evidence and residual; p189 whole and p173 whole remain uncleared unless their own full criteria are actually met.

Prerequisite: [q1013 result](evidence/20261014-q1013/README.md) and fixed q1012 manifest. Allowed source: `plan/ws074/tests/` runner and focused tests, `plan/ws074/phase189/` evidence, WS074 Queue/WS projections, ignored build and external pinned WPT checkout. Applicable [Guardrail](../../guardrail.md); browser source is outside this attempt. The prior q1013 approval does not authorize q1014.
