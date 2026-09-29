<!-- awesome-plan project=zedbsd record=ws101p010 -->

# ws101-p010: GLES の compute の i915 での G2（5330 の passthrough）

Phase ID: `ws101-p010`
Parent: [WS101](../ws.md)
Status: cleared（2026-09-30。5330 の passthrough で glescompute の全 step（indirect を除く）PASS、egltest の feedback・queries の場面（egl-p030・p027 の回帰）PASS。indirect は p007 の後）
Phase disposition: normal
Queue: main の割り当て（2026-09-30、サブエージェント `wt/ws101`。実機は WS075 と共有、`flock /tmp/i915-hw.lock` の下で短く使う）

## 範囲（[design.md](../design.md) §5.3、§6 の p010）

p009 の libglesv2 の OpenGL ES 3.1 の compute を i915 のネイティブの実行器（5330 の QEMU passthrough）で動かす。基本の dispatch は p005、
shared・barrier は p006 の後。indirect（`vkCmdDispatchIndirect`、実行器の opcode 111）は p007 で入るまで範囲外。egl-p030（transform
feedback と版）が i915 でも通ることを回帰として確かめる（main の指示）。

## 変えた file

| file | 内容 |
| --- | --- |
| `userland/desktop/glescompute/main.c` | `--no-indirect`（indirect の step を飛ばし「GLESCOMPUTE indirect SKIP」を出す）。i915 の実行器が `vkCmdDispatchIndirect` を持つまで（p007）使う |
| `plan/ws101/tests/hw/gles-hw.sh`（新） | 実機の run: image を lock の外で build（`GLES_HW_BUILD_ONLY=1` で build だけ）、lock の下で 5330 へ写し `~/bigbang/run-parity-vk.sh`（参照の passthrough、guest の power-off で終わる）で走らせ、guest の disk から `plan/ws031/tests/ufs-cat.py` で program の log を読み、lock を返してから判定を出す |
| `plan/ws101/tests/hw/gles/`（新） | image の構成（`config.mk`: `plan/ws031/tests/config-zdesktop-hw.mk` から browser・files・noct・clang・libc++ を外し glescompute を足す）、`rc.conf`、service `gles1`・`poweroff`、`run-zdesktop.sh`（壁紙なしの compositor）、`run-gles.sh`（glescompute を auto（repeat 100）と default（repeat 20）で、egltest の feedback と queries の場面を compositor の window で、各 run の後に compositor の process を記録） |

libglesv2・i915 の driver の source は変えていない（p009 のままで通った）。

## 確認

| 確認 | 結果 |
| --- | --- |
| host: glescompute の 3 つの shader（add・noct・shared）を GLSL の compiler で link、spirv-val、i915 の host の compile（`plan/ws101/tests/host/compute-dump.c`） | PASS（72・81・103 命令） |
| `plan/ws101/tests/hw/gles-hw.sh build/ws101-p010-gles-hw`（**5330 の QEMU passthrough、i915 のネイティブの実行器**。lock は 07:45:29〜07:51:39 の約 6 分） | **PASS** |
| └ glescompute auto（surfaceless の pbuffer） | 「OpenGL ES 3.1 Kei」「OpenGL ES GLSL ES 3.10」3.1。上限 count 65535×3・size 128・128・64・invocation 128・binding 4・block 4・alignment 64・shared 16384・block size 1 GiB。version・limits・add・noct・shared・chain・release・repeat（100 回）・errors PASS、indirect SKIP、`DONE failures=0` |
| └ glescompute default（default display の pbuffer） | 同じく全 PASS（repeat 20 回）、indirect SKIP |
| └ egltest --scene=feedback（egl-p030 の回帰、compositor の window） | `EGLTEST FEEDBACK ready failures=0`・`EGLTEST CHECK run=feedback failures=0 glerror=0x0`・`EGLTEST DONE ... failures=0`（main の直した版の検査を含む） |
| └ egltest --scene=queries（egl-p027 の回帰） | `CHECK run=queries failures=0 glerror=0x0`・`DONE failures=0` |
| └ compositor | 4 回の run の前後とも `/bin/wayland` が生きていた（pbuffer の context が画面を取らない） |

判定は guest の disk の program の log（`build/ws101-p010-gles-hw/guest-logs.txt`）による。QEMU の serial・console の log では判定していない。

## 未実施・制限

- indirect の dispatch（i915 は p007 の後。そのとき `--no-indirect` を外して再実行する）。
- egl-p030 の画面の照合（今回は egltest の自己検査の行だけ。画面は撮っていない）。
- 素の 5330（p012）。
- 時間の測定（p011 の G3）。

## 残り

- main への依頼: Tools 節の WS101 の行に `plan/ws101/tests/hw/gles-hw.sh`（と `gles/`）を足す。
