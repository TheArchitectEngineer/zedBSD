# WS048 p014: ARM64 worker IRQ evidence / 2026-10-11

Base main2008584ae, private branch codex/rpi4-thread-irq. User photo `codex-clipboard-304b976d-59c4-4d42-8a47-602fa317ffa8.png` reports another boot freeze: Enable Slot doorbell returns, first wait prints irq=0, then completion poll resumed. No SSH available before boot; none attempted.

## Finding and repair

Scheduler context switching disables IRQ and releases its CPU lock before dispatch. Existing threads later restore IRQ through the suspended scheduler call. New ARM64 kernel threads enter arm64_kernel_task_entry instead, which previously called the worker without restoring IRQ. This differs from amd64_kernel_task_entry's explicit sti and from ARM64 user entry's unmasked SPSR/eret. The USB root worker therefore begins with IRQ disabled, and its blocking/yield paths preserve that initial state. The photographed irq=0 is consistent with this source defect.

Add `msr daifclr, #2` at the new kernel entry before passing x20 as x0 and calling x19. Context-switch publication, stack and address-space selection are already complete; scheduler lock is released. Existing resumed-thread IRQ state is unchanged. This affects all newly created ARM64 kernel workers, which must be interruptible, not just USB. HAL API unchanged, authorized internal implementation repair.

Clarify the old misleading poll-resumed log: irq=0 skips sched_yield entirely. New first-poll logs distinguish polling entry, acquired event lock, and completed event sampling/dequeue acknowledgement. Logs are RPi4-only and once per Enable Slot command, not every polling iteration. No DMA/PCI alias or firmware changes.

This confirms the missing IRQ enable but does **not** establish the exclusive cause of the post-poll freeze or prove USB recovery. The earlier IRQ ack correction remains integrated; hardware acceptance remains unmet.

## Checks

- `python3 plan/ws048/tests/command-wait-host.py`: PASS 11 existing scenarios, including callers legitimately entering with IRQ disabled and fault/timeout handling. No new mock suite.
- `make -j16 build/arm64/vmunix`: exit 0, warning 0, error 0; ELF/raw-image format checks PASS.
- `build/llvm/bin/llvm-objdump -d --disassemble-symbols=arm64_kernel_task_entry build/arm64/src/hal/arm64/dispatch.o`: actual object begins `d50342ff msr DAIFClr, #0x2`, then `mov x0, x20`, `blr x19`. Confirms the mask/argument/call order. Does not simulate interrupt delivery.
- Current config.mk byte-identical to main. `git diff --check`: PASS.
- Final changed-source review against full coding-style.md, Guardrail and AGENTS: narrow assembly change, matching first-entry contract, caller IRQ preservation, diagnostic frequency/locking and canonical split call layout. clang-format-19 range output reviewed; canonical multiline function/call layout retained manually. Versions unchanged from p013: repository clang23.1.0, host cc14.2.0, clang-format19.1.7.

Build log and disassembly are disposable worktree files `build/rpi4-thread-irq-build.log` and `build/rpi4-thread-entry-disassembly.txt`. Final SHA256:

```
56c11753594045c9edce375c3eefa3dd0c3f9bfb50de3e5ea81867d380760c06  config.mk
f48354cac350d866075ba1728aa18ff6892b6292398ba22ac0dff1b5c75a53e7  src/hal/arm64/dispatch.S
78f84150ca4ec79f6a3d1e95c25886651217680872fccc533f458f2a906d7be0  src/drivers/pci/pci-xhci.c
eb31ef7cb503bad4e2c2c998d9e8eca78f06bb9f8e4ef77c05c89b41056c780c  build/arm64/kernel.elf
4bb78939242b4efc8afc6c901acdba034c5cae29dcd55d8b3e397e52b06ef3ff  build/arm64/vmunix
```

Source/build criteria cleared; physical boot, IRQ delivery and USB enumeration unverified. No QEMU/full SD build/remote deployment/push. Next: approve/integrate concrete commit, user boots new kernel and checks wait irq=1, login/network progress, USB input and last event stage if it still stops. IRQ-off at a later legitimate critical-section caller must not be forcibly overridden in command_ex. WS remains incomplete; shared Master/Queue/history/cache/GitHub projections Q1 pending.
