# Producer-exit wait deadline and final-close ordering

`q310-fence-exit-002` is retained as FAIL. Its consumer received `VK_TIMEOUT` (2), while this fault scenario required `VK_ERROR_DEVICE_LOST` (-4); no success marker was accepted. The earlier `q310-fence-exit-001` passed. Total harness durations were 14.502 and 15.076 seconds respectively, including boot/setup. The old attempts did not record the individual Vulkan wait interval.

At attempt002, the consumer sent the producer its exit acknowledgement and immediately began the common 10-second Vulkan wait. The producer still had to receive that acknowledgement, run `_exit` thread termination, and release its GPU file. Final `gpu_close` called the backend command drain before retiring producer fence bindings; that drain independently allowed 10 seconds. The asynchronous transport watchdog also used a 10000ms deadline. These equal deadlines permit consumer timeout to win over terminal-error publication, depending on scheduling and clock/tick boundaries. The observed TIMEOUT is a valid bounded wait result; it neither fabricates GPU success nor demonstrates permanent failure to propagate producer loss. Exact historical event ordering cannot be reconstructed from the old untimestamped markers alone.

The fault test now uses `FENCE_TEST_EXIT_WAIT=30000000000` ns, separate from the unchanged ordinary `FENCE_TEST_WAIT=10000000000` ns. Standard `CLOCK_MONOTONIC` samples surround the real call and produce:

```text
GPUFENCE PRODUCER_EXIT_WAIT result=<VkResult> elapsed_ms=<measured> budget_ms=30000
```

The test still requires NOT_READY before allowing producer exit, DEVICE_LOST afterwards, and successful final waitpid of the producer. TIMEOUT or SUCCESS remains failure. Clock failure or backward measurement also fails. The larger fault-only budget covers process teardown and the transport's finite retirement boundary without changing libvulkan timeout semantics or ordinary test limits.

The kernel owner independently moved producer-binding error retirement before backend drain, preserving the drain barrier for callback/completion/session/DMA lifetime. This separates loss of the producer's authority from physical retirement; possible delay before final-close entry remains covered by the explicit fault budget. New runtime acceptance is owned by the root run and is not claimed by this local source check.

Validation of the application change: target Clang `x86_64-unknown-zedbsd`, strict warnings and `-Wdeclaration-after-statement` passed using C11 parsing for existing libc public headers; focused diff whitespace check passed. An initial strict C89 parsing attempt rejected pre-existing `restrict` declarations in libc time/signal headers and was not treated as PASS. No libc/HAL workaround was applied.


## q310 最終実受入

同一最終kernelのwayland-008、direct-004、fence-exit-003、recovery-004は全てPASS。producer終了の実waitは10msでDEVICE_LOST、recoveryは10000ms timeout後に新contextを確認。直接表示全runのQEMU traceで320×240 BLOB要求107回・同寸法legacy要求0回と実画面を照合した。source/artifact/renderer hashと全失敗履歴は[最終結果](results.md)および[verification](final-verification.json)に対応する。旧attemptの数値・fixture限界は履歴として保持する。
