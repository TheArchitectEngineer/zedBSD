<!-- awesome-plan project=zedbsd record=ws279-p007 -->

# ws279-p007 `drv_gpu_interface` と Keiland の graphical login

- 親: [ws279](../ws.md)、担当: P2（2026-10-22、worktree `agent/p2-pi5gpu`）
- Status: in-progress（Keiland の desktop の session が GPU で起動（sessiond の DESKTOP shown）。画面の目視と keyboard での操作の確認が残り）
- 依頼の元: 2026-10-22 Q1（ユーザーの「GPUのコードを実装していく」の続き）。依存: p005・p006（実機 PASS）、WS278 p004（USB keyboard、済み）。

## 目的と受け入れの条件

1. 起動の時に GPU（display・render の node）が出て、sessiond が Keiland の session を GPU で始める。
2. 起動を止めない: 既定は今まで通り root の `sysctl hw.gpu.start=1`。boot parameter `rpi5gpu.start=boot` の時だけ起動の途中（user space の前）に start する。失敗は log に出して起動を続ける。
3. 実機: Keiland の desktop が monitor に出て、keyboard で操作できる（目視はユーザー）。

## 作った物

- `attach71.c`: `rpi5gpu.start=boot` で attach の時に start（display の P1〜R3・V3D の V1〜V9・render の node）。既定は保持。
- display・render の node は p005・p006 の物。Keiland 側（libvulkan・compositor・sessiond）は変えていない。device の名前は engine の識別の版で選ぶ（`vk/vulkan-query.c`、`#if` を外した。Q1 の指摘、2026-10-22）。shader state record の大きさは board の encoder が返す（`bcm2711_native_shader_bytes()`、`native-state.h` の `#if` を外した）。

## 確認（実機、2026-10-22 P2、kei@10.0.30.5 D0、51fd54291 の build）

- `cmdline.txt` = `login=console rpi5gpu.start=boot`: `v3d: rpi5gpu.start=boot: starting at boot` → display・V3D の self-test が全部 ok、`/dev/gpu0`・`/dev/gpu1`、SSH は使えた。
- `cmdline.txt` = `login=graphical rpi5gpu.start=boot`（元は `cmdline.txt.good` に保存、`login=console`）: 約 35 秒で SSH が戻り、`/var/log/sessiond.log` に `SESSIOND SEAT path=/dev/gpu0`・`/dev/gpu1`、`SESSIOND SESSION start user=kei`、`SESSIOND HANDOFF session ready=1 waited_ms=1038`、`SESSIOND DESKTOP shown at_ms=7475`。`ps` に `/bin/wayland`・`files`・`settings`・`keiland-ime`。kernel の log に GPU の error は無い。
- 未実施: monitor の目視（ユーザーが見られる）、keyboard・mouse の操作、画面の capture（keiland-shot は試験の image にしか入らない）。

## 板の状態

- 板は `login=graphical rpi5gpu.start=boot` のまま（Keiland の desktop が出ているはず）。戻すには `sudo cp /boot/boot0/cmdline.txt.good /boot/boot0/cmdline.txt`。kernel は GPU 入りの試験の build、`/boot/boot0/vmunix.good` は元の kernel。
