<!-- awesome-plan project=zedbsd record=ws242-p004 -->

# ws242-p004: chip の SM を compiler に、QMD 3.0・compute の class・PCAS2_B、3D の class、HBLANK_DELAY

Status: planned
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
