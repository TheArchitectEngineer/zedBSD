<!-- awesome-plan project=zedbsd record=ws155-p004 -->

# ws155-p004: system bar の時計から Calendar を開く

Status: in-progress（実装・build 済み。QEMU は T1）
Disposition: normal
Parent: [WS155](../ws.md)
Queue: q831（2026-10-07、P2）
依存: [p001](../phase001/phase.md)

## 実装（2026-10-07、P2）

ユーザー（2026-10-04 夜）「画面右上通知領域の日付・時刻をクリックすると、カレンダーappが表示されるようにします。」

- `wayland/home.c`: `kwl_home_open_app(server, name, via)`（App Home の list を読み、名前の app を `home_launch` で起動、動いていれば前に出す。App Home の icon から育たない）。`kwl.h` に宣言。
- `wayland/shell.c` の `bar_press`: 時計の pill の click で `kwl_home_open_app(server, "Calendar", "clock")`（docked の窓が無くても）。log `KWL HOME open name=Calendar via=clock`。

## 確かめ

- build: zedBSD の `bin/wayland` warning 0、`keiland-os-boundary` PASS、style-check（home.c・shell.c の変えた所）0。
- host の試験は無し（compositor の press の経路は QEMU で）。AAT `apps.calendar.event` の 3 段。
- 未実施: QEMU（T1）。
