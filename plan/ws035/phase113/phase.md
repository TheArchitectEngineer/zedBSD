<!-- awesome-plan project=zedbsd record=ws035p113 -->

# ws035-p113: sessiond が GPU の attach の途中だけ /dev/gpu0 を上限付きで待つ（BUG-092）

Phase ID: `ws035-p113`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28。kernel の `hw.gpu.attaching` と sessiond の最大 15 秒の待ち。実機の i915 で回避なしに greeter が HDMI に出た、
GPU の無い QEMU は待たずに console の login）
Phase disposition: normal
承認: 2026-09-28 main の依頼（デモ 2026-10-17 の前に直す BUG-091・BUG-092）。

## 範囲

- [BUG-092](../../bugs/BUG-092.md): graphical な起動で sessiond が i915 の `/dev/gpu0` の公開（約 1 秒後）より前に始まり console に落ちる。
- GPU の driver が attach の途中の間だけ上限付きで待つ。GPU の無い機械はすぐ console。kernel が既に出す信号を使うか、最小のものを足す。
- demo の image の `greeter_gpu` の回避を削除する。

## 設計

既存の信号: 無い（`/dev` の node は公開の時に現れるだけで、「まだ来る」を表すものが無い。PCI の一覧は userland に無い）。最小の信号を足した:

- sysctl `hw.gpu.attaching`（`CTL_HW`・`HW_GPU_ATTACHING` = 4、uint32、読むだけ）= driver が attach したが node をまだ公開していない
  （または諦めていない）GPU の device の数。kernel の `kern_gpu_attach_begin()`・`kern_gpu_attach_end()`（`include/kern/sysctl.h`、
  `src/kern/sysctl.c`、atomic、0 未満にならない）。
- i915: PCI の attach（boot の platform の発見の中、init より前）の `drv_i915_device_schedule_start()` で数え始め、node の公開
  （`drv_i915_publish()` の `drv_gpu_register()` の成功）、start の worker の終わり（失敗を含む）、worker の作成の失敗、pending の
  ままの stop のどれかで 1 回だけ外す（`drv_i915_device_attach_settled()`、`attach_counted` は start registry の lock の下）。
  Venus は attach の中で同期に公開するので数えない。
- sessiond の `main_ready()` → `main_wait_display()`: `/dev/gpu0` が無ければ `hw.gpu.attaching` を読む。0（または sysctl が無い kernel）
  なら最後にもう一度だけ見て、無ければすぐ `SESSIOND CONSOLE reason=no-display`。0 でない間は 100 ms ごとに見直し最大 15 秒
  （超えたら `SESSIOND DISPLAY timeout waited_ms= attaching=` の後 console）。待った時は `SESSIOND DISPLAY waited_ms=`。
- HAL は変えていない。UAPI は sysctl の leaf を 1 つ足しただけ。

## 変更

| 所 | 内容 |
| --- | --- |
| `include/uapi/sysctl.h` | `HW_GPU_ATTACHING` |
| `include/kern/sysctl.h`・`src/kern/sysctl.c` | `hw.gpu.attaching` の leaf と数、`kern_gpu_attach_begin/end()` |
| `src/drivers/gpu/i915/i915.h`・`device.h`・`device.c`・`i915.c` | `attach_counted`、`drv_i915_device_attach_settled()` と数える所・外す所 |
| `userland/desktop/sessiond/main.c` | `main_wait_display()`・`main_gpu_attaching()`、file の注記 |
| `plan/ws075/demo/` | `greeter_gpu`・`greeter-gpu.sh`・`rc.conf` を削除、`build-demo-image.sh` から参照を外した |

## 確認

| 確認 | 結果 |
| --- | --- |
| build（`plan/ws075/demo/build-demo-image.sh build/b091 passthrough`、ws075-p014 と同じ image） | 成功（warning は外部の Noct の既存 1 件だけ） |
| **実機**（5330 の i915 の passthrough、回避なし、2026-09-28） | guest の sessiond.log: `SESSIOND DISPLAY waited_ms=300` → `SESSIOND START` → `GREETER start` → `HANDOFF greeter ready: go`。greeter が HDMI の 1920x1280 に出た（scanout の buffer、worktree の `build/ws075-shots/bug092-greeter-hdmi-no-workaround.png`）。login・session・Log Out の後の greeter も正常（ws075-p014 の run） |
| QEMU（GPU の無い q35、同じ image = `login=graphical`） | `plan/tools/boot-test.sh` PASS（`build/b091-boottest/login.png` = worktree の `build/ws075-shots/bug092-qemu-nogpu-console-login.png`: `init: greeter ended; starting getty_console` → `login:`）。別の 150 秒の run の guest の sessiond.log は `SESSIOND CONSOLE reason=no-display errno=6` の 1 行だけ（`DISPLAY` の行なし = 待っていない） |
| 規約: `plan/tools/style-check.py` の変えた行 | sessiond・sysctl.c は指摘 0。i915 の critical section の unlock の行の指摘は既存の形と同じ |

## 未実施・残り

- Venus の QEMU での graphical な起動（Venus は数えないので従来どおり）。
- bare metal の 5330 での待ち時間（passthrough では 300 ms）。15 秒を超える機械が出たら上限を見直す。
- 他の GPU の driver（将来）は同じ `kern_gpu_attach_begin/end()` を使う。
