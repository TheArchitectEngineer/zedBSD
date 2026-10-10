# p010 evidence — 2026-10-11

Base main211be00dc / own branch codex/rpi4-gpu-init / worktree .claude/worktrees/rpi4-sshd. No config/toolchain changes.

## Physical evidence (user supplied)

User photos show core preparation ENODEV(13), V7 noop completed, V8 all4096 pixels ff317ce0, V9 TFU all64 pixels. Later user explicitly reports boot resumed and supplies `V10 reset 1 post-reset noop complete` and `render node ready native Vulkan/strict queue`. Therefore no persistent V10 freeze is established. Existing physical rendering diagnostics pass; display scanout/Keiland remains unverified. This repair has not been deployed or tested physically.

## Functional findings / references

CORE(id4) is critical to the shared bus. The old helper replaced its nonzero rate with zero whenever GET_STATE omitted the leaf enable flag; preparation and later display CLOCK_ON therefore returned ENODEV. Keep provider/absent/zero-rate refusals and enable request, but read CORE rate independently of that flag. Leaf clocks (HSM/V3D/BVB) still require the running flag. This is a scoped interpretation based on the reported core200MHz/off state and provider implementation; its physical success is pending.

Primary reference: [Linux firmware clock driver](https://raw.githubusercontent.com/torvalds/linux/master/drivers/clk/bcm/clk-raspberrypi.c), CORE CLK_IS_CRITICAL and separate get-rate/prepare. Behavior/numerical facts only, no copied source. Fixed local reference ws141-codex/plan/ws141/temp/linux/drivers/clk/bcm/clk-raspberrypi.c was also read. Raspberry Pi6.12 provider likewise implements rate retrieval separately; its older ops lack prepare/unprepare and are not the source of the newer critical flag.

The real kern_usleep_range (src/kern/clock.c) yields to a scheduler tick and explicitly treats maximum as advisory. 50000 sleeps at1ms can take50 seconds instead of500ms. Each V3D job progress window now samples kern_rtc_read_counter and expires after half a second, with at most4 progress windows and existing finite iteration guard. Sleep/preemption may overshoot a deadline, but it is never extended by counting the requested minimum sleep as actual elapsed time. Counter failure/frequency change returns EIO with unretired DMA ownership preserved. Completion is checked before expiry; native reset sequence is unchanged. V10 now logs the transition from observed timeout to reset.

## Commands/results

- `cc -std=c99 -Wall -Wextra -Werror -Iinclude -Isrc -I. plan/ws141/tests/firmware-clock-host-test.c src/drivers/gpu/bcm2711/firmware.c -o build/ws141-firmware-clock-host-test`
- `build/ws141-firmware-clock-host-test`:10 cases PASS, including core gate remains off/rate200MHz accepted, core zero rejected, HSM enable refusal still rejected.
- `sh plan/ws141/tests/v3d-hardware-host-test.sh > build/ws141-core-timeout-host.log 2>&1`:exit0/PASS. Actual job code with1ms tick times out in500 sleeps and10ms tick in50 sleeps; missing/changed counter rejects with DMA unretired. Existing physical-event/reset/retention cases pass in the explicit MMIO/IRQ model.
- `make -j16 build/arm64/vmunix > build/ws141-core-timeout-kernel.log 2>&1`:exit0, warning0/error0, ELF/raw image check PASS.
- `git diff --check`:PASS.
- `clang-format-19 --lines=494:550 src/drivers/gpu/bcm2711/v3d-job.c`:output reviewed. Retained canonical full-style comments/one-argument-per-line where formatter conflicts. Manual full C review covers all new/changed code: declaration-first locals, purpose comments, early failure returns, locking/ownership, no new HAL/UAPI, original Zlib implementation. Other existing code left unchanged.

Versions: GCC14.2.0, clang-format19.1.7; existing configured LLVM cross compiler. No aggregate make check, QEMU, toolchain rebuild, main merge, push or deployment. Final p007 main integration remains pending; shared records/GitHub are Q1-owned.

## Input/artifact SHA256

```
56c11753594045c9edce375c3eefa3dd0c3f9bfb50de3e5ea81867d380760c06  config.mk
c9ffc9b4a2307764d16bb6462e1d3254ae114d7665fce4f838d68125011596e0  src/drivers/gpu/bcm2711/firmware.c
6ffda617895cd888e9bee37784024c59afaa17469e3662a0d13806e9219036f7  src/drivers/gpu/bcm2711/v3d-job.c
4d682daa006f2e288d14dcae93a17b6e3a1f911b464f8d34d94b5646187eacfa  src/drivers/gpu/bcm2711/v3d-diagnostic.c
f3fe16017ebdf09d4258506d23c74524bb46244e379dd5eda149696cb295116f  plan/ws141/tests/firmware-clock-host-test.c
fc4cdb8907bce6b1e66c309937613d15a4d01712eee0ad0c7d06723e450b3148  plan/ws141/tests/v3d-hardware-host-test.c
c5a25361bce87f5ce37c6dccbe76fe879a7afbbc3056ebac8f29c366083ce74d  build/arm64/vmunix
```

## 自動統合の承認と統合結果（2026-10-11）

ユーザー「mainへのコミットは自動でお願いします。」、確認回答「今後は自動で統合してください。」により、このセッションの承認済み作業のmainコミット/統合は以後自動実施する。従来のQ1担当による個別main統合確認をこの指示で置換する。pushや実機更新の指示ではない。共有記録への投影は引き続きQ1へ保留。

USB `3e9ace7b9` とGPU `461906448` を専用worktreeでmerge `afe01157f` にまとめ、現在のconfig.mkで `make -j16 build/arm64/vmunix` を実行。warning0/error0、arm64 ELF/raw-image check PASS。USB/GPU sourceが各個別検証版と一致することも確認。clean main `211be00dc` を `afe01157f` へfast-forwardし、変更17path全てが検証済みworktreeとbyte一致した。main統合完了。実機の新core判定/待ち時間/USBログ抑制は未確認で、WSの残る実機条件は保持。push・実機更新/再起動は未実施。

最新USBログでdevice1/port1 `2109:3431` class09、usb-hub、4portsの登録を確認し、slot1の反復Stopped通知がhub監視と整合することを確認。USBキーボードの入力成功を意味しない。GPUは旧版のV7〜V10成功とrender node登録までユーザー確認済み。

Combined artifact SHA256: `3d0f1e879be8d1e4bf03f10560695f1454e91243273848813e1e2c179abb12be` (`build/arm64/vmunix`)。旧個別build hashは当時の証拠として保持。組合せbuild log: `build/ws141-gpu-usb-integration.log`。
