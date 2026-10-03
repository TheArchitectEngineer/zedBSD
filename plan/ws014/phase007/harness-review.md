# Fault and ordinary-capture harness verification

The independent q310 review corrected three concrete gaps in `plan/ws014/tests/wayland-qemu.py`, `venus-qemu.py` and `run-venus-remote.py`:

- Renderer pidfds are checked before/after binding and before SIGSTOP using executable path/inode/device, process starttime, parent and owned-QEMU ancestry. A departed pidfd is rejected even if its numeric PID is reused.
- SIGTERM unwinds through renderer cleanup and records FAIL. Every owned pidfd receives a SIGCONT attempt; errors are aggregated and cannot be erased by repeated cleanup. Catchable signals are blocked while that bounded cleanup drains. Uncatchable SIGKILL cannot execute Python cleanup; the remote watchdog attempts TERM first.
- Fault acceptance checks the uploaded common transport harness SHA256, alongside existing exact image, selected renderer library mapping/hash and server hash checks.

`timeout 120 python3 plan/ws014/tests/test-renderer-pause.py` passed all five fixtures: actual stop/resume of three fresh owned children, fake PID/ancestry/death rejection, all-handle cleanup after a failure, actual common.run TERM-to-FAIL cleanup with fake QEMU I/O, and failed/stale-transport acceptance rejection. Signals reached only these disposable fixture processes.

`timeout 120 python3 plan/ws014/tests/test-vkdemo-oracle.py` passed all 13 fixtures (10.482 seconds). Two initial errors came from stale fixture inputs: ordinary mode still emitted readback PRESENT and omitted the new renderer/fault options. The corrected fixture supplies no-readback SUBMITTED progress and two independently supplied 320x240 images while retaining the six-frame mathematical texture oracle. Python syntax and focused diff whitespace checks passed.

Ordinary live capture establishes two distinct actual 320x240 displayed images, progressing no-readback submissions, DONE and shell return. It does not equate an asynchronous submit timestamp with a sampled displayed frame. Exact image/time/oracle matching remains the separate six-frame diagnostic path. These local fixtures supplement the root-owned real QEMU acceptance; earlier failed attempts and their evidence are retained.

The final direct-display harness also enables the QEMU `virtio_gpu_cmd_set_scanout_blob` and `virtio_gpu_cmd_set_scanout` trace events in the bounded VM argv. After the existing image/DONE acceptance, it requires at least one nonzero-resource 320x240 BLOB request and no legacy request at that same extent. Console requests at 640x480 and disabled resource0 requests are distinguished. `scanout_trace` records request counts and BLOB resource identities with scope `whole direct acceptance run`; it is not ordinary-only timing attribution. The local wrapper independently re-parses the fetched, hash-checked renderer log and compares that result with the remote record.

These QEMU events occur before command validation. Their role is evidence of BLOB selection, combined with the already mandatory displayed pixels and successful application cleanup, not standalone proof that the backend accepted a request. The positive and negative trace fixtures cover a real-shaped BLOB record, console-mode separation, missing/disabled/wrong-extent events, and forbidden legacy fallback even when a BLOB event is also present. The updated `test-vkdemo-oracle.py` passed all 16 tests (10.385 seconds); `test-renderer-pause.py` passed its five tests again. Syntax and focused diff whitespace checks passed. The subsequent root-owned VM attempts provide actual trace evidence.


## q310 最終実受入

同一最終kernelのwayland-008、direct-004、fence-exit-003、recovery-004は全てPASS。producer終了の実waitは10msでDEVICE_LOST、recoveryは10000ms timeout後に新contextを確認。直接表示全runのQEMU traceで320×240 BLOB要求107回・同寸法legacy要求0回と実画面を照合した。source/artifact/renderer hashと全失敗履歴は[最終結果](results.md)および[verification](final-verification.json)に対応する。旧attemptの数値・fixture限界は履歴として保持する。
