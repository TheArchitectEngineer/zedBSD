# q1039 details crashtest runner

The new plain-HTTP [crashtest runner](../../../tests/run-interop2025-crashtests.py) verifies the pinned WPT revision and selection manifest, serves the original files on localhost, loads each selected page with browser `--render --async --settle-ms=3000`, and requires both a zero exit code and a decodable nonempty 800×600 image. It records nonzero exit, signal crash, process timeout and missing/invalid image separately. The browser headless mode executes page scripts and settles the page before rendering.

All three fixed details crashtest IDs match the manifest and produced **3 local PASS / 0 crash / 0 timeout / 0 no-image**. [Report](report.json) retains exact command, binary and image hashes, exit status and streams. Outcome classification controls passed 5/5; Python compile and `git diff --check` passed. Browser, WPT and Master were not changed, so host rebuild and Acid reruns were outside this runner Queue.

This measures the current host renderer under one HTTP origin. The remaining 45 Interop crashtests and their environment requirements are unmeasured, and local PASS is not official WPT certification. p190 whole and the 3,805-ID objective remain uncleared. Shared Queue/Master/GitHub publication is Q1 owned; no push.
