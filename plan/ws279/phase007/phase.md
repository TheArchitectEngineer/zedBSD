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

## cursor が 10 fps に見える件（2026-10-22 P2）

- ユーザー「Pi5での描画は、カーソルが10fpsくらいに見えます。…App Homeへの遷移のアニメはかなりスムーズなので、描画は大丈夫そう。」
- 原因は display ではなく入力の側。session.log の `KWL PERF cursor` では box_writes が 5 秒で 54〜86、`box_gap_p99_us=50000` で、presents は box_writes と同じ数。pointer の報告が 50 ms ごとにしか届いていなかった。`rp1-usb: polling 2 controllers every 50 ms` は割り込みが route 済みの時の safety poll だが、`drv_rp1_irq_establish()`（`src/drivers/platform/rpi5/rp1-irq.c`）が GIC の line を unmask していなかった（arm64 の `hal_irq_register` は line を有効にしない）。このため RP1 の割り込み（USB 31・36、Ethernet 6）は一度も来ず、USB は 50 ms の poll、Ethernet は 20 ms の poll だけで動いていた。hvs6-flip の cursor の経路（record して待たずに publish）は Pi 4 と同じ。「copy FIFO」は stage の名前に過ぎない。
- 修正: establish の最後に `kern_irq_unmask(irq)`（Q1 の許可、WS278 の file）。commit ae592a9b8・0c02104a2。
- build: `config-rpi5-gpu.mk`（main 366310c25 の fan 入り、merge 239bb232b）と Pi 4（config/ci/config-rpi4.mk）の vmunix、どちらも warning 0。
- 実機（D0、kernel = 239bb232b の config-rpi5-gpu.mk の build）: host から板への ping の rtt は avg 9.9 ms（0.5〜19 ms）から 0.15 ms に下がった。xHCI の handler の入りは 10 秒で 420（poll の 400 と IRQ）。`rpi5-fan:` の行・V3D・display node・Keiland の起動は前と同じ。ユーザーに mouse を回してもらった間（2026-10-22 20:3x）の 5 秒の窓で box_writes 282・253（前は 54〜86）、`box_gap_p99_us=23000`（前は 50000）、presents 150・138（前は 32〜85）、held 250・195、`present_gap_max_us` 30000〜37000。pointer の報告は約 18 ms ごと、present は refresh の約半分（30/s）で、残りは userland（cursor thread の hold）か HID の側。Pi 5 の display の経路ではない。

## Notes と System Monitor（2026-10-22 P2、ユーザー「System Monitorの表示が出なかったのと、Notesが起動せず落ちたようです。」）

- Notes: `notes: stencil format failed (-11)`。bcm2711 の実行器の `format_properties`（`vk/vulkan-query.c`）が depth・stencil の format を一つも出さないため。表は Pi 4 と共有なので Pi 4 でも同じで、計画済みの ws141-p017（bcm2711 の Vulkan の stencil）の範囲。ここでは直していない（Q1 の判断待ち）。
- System Monitor: 窓は出て frame も出る（ZMON FRAME fps=19.9）が中身が無い。`plan/ws279/tests/vkmon-pi.c`（monitor の draw.vert と shape.frag・glyph.frag を 64x64 に描いて読み戻す、`--vert=`・`--frag=` で SPIR-V を差し替える）で切り分けた: 形は描かれ（赤の固定の frag で覆う）、attribute の 0・1（corner・shape）は届くが、2・3（color・second）が 0。8 成分目より先の attribute が 0。
- 原因: 7.1 の shader state record（`native71-shader.c`）が input の VPM segment の大きさを 1 にしていた。4.2 は separate の flag があるので shared でも 1 だが、7.1 には flag が無く、0 でない大きさは 1 sector（8 行）の separate の input segment になる。Mesa の 7.1（v3dvx_pipeline.c、vir.c）は input の大きさ 0、output を両方の大きい方にする。直し: byte 5・7 を 0（commit 24674b2f4）。`native71-state-check.py` の期待値も 0 に。
- 確認: `sh plan/ws279/tests/v3d71-host-test.sh` 全 PASS。build: Pi 5 GPU（`config-rpi5-gpu.mk`）・Pi 4 とも warning 0（Pi 4 は native71 を compile しない）。実機（D0、24674b2f4 の build）: `VKMON ok … shape`・`VKMON ok … glyph`（直す前は 4 cell とも clear の青）、`VKQUAD ok … pixels=4096`、`rpi5-fan:` の行・V3D ready・display node は前と同じ。
- 未実施: System Monitor の窓の目視（ユーザー）。
