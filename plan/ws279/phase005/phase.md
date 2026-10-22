<!-- awesome-plan project=zedbsd record=ws279-p005 -->

# ws279-p005 描画の経路（7.1 の shader state record・texture・tile・Vulkan の native の部分）

- 親: [ws279](../ws.md)、担当: P2（2026-10-22、worktree `agent/p2-pi5gpu`）
- Status: in-progress（compositor の quad（texture・blend・clear・copy・readback）が実機で全 pixel 一致。Keiland の画面（p007）での確認が残り）
- 依頼の元: 2026-10-22 Q1「続けて p006 … と p005（描画の経路）を進めて」。依存の p003・p004 は済み。

## 目的と受け入れの条件（design §2.2・§3・§6）

1. 7.1 の CL の pass（Binning・Common・RT Part1・ZS・dummy tile・Clear Render Targets・load/store）、draw の bin（Cfg Bits の Z clip、Clipper XY 1/64）、shader state record（rev ≥ 10 は draw index の形、32 byte）、texture state（R/B swap の bit、array stride は bit 33）を host で Mesa の 7.1 の XML と照合する。
2. Pi 4 の Vulkan の実行器（`vk/`・`render-*.c`・`v3d-memory.c`・compiler の共通部分）を Pi 5 でそのまま使い、版の違う encoder だけ 7.1 の写しにする。Pi 4 の kernel.elf は不変。
3. 実機: Vulkan の client が 7.1 で描いた結果を読み戻して一致する（最初の三角形と texture の quad の代わりに、compositor の quad の試験）。

## 作った物

- `src/drivers/gpu/bcm2711/` の 7.1 の写し（同じ関数名、Pi 5 の source の一覧で選ぶ）: `native71-pass.c`（render の前置き 93 byte、tile list 27 byte）、`native71-bin.c`（Cfg Bits に Z clip mode 2）、`native71-shader.c`（32 byte の record、`bcm2712_v3d_draw_index_record` で D0 の形）、`native71-texture.c`（array stride を bit 33 から）、`native71-viewport.c`（XY の scale を ×32、1/64 pixel）。
- 共有の file: 最初は `#if defined(HAL_BOARD_RPI5)` 2 つ（record の大きさ・device の名前）だったが、Q1 の指摘（2026-10-22、ユーザーは board の違いを `#if` より写して分ける方を好む）で、record の大きさは board の encoder の `bcm2711_native_shader_bytes()`（`native-shader.c` 36、`native71-shader.c` 32）に、device の名前と ID は `vk/vulkan-query.c` が engine の識別（hub IDENT1 の版 7 か）で選ぶ形に直した（2d52638b8）。Pi 4 の kernel.elf はこの直しで変わる（実行時の選択の code が入る、warning 0）。
- `v3d71-hardware.c`: 識別で rev ≥ 10 なら draw index の形。`attach71.c`: self-test の後に `bcm2711_render_register()`（render の node）。
- build: Pi 5 の GPU の switch の時、`vk/` の全部（video を除く）・`render-*.c`・`v3d-memory.c`・`native-colour.c`・`native-storage.c`・7.1 の compiler。
- 試験: `plan/ws279/tests/native71-pass-host-test.c`・`native71-pass-check.py`・`native71-state-host-test.c`・`native71-state-check.py`（WS141 の 4.2 の試験の 7.1 版、`v3d71-host-test.sh` から流す）、`vkquad-pi.c` と `vkquad-pi-build.sh`（vkquad を Pi の実行器が受け付ける形に: layout の変更を全て barrier で、render pass の最後の layout は GENERAL。image には入れず手で板に写す）。

## 確認

- host（2026-10-22 P2）: `sh plan/ws279/tests/v3d71-host-test.sh` → `native71-pass-check: PASS (7 complete 7.1 pass streams; ...)`、`native71-state-check: PASS (9 full 7.1 records and complete draw BCL; ...)`、他も PASS。rpi5 GPU 入り・rpi4 の build は warning 0、rpi4 の kernel.elf は e7b4e07ad と一致。
- 実機（2026-10-22 P2、kei@10.0.30.5 D0、4517a588a の build、`sysctl hw.gpu.start=1` の後に `sudo /tmp/vkquad-pi`）: `VKQUAD ok device=zedBSD V3D 7.1 (Keiland graphics) pixels=4096`（32×32 の 4 色の texture を 64×64 の青に blend して描き、左上 16×16 を別の image に copy、全部を buffer に読み戻して照合）。
- 経過: 最初の vkquad（そのまま）は render pass の最後の layout（TRANSFER_SRC）が実行器の対象外で ENOTSUP、次に barrier の無い upload が layout の照合で EINVAL（実行器の正しい拒否）。vkquad-pi で通った。
- 未実施: 深度・MSAA・複数の RT・scaled overlay（実行器が Pi 4 でも扱わない物）、TFU の UIF 変換（7.1 の self-test を足すまで CPU の変換）、性能 counter。

## 残り

- p007: Keiland の graphical login（起動時の GPU の開始の方法を含む）。
- TFU の UIF の自己試験と 7.1 の TFU の job（IOC の stride）を足すと texture の変換が速くなる（p008）。
