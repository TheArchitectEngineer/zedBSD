<!-- awesome-plan project=zedbsd record=ws155-p002 -->

# ws155-p002: 予定とメモの保存（iCalendar）

Status: cleared（2026-10-07 Q1 の判定: T1-297 の AAT（needs-person）を Q1 が PNG で目視: Calendar の EVENT saved・時計の click の log と保存の PNG（.ics の中身は未確認））（旧: test-wait（T1-297））
Disposition: normal
Parent: [WS155](../ws.md)
Queue: q831（2026-10-07、P2）
依存: [p001](../phase001/phase.md)

## 実装（2026-10-07、P2）

`userland/desktop/calendar/store.c`（新規）。設計は [p001](../phase001/phase.md) §1。

- `cal_store_open`（4 つの calendar の folder と Memos と memo.txt を読む）・`_close`・`cal_items`・`cal_store_save_event`（新しい物は UID を作る、calendar が変われば旧い folder の file を消す）・`cal_store_delete`・`cal_store_add_memo`・`cal_store_memo`・`cal_store_set_memo`・`cal_list_name`・`cal_list_color`。
- 書き: 75 byte で折り返し（UTF-8 の文字の途中で切らない）、SUMMARY・DESCRIPTION の `\\`・`;`・`,`・改行の escape。読み: 折り返しを戻す、escape を戻す、DTSTART・DTEND の `VALUE=DATE`・時刻・UTC の `Z`。

## 確かめ

- host: `sh plan/ws155/tests/run-host-calendar-store.sh` → PASS 15（空の store、時刻の予定・終日の予定・日付のメモ・app のメモ、他の program の .ics（折り返し・UTC・escape）、開き直し、calendar の移動で file が移る、削除で file が消える）。ASan・UBSan。
- style-check 0。

## 積み残し

[WS177 backlog-p2](../../ws177/backlog-p2.md) の WS155 p002 の行。
