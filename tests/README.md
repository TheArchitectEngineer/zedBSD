# Tests

Scenario tests for zedBSD and Keiland: what a person does with the system and
what they should see, written so that an agent can carry them out on a real
machine or in QEMU. The format, the suites and how scenarios are chosen are
described in [docs/reference/scenario-tests.md](../docs/reference/scenario-tests.md).

- `scenarios/os/` — operating-system functions
- `scenarios/desktop/` — the desktop
- `scenarios/apps/` — applications
- `suites/` — named sets of scenarios (`smoke`, `full`, per area and per application)

Scenarios are source: change them together with the behaviour they describe.
