<!-- awesome-plan project=zedbsd record=ws101p015 -->

# ws101-p015: L1 デモの場面 S13 を demo の image で

Phase ID: `ws101-p015`
Parent: [WS101](../ws.md)
Status: cleared（2026-09-30。5330 の QEMU passthrough、demo の image、kei の Terminal で `sh /usr/share/gpudemo/s13.sh` が通った。画面: [s13-live.png](s13-live.png)）
Phase disposition: normal
Queue: main の割り当て（2026-09-30、P2、`wt/ws101`）

## 何をしたか

- `plan/ws101/tests/demo/config.mk`: demo の構成（`plan/ws075/demo/config-demo-hdmi.mk`、main が `gpudemo` を足した）から noct を外したもの。
  WS101 は Noct を build しない（toolchain の規則）ので、`/bin/noct` は main の `build/demo-lcd9/bin/noct`（accel ON）を file として入れる。
- `plan/ws101/tests/demo/build-s13-image.sh [BUILD]`: `build-demo-image.sh BUILD passthrough` と同じ手順（accounts、壁紙、apps.conf、guest の鍵、
  `I915_TEST_VBT=y`）で image を作る（既定 `build/ws101-p015-demo`、`NOCT` で noct を選ぶ）。Noct の build は起きなかった（stamp の時刻を確認）。
- `plan/ws101/tests/demo/s13-hw.sh [IMAGE] [OUTDIR]`: `plan/ws075/tests/hdmi-h4-hw.sh` で image を 5330 の passthrough で起動し（lock は start から
  stop まで）、kei の自動の login の後、Kei の button → App Home の Terminal の tile（1031,386）→ `sh /usr/share/gpudemo/s13.sh` と Enter を打ち、
  session・terminal・s13 の画面を撮る。

## 確認（5330 の QEMU passthrough。素の機械は未実施）

| 確認 | 結果 |
| --- | --- |
| `plan/ws101/tests/demo/build-s13-image.sh` | PASS |
| `plan/ws101/tests/demo/s13-hw.sh`（QEMU 09:33:16 起動、lock は約 5 分、`build/ws101-p015-s13-hw/`） | **PASS**（画面で判定）: kei の Terminal に「Kei GPU compute: 4000000 integers…」「CPU: 9 ms a run」「GPU: 249 ms a run (6 kernels ran on the GPU)」「**The CPU is 27.7 times as fast as the GPU.**」（p011 で直した表示が実機で正しく出た）「The results are the same on the CPU and the GPU.」。GPU の node は kei の権限で使えた |

## 制限

- 見せ方としては GPU が CPU の約 28 倍遅い（p011 の測定と同じ）。最適化（p016・p017）は 2026-09-30 ユーザーの判断で優先を下げた（ws.md）。
- 素の 5330 では未実施。

## Resume point

p015 は cleared。WS101 は L1 で区切り（ユーザーの判断で最適化の優先を下げた）。再開するなら ws.md の p016（260 ms の内訳、下の道具の案）から。
- p016 の測り方の案（2026-09-30、実装して build まではしたが実機で走らせずに戻した）: libglesv2 に `KEI_GLES_COMPUTE_TRACE=2` の段で、
  glBufferData・device の buffer の作成・upload・待ち（`gles_frame_wait`）・readback の memcpy・`gles_collect` の時間を stderr に出し、
  `plan/ws101/tests/hw/g3/run-g3.sh` に 4 call の traced の run を足して集計する。Noct は call ごとに `glGenBuffers`・`glBufferData`（16 MB × 入出力）、
  dispatch、`glFinish`、`glMapBufferRange(READ)` と memcpy、`glDeleteBuffers` をする（`accel_opengles.c` 1588〜2040 行）ので、16 MB の copy の
  回数と GEM の 16 MB の確保・解放が疑い。host の写像は WB（`gpu_mmap` は device の属性でなければ cache あり）。
