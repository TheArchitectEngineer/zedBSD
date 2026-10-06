<!-- awesome-plan project=zedbsd record=ws155-p001 -->

# ws155-p001: Calendar の設計（まず簡単な物）

Status: cleared（2026-10-07 P2、設計だけ。正常系の範囲は q831 の規則）
Disposition: normal
Parent: [WS155](../ws.md)
Queue: q831（2026-10-07、P2）
依存: p000 の mock へのユーザーの再指示（2026-10-05、glass・メモ、反映済み）

## 範囲（Q1 ACK 2026-10-07）

月・週・日の表示、予定の追加・編集・削除、日付のメモ、app のメモの保存、app が動いている間の開始の通知、system bar の時計からの起動。範囲外（WS177 の backlog）: 繰り返しの予定、calendar の追加・設定、CalDAV・同期、app が止まっている間の通知、To-do。

## 1. 保存（p002）

`~/Documents/Calendar/`（`~/Documents/` は cloud backed とみなす）。1 つの物を 1 つの file に（同期の衝突に強い形、Phone と同じ考え）。

- 予定: `<Work|Personal|Family|Study>/<UID>.ics`、iCalendar（RFC 5545）の VCALENDAR に VEVENT 1 つ: UID・DTSTAMP（UTC）・DTSTART・DTEND（終日は `VALUE=DATE` で翌日まで、時刻は zone の無い local の時刻）・SUMMARY・CATEGORIES（calendar の名前）。calendar は folder で決まる。
- 日付のメモ: `Memos/<UID>.ics` に VJOURNAL（DTSTART の日付、SUMMARY に 1 行目、DESCRIPTION に全文）。
- app のメモ: `memo.txt`（平文）。
- 書き換えは `.new` に書いて rename、削除は file を消す。他の program の .ics（折り返しの行、UTC の `Z`、`TZID` の parameter）も読む（UTC は local に、TZID は local と見なす）。

## 2. app（p003）

- 試験の data を保存の読みに置き換える（p000 の data.c は host の試験へ）。
- 種類の card を日付に drop → panel に予定の編集の form（題・All day・From/To・calendar・Save・Cancel）。日の card・週・日の一覧の項目の click で編集（Delete つき）。日付のメモは Delete と Cancel だけ。
- Month / Week / Day: Week は選んだ日の週の 7 列、Day は選んだ日の一覧（項目の click で編集）。
- app のメモは変わるたびに保存、日付への drop は VJOURNAL で保存。
- 通知: app が動いている間、今日の時刻のある予定の開始の分に `kl_app_notify`（題と「Starts at 09:00 (Work)」）。

## 3. system bar の時計（p004）

- compositor の `shell.c` の `bar_press` で時計の pill の click を見て、`home.c` の新しい `kwl_home_open_app(server, "Calendar", "clock")`（App Home の list の名前で起動、動いていれば前に出す。icon から育たない）。log `KWL HOME open name=Calendar via=clock`。
