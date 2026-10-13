# q1040 complete fixed crashtest census

The runner's `--focus all` mode selected exactly the 48 unique crashtest IDs in the pinned Interop 2025 manifest. Every ID is plain HTML and was rendered through one localhost HTTP origin at 800×600 with fixed fonts, a 3-second settle budget and a 60-second process timeout. The [report](report.json) records **48 local PASS / 0 crash signal / 0 process error / 0 timeout / 0 missing image / 0 unsupported**. Each PASS has exit code 0 and a verified 800×600 image. All three details images and outcomes match [q1039](../20261014-q1039/report.json).

Selected focus distribution: anchor-positioning 9, backdrop-filter 1, details 3, layout 21, scope 1, view-transitions 8, webcompat 4, writingmodes 1. The ID set, counts, image validity and details hashes were checked against the fixed manifest and prior results. Selection/classification controls passed 6/6; Python compile and `git diff --check` passed. Browser/WPT/Master were unchanged, so host build and Acid reruns were outside this runner Queue.

This clears the **local crashtest measurement** for the fixed 48 IDs under these host conditions. It does not certify official WPT environment equivalence, reftest coverage, testharness coverage, or the overall 3,805-ID goal. p190 whole remains uncleared. Shared Queue/Master/GitHub publication is Q1 owned; no push.
