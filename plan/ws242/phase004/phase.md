<!-- awesome-plan project=zedbsd record=ws242-p004 -->

# ws242-p004: chip の SM を compiler に、QMD 3.0・compute の class・PCAS2_B、3D の class、HBLANK_DELAY

Status: in-progress（2026-10-29 P2: 実装済み・host 試験と build まで。実機・QEMU の試験は未実施。cleared にしない）
Disposition: normal
Parent: [WS242](../ws.md)
Queue: q1678-i01（P2）
依存: ws242-p003
設計: [design.md](../design.md) §2.2〜§2.5、第 2 版の改訂 V4・V6・V8。review の M1・M4・M5、m2〜m5、m8

## 範囲

1. compile の呼び手 4 か所（`draw.c`・`compute.c`・`vk/vk-pipeline.c`・`vk/vk-execute.c`）を `_sm(…, sm, …)` に。vk の層は owner の口
   `nvrtx_vk_shader_model(void *owner)`・`nvrtx_vk_threed_class(void *owner)`（`nvrtx-vk.h`、`vk-node.c` か `node.c` で実装）。
   `execute_compile()`・`execute_start()`・`pipeline_compile_stage()` に引数が要る（WS291・WS250 と重なるので変える前に Q1 へ）。
2. draw の init を class 版（`nvrtx_push_draw_init_class`）に（`draw.c`・`vk/vk-execute.c`）。
3. compute の launch: 版・class・PCAS2_B の要否の小さな記述（`nvrtx-compute.h` の型、profile を compute-launch.c に持ち込まない）を渡す
   `nvrtx_qmd_build_for()`・`nvrtx_push_compute_launch_for()`。QMD V03_00 の位置、`SET_OBJECT` は compute の class、Ampere 以降は
   `SEND_SIGNALING_PCAS2_B`（0x02c0、値 3）。今の名前は Turing の wrapper のまま（byte で同じ）。carve-out・warps の欄は使わない（Turing の値は Ampere の carve-out の集合に入る、速さだけ）。
4. display: `HEAD_SET_RASTER_HBLANK_DELAY`（0x2364）を c67d・c77d の head の設定に、firmware の armed の値を写す。c57d では書かない。
5. host 試験: QMD の bit の期待値は open-gpu-doc の header から機械的に抜いた表（出典の版と sha256 付き）、Turing の QMD・launch の push が
   今の関数と byte で同じ（shared 0・32・48・64 KiB）、TU・GA・AD の launch の push の語の全部、vk の試験の stub に SM と class、
   vk の道で SM86・89 の compile を 1 回。
6. WS250 R10（Ampere の runlist ごとの chid）の確かめ: 衝突しないことを記録、違えば session の chid を graphics の runlist の上限の中に。

## 受け入れ

- host 試験が通る（Turing の出力が byte で同じ）。amd64 vmunix・libvulkan の build が warning 0。実機の確かめは無し（RTX 2070 は p008）。

## 実装（2026-10-29 P2）

| file | 変更 |
| --- | --- |
| `node.c`・`vk/nvrtx-vk.h` | owner の口 `nvrtx_vk_shader_model()`・`nvrtx_vk_threed_class()`（family の record の `sm`・`class_threed`） |
| `vk/vk-execute.c` | `struct nvrtx_vk_execute` に `shader_model`・`threed_class`（`execute_open` で 1 回読む）、`execute_compile` に SM の引数（`_ir_sm`）、`execute_begin` の draw init を `nvrtx_push_draw_init_class` に。変更は 4 か所の小さな差（WS243 の P6 との重なりを小さく） |
| `vk/vk-pipeline.c` | `pipeline_compile_stage` に SM の引数（`_stage_sm`）、`pipeline_compile` で owner から 1 回読む |
| `draw.c` | draw-test の compile を `_ir_sm(profile->sm)`、init を class 版 |
| `compute.c` | compute-test の compile を `_ir_sm`、QMD と launch を `nvrtx_compute_kind`（profile の QMD の版・compute の class を詰めた小さな struct）で `_for` に |
| `nvrtx-compute.h`・`compute-launch.c` | `struct nvrtx_compute_kind`、`nvrtx_qmd_build_for()`（2.2 と 3.0 の動く field の表 `qmd_layout`、未知の版は EINVAL）、`nvrtx_push_compute_launch_for()`（`SET_OBJECT` は kind の class、Turing より後の class は `SEND_SIGNALING_PCAS2_B` 0x02c0 値 3）。旧名は Turing の wrapper。profile（`nvrtx-private.h`）は compute-launch.c に持ち込まない（review m4） |
| `plan/tools/nvrtx/qmd-fields-gen.py`・`qmd-fields.h` | open-gpu-doc（9fdf5c40…、MIT）の `clc3c0qmd.h` V02_02・`clc7c0qmd.h` V03_00・`clc9c0qmd.h` V03_00 から field の位置を機械的に抜いた表（sha256 付き、手で写さない。review M4） |
| `plan/tools/nvrtx/nvrtx-qmd-host-test.{c,sh}` | 新: 3 世代 × 5 の launch（shared 0〜64 KiB、1〜32 warp、barrier、buffer 0〜8）の全 field、field の外の bit が全部 0、Turing の旧名と byte で同じ、launch の push の全 method と値、未知の版の拒否 |
| `plan/tools/nvrtx/nvrtx-vk-d3-host-test.c`・`nvrtx-vk-host-test.c` | owner の口の stub。d3 は submit の場面を SM75/c597・SM86/c797・SM89/c997 の 3 回流し、push が各世代の 3D class を bind することを見る |

技術の判断（範囲の中）:

- **HBLANK_DELAY は入れない**（§2.4 は任意）。nouveau は GA/AD でも書かずに動き（`dispnv50/core.c` が c57d の core の code を使う）、nvkms が書く値は HDMI の DSC 以外は 0（＝既定）。armed の値を写すには armed の読み出しの表（`nvrtx_disp_head_copied`）に c57d に無い 0x364 を足し、Turing の push を class で分ける変更が display の 3 file と試験に広がる。実機の確かめが無いまま触る危険が利点より大きい。DSC の mode を GOP が組んだ GA/AD で絵が崩れたら、その時に armed を写す（Future Work の候補、Q1 へ）。
- carve-out・warps の欄は使わない（Turing の 32・64 KiB は GA/AD の carve-out の集合に入る。MAX は 64 KiB の 17 のまま。速さだけの差）。
- WS250 R10（Ampere の runlist ごとの chid）: nvrtx の doorbell は RM の devinfo の runlist id を上の bit に入れる（`channel.c`、nouveau の `tu102_chan_doorbell_handle` の GA/AD と同じ形）。固定の chid（work 4・copy 8・session 16〜）は runlist ごとの空間なので衝突しない見込み（review m8 と同じ）。code は変えない。実機で RM が chid を拒んだら session の chid を graphics の runlist の上限の中に。

## 確かめ（2026-10-29 P2、host と build だけ）

| コマンド | 結果 |
| --- | --- |
| 変更の前後の Turing の QMD と launch の dump（48 の入力：shared 0〜70000・4 の workgroup・barrier 0/1、scratchpad の使い捨て） | byte で同じ（`cmp` 一致） |
| `sh plan/tools/nvrtx/nvrtx-qmd-host-test.sh` | `PASS nvrtx qmd: 733 checks`。感度: `QMD30_BARRIER_COUNT_LOW` を 955 にすると 12 件 FAIL。shared config の幅 6→7 は検出されない（値は最大 17 で bit 663 に届かず、書いても 0、動きも同じ） |
| `sh plan/tools/nvrtx/nvrtx-vk-d3-host-test.sh` | `PASS nvrtx vk d3: 209 checks, 0 failures`（SM75・86・89 の 3 回） |
| `sh plan/tools/nvrtx/nvrtx-vk-host-test.sh` | `PASS nvrtx vk: 86 checks, 0 failures` |
| `sh plan/tools/nvrtx/nvrtx-sm75-host-test.sh` | `287 checks, 0 failures` |
| `sh plan/tools/nvrtx/nvrtx-sm75-desktop-test.sh --strict` | `186 compiled, 0 refused, 0 disagreed` |
| identify・gsp | p003 と同じ（PASS、gsp 379 checks SKIPPED 0） |
| `make -j16 ZEDBSD_CONFIG=plan/tools/nvrtx/config-amd64-nvrtx.mk BUILD=build/ws242-p004 build/ws242-p004/vmunix` | rc 0、`warning:` 0、amd64 vmunix check PASS |

- libvulkan: userland を変えていないので build していない（未実施）。
- 未実施: 実機（RTX 2070 は p008、RTX 30/40 は無し）、QEMU。

## plan/tools.md の登録の案（Q1 が merge の時に）

| 道具 | 何を確かめるか | 流し方 |
| --- | --- | --- |
| `nvrtx/nvrtx-qmd-host-test.sh`・`qmd-fields-gen.py`・`qmd-fields.h` | compute の launch の QMD（Turing 2.2、Ampere・Ada 3.0）を open-gpu-doc から機械的に抜いた field の位置で全 field と field の外の 0 を、Turing の出力が旧名と byte で同じこと、launch の push（class、PCAS_B／PCAS2_B） | `sh plan/tools/nvrtx/nvrtx-qmd-host-test.sh`、`PASS nvrtx qmd: N checks`。表の作り直しは `python3 -I plan/tools/nvrtx/qmd-fields-gen.py <open-gpu-doc> > plan/tools/nvrtx/qmd-fields.h` |
