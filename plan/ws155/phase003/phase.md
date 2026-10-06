<!-- awesome-plan project=zedbsd record=ws155-p003 -->

# ws155-p003: Calendar の app（保存・編集・週と日・通知）

Status: in-progress（実装・host の試験・build 済み。QEMU は T1）
Disposition: normal
Parent: [WS155](../ws.md)
Queue: q831（2026-10-07、P2）
依存: [p002](../phase002/phase.md)

## 実装（2026-10-07、P2）

設計は [p001](../phase001/phase.md) §2。

- `calendar.h`: 試験の data の型（`cal_event`・`cal_added`・`cal_memo`）を store の `cal_item` に。view に表示の mode（Month・Week・Day）と編集の状態（item、題・開始・終わりの field）。
- `view.c`: 日の項目は store から（終日が先、開始の順、メモは後）。種類の drop で編集の form を panel の上に（題・All day の switch・From/To・calendar の 4 つ・Save・Cancel、保存済みは Delete）。日の card・週・日の一覧の項目の click で編集。Month/Week/Day の segmented（click の frame で切り替わるよう、hit を先に）。Week は選んだ日の週の 7 列、Day は 1 列。app のメモは変わるたびに `cal_store_set_memo`、日付への drop は `cal_store_add_memo`。「not in the mock」の notice は「not in this version yet」に（Settings・Add Calendar・Custom・…）。今日の cell の場所を log（`CALENDAR CELL today …`、AAT が drop に使う）。
- `main.c`: 起動で `~/Documents/Calendar` を開く、今日の時刻のある予定の開始の分に `kl_app_notify`（`CALENDAR REMIND`）。
- p000 の試験の data（`data.c`）は program から外して `plan/ws155/tests/host-calendar-data.c` へ（試験の folder の store に入れる関数）。

## 確かめ

- host: `sh plan/ws155/tests/run-host-calendar.sh` → PASS 13（p000 の 10 に、drop で編集が開き Save で 22 日 09:00〜10:00 の Work の予定が保存、Week・Day・Month の切り替えを足した。絵 `build/ws155/host-calendar-editor.png`・`-memo.png`・`-week.png`・`-day.png` を目で見た）。
- build: `make -j16 ZEDBSD_CONFIG=plan/ws155/tests/config-amd64-calendar.mk BUILD=build/ws155-zed build/ws155-zed/bin/calendar` warning 0。style-check（calendar の全 file）0。
- AAT: `tests/scenarios/apps/calendar/event.md`（新規、p004 と一緒）、helper `plan/tools/aat/scenarios/helpers_calendar.py`。`check-scenarios.py` PASS。
- 未実施: QEMU（T1）。

## 積み残し

[WS177 backlog-p2](../../ws177/backlog-p2.md) の WS155 p003 の行。
