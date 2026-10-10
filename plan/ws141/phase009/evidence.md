# p009 software evidence — 2026-10-11

Base: main `091c65e4a`. Worktree: `.claude/worktrees/rpi4-sshd`, branch `codex/rpi4-gpu-init`. Current config.mk copied unchanged from main; RPi4/BCM2711 GPU enabled. Shared LLVM symlink read-only, no toolchain build/change.

## Commands and outcomes

- `cc -std=c99 -Wall -Wextra -Werror -Iinclude -Isrc -I. plan/ws141/tests/firmware-clock-host-test.c src/drivers/gpu/bcm2711/firmware.c -o build/ws141-firmware-clock-host-test`
- `build/ws141-firmware-clock-host-test`: PASS, 9 cases (zero/off, existing high rate, core off/configured, absent, SET_RATE error, clamping, enable refusal, short response, wrong clock). Provider is modeled; actual helper source runs.
- `sh plan/ws141/tests/v3d-hardware-host-test.sh > build/ws141-gpu-init-v3d.log 2>&1`: exit0/PASS. Actual hardware owner accepts PA35/VA35 and publishes the existing 4MiB table. Too-small VA rejected before allocations/MMIO writes. Original PA36/VA32 fixtures remain exercised by later cases. Hardware/IRQ/cache events are modeled, not real GPU execution.
- `make -j16 build/arm64/vmunix > build/ws141-gpu-init-kernel-final.log 2>&1`: exit0, warnings0/errors0, arm64 ELF/raw-image checker PASS. Final source after style/comment fixes rebuilt.
- `git diff --check`: PASS.
- `clang-format-19 --lines=125:240 src/drivers/gpu/bcm2711/firmware.c` and new test formatter output reviewed in own build/. Canonical multiline definitions/calls/comments retained manually where repository formatter conflicts with full coding-style. No unrelated mass formatting.

Tools: Debian GCC14.2.0; clang-format19.1.7; configured existing LLVM cross compiler.

## Changed-source conformance and functional review

C full standard read/reviewed: function declarations/definitions, declaration-first locals, semantic comments, guarded failures, request/response length checks, success return conventions. Existing style outside changed sections preserved. New helper is independent original Zlib implementation of documented mailbox operations; no reference code copied. GPU internal API only; no public HAL/UAPI changes.

Readout completes emulator guard and firmware screen observation before preparing HSM/core. No HVS/PV/HDMI register access occurs on preparation failure. HSM rate is raised only below120MHz; GET_RATE preserves the next-enable rate of stopped clocks, and SET_RATE uses three request words with turbo changes skipped. SET_STATE uses two words. Fresh state/rate checks authorize MMIO; errors retain console clocks rather than turning them off. Existing port selection and later reset/scanout stages remain responsible for display takeover.

V3D's width reports capability, not the driver's allocated VA range. Linux v3d_gem.c also allocates4MiB table and limits its allocator to lower4GiB; v3d_mmu.c does not add an ADDR_CAP write. This repair keeps existing uint32 VA issue paths and checked PTE/allocator bounds; it does not expand address space or speculate about cap encoding. Existing physical-width/PTE encoding checks remain.

## Limits / next evidence

No physical GPU/scanout/Keiland test, SSH update, reboot, QEMU, main merge or push. User must verify actual clock provider acceptance and subsequent N0/V4/V5/V6 progress. Host PASS and kernel build do not establish Keiland rendering or WS completion. Main integration requires concrete approval. WS remains incomplete; shared Master/Queue/cache/history/GitHub projections remain Q1-owned and unchanged.

## Final input/artifact SHA256

```
56c11753594045c9edce375c3eefa3dd0c3f9bfb50de3e5ea81867d380760c06  config.mk
c6f09cbec9fd36827f46e192d526a5f566816914f325946fe61f222d01abe3e2  src/drivers/gpu/bcm2711/bcm2711-private.h
3221dc9cb08a52651e7554cae29b481c7df36f20504c5c6c0a77eed08020aefa  src/drivers/gpu/bcm2711/firmware.c
f4067d7cab8daf76d3f039e70a316da102eefef92ad829e4084188ba43e01e03  src/drivers/gpu/bcm2711/readout.c
4463b2a5e10fcb9cf2e4a0ac60429f87233d3174f34fc23f639590f3b82dc4fd  src/drivers/gpu/bcm2711/v3d-hardware.c
15f0a7e4c60ba74fda8551a20d9a9b3d978f6cc6c45178c22d16406e1e1a60b9  plan/ws141/tests/firmware-clock-host-test.c
eabf2e5a21ec9eea4f88dd0794a8be2eb8c2b8e315111ed6d99614a847c5195e  plan/ws141/tests/v3d-hardware-host-test.c
966cdc09e5dea9a60311194b9e0719e720e86f64b6179ba64893496d7b8e4d3e  build/arm64/vmunix
```
