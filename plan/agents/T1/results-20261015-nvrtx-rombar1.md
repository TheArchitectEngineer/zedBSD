# T1-640: WS037 R7(P8a) を rombar=1 で 1 回 2026-10-15

host awe@10.0.10.3（RTX 2070 を VFIO で QEMU guest に。実機の GPU の証拠、QEMU の guest）。image は前回の R7(P8a)（58cfb4dea、v6/R7p8）をそのまま使い、host の ~/nvrtx/v6R7p8rb1/hdd-image.img に複写して流した。
`prepare.sh --check` は ok 以外 0 行。host は run の前（直前の reboot から 1 分）に reboot 済みの状態で流し、run の後に QEMU を止めて reboot。

変えた行（~/nvrtx/start-rombar1.sh、start.sh の複写で、01:00.0 だけ）:
`-device vfio-pci,host=0000:01:00.0,bus=rp1,addr=0.0,multifunction=on,rombar=1 \`（元は `rombar=0`）。01:00.1〜.3 と emulated の `VGA,id=console` は元のまま。

## 結果: PASS（lit 1）

screendump（emulated VGA）に出た印:
- P5: fence 0x1234 499 us、0x1235 994 us、filled 64 words
- `P8 begin` → `P8 heads 4 (f), windows ff, outputs ff00`
- `P8 head 0: display 200 SOR 1 TMDS-A`
- `P8 head 0: 1280x1024 of 1688x1066, clock 108000000`
- `P8 ok: display, heads f, windows ff, lit 1`
- `P8b is not in this kernel; stopping after P8a` → `P6 ok: channel and vaspace freed` → `P7 begin: unload`・FWSEC-SB → `P7 ok: region torn down`
- 画面は scroll して先頭に `login:` の断片。guest は HLT で待機（info registers の HLT=1）。OVMF は option ROM で止まらず、固まらなかった。

(1) どちらの GOP か: emulated の VGA の screendump には段の印（kernel の nvrtx: の行）が出る。lit 1（head 0、SOR 1 TMDS-A、1280x1024 / 1688x1066 / 108 MHz = 1280x1024@60）なので、rombar=1 で RTX の option ROM が走り、RTX 側に GOP が点いた（rombar=0 の前回は lit 0）。OVMF が console にどちらを選んだかは QMP の screendump が emulated VGA だけを撮るため未確認。
(2) 値の照合: RTX の出力の monitor の実際の解像度との照合は未実施（人が見る）。
(3) `P6 ok`・`P7 ok` 完了。`login:` の行は scroll で切れて断片のみ（R7 と同じ）。

PNG（ppm から convert。QMP screendump は PPM を返す）:
- /home/awe/zedBSD-worktrees/t1/build/ws037-nvrtx/hw6/rb1-mid.png（印の全体）
- /home/awe/zedBSD-worktrees/t1/build/ws037-nvrtx/hw6/rb1-final.png（最終）
- /home/awe/zedBSD-worktrees/t1/build/ws037-nvrtx/hw6/rb1-early.png（mid と同一）

所要: 約 5 分。後始末: QEMU 停止、host reboot 済み。ローカルの QEMU は起動していない。
