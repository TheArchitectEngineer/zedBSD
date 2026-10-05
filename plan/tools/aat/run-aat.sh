#!/bin/sh
# ws173-p004: runs scenario tests (tests/scenarios, plan/tests.md) against a target with the AAT's automatic helpers
# (plan/tools/aat/scenarios/helpers_*.py, by the scenarios' ids) and writes the run's record.
#   plan/tools/aat/run-aat.sh TARGET OUTDIR [SUITE|ID|PATTERN|area:NAME ...] [--record PATH] [--no-samples]
#   TARGET: 5330 (root@10.0.30.3), qemu (the guest of GUEST_RUNTIME/session.json) or user@host[:port].
#   The default is the smoke suite.  OUTDIR/summary.md is the record (every verdict, its note, the screenshots and
#   each scenario's steps in OUTDIR/records/); --record PATH copies it (plan/ws173/runs/DATE-SUITE.md).
# Verdicts: pass, fail, needs-person (a look at a screenshot, or a person's hands), by-agent (no helper: an agent
# carries the scenario out from its document), not-run (the real machine's scenario on QEMU, or no session).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
exec python3 plan/tools/aat/scenarios/runner.py "$@"
