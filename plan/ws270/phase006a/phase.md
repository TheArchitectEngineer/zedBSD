<!-- awesome-plan project=zedbsd record=ws270-p006a -->

# ws270-p006a Keiland の起動と不足の一覧

- 親: [WS270](../ws.md)、Status: in-progress（2026-10-20 P1、q1356 / q1356-i01。必須項目 2a（sessiond の gpu1 の待ち）の実装・build・host 試験まで。Keiland の起動と不足の一覧は WS269 p010 の後）
- 設計: [design.md](../design.md) 改訂 2.1 の §2.2 の 2a・2b、§3 の p006a。
- 依存: WS269 p010、本 WS の p004、WS269 p005（powervr の遅い段が gpu1 の登録まで `hw.gpu.attaching` を数える。q1356 で同時に実装）。

## 範囲

1. （2a、済み）sessiond の `main_wait_display` が、`/dev/gpu0` があっても `hw.gpu.attaching` が 0 でない間は待つ（上限は今の `MAIN_DISPLAY_WAIT_MS` 120 s）。
2. （2b、未着手）VF2 で Keiland を起動し、Vulkan の機能の不足の一覧と、それを確かめる試験の program を作り、WS269 p011 の入力として Q1 に渡す。

## 2a の実装（2026-10-20 P1、q1356）

| file | 変更 |
| --- | --- |
| `userland/desktop/sessiond/fallback-rules.c`・`.h` | 判断だけの関数 `sessiond_display_next(present, attaching, waited_ms, limit_ms)` と `enum sessiond_display_step`（READY・NONE・WAIT・TIMEOUT）。attaching が 0 なら present で READY か NONE、そうでなく上限に達したら TIMEOUT、他は WAIT |
| `userland/desktop/sessiond/main.c` `main_wait_display` | 毎回 gpu0 を stat し attaching を読んで上の関数で決める。NONE は前と同じ最後の 1 回の stat（数と look の間に出た node を拾う）。TIMEOUT は log（`SESSIOND DISPLAY timeout waited_ms=… attaching=… present=…`）の後、gpu0 があれば使い、無ければ前と同じく errno を返す |

### amd64・rpi4 の振る舞い（code で確かめた事）

- **rpi4（bcm2711）**: `src/drivers/gpu/bcm2711/` に `kern_gpu_attach_begin` の呼び出しは無い（grep）。attaching は常に 0 なので、gpu0 があれば READY、無ければ NONE（最後の stat）で、前と同じ。
- **amd64 の i915 だけの機械**: i915 は device ごとに start の registry の解放で数え（`i915/device.c:921`、`:370`）、node の登録か失敗で `drv_i915_device_attach_settled` → `kern_gpu_attach_end`（`:1110-1121`）。前は gpu0 が見えた時点で抜け、今は settled まで待つ。settled は node の登録の直後なので、差は高々 1 回の poll の間隔（`MAIN_DISPLAY_POLL_NS`）。
- **amd64 の nvrtx（resident かつ display）**: `nvrtx.c:385-388` で +1、worker の終わり（`:409-413`）で -1。node の登録は worker の最後の段なので同じく差は poll の間隔。
- **振る舞いが変わる場合**: GPU が 2 つあり、片方の gpu0 が先に出て、もう片方（i915・nvrtx・amdrdna）がまだ attaching の機械（例 iGPU と dGPU の両方を使う機械）。前は先の gpu0 で greeter を起動し、今は全部が settled まで待つ（上限 120 s）。libvulkan が node の組を instance ごとに固定する（design §2.2 の 2a）ので、待つ方が意図に合う。T1 の回帰の対象（amd64 は 1 GPU の QEMU）ではこの場合は起きない。

## 確かめ（2026-10-20、worktree /home/awe/zedBSD-worktrees/p1、host。QEMU は起動していない）

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| VF2（desktop の試しの config）kernel と sessiond | `timeout 1800 make ZEDBSD_CONFIG=plan/ws270/tests/config-visionfive2-desktop.mk BUILD=build/visionfive2 vmunix build/visionfive2/bin/sessiond` | rc=0、`warning:`・`error:` 0 件（log `build/q1356-vf2.log`、main.c・fallback-rules.c・powervr.c・firmware-boot.c を compile） |
| riscv64-virt の kernel | `timeout 1800 make ZEDBSD_CONFIG=config/ci/config-riscv64-virt.mk BUILD=build/riscv64-virt vmunix` | rc=0、0 件（powervr は入らない） |
| amd64 の sessiond | `timeout 2400 make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/amd64 build/amd64/bin/sessiond` | rc=0、0 件 |
| rpi4 の sessiond | `timeout 2400 make ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/p1-rpi4 build/p1-rpi4/bin/sessiond` | rc=0、0 件 |
| host 試験（新規） | `timeout 120 sh plan/ws270/tests/run-host-display-wait.sh` | plain・ASan/UBSan とも `PASS 8 FAIL 0` |
| host 試験（WS239、同じ file の既存） | `timeout 120 sh plan/ws239/tests/run-host-fallback-rules.sh` | PASS（plain、ASan/UBSan） |

未実施: QEMU・実機。amd64・rpi4 の desktop の回帰は下の T1 の依頼で。

## T1 への依頼文（Q1 が判断して送る）

- **ws270-p006a の sessiond（amd64・rpi4 の回帰）**: P1 の WIP（返却の SHA）を含む main で、amd64（QEMU、`config/ci/config-amd64.mk`）と rpi4（QEMU raspi4b か既存の rpi4 の回帰の道）の image を作り、`plan/tools/boot-test.sh` で (1) graphical login（`login=graphical`）の greeter の画面、(2) text login（graphical なし）の login prompt を PNG で撮る。合否: (1) greeter が出る、(2) login prompt が遅れずに出る。

## 残件

- 2b（Keiland の起動と不足の一覧）は WS269 p010 の後。
- T1 の回帰（上）。
- 片付け（Q1 に依頼）: worktree /home/awe/zedBSD-worktrees/p1 の `build/q1356-p004host/`・`build/q1356-vf2.log`・`build/q1356-virt.log`・`build/q1356-amd64.log`・`build/q1356-rpi4.log`・`build/tmp/ws270-display-wait*`。
