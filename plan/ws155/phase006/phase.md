<!-- awesome-plan project=zedbsd record=ws155-p006 -->

# ws155-p006: 月の表示の予定の double click（double tap）で編集を開く

Status: test-wait（T1 の AAT `apps.calendar.event` の 3 段目。Q1 が結果を判定するまで cleared にしない）
Disposition: normal
Parent: [WS155](../ws.md)
Queue: Q1 の依頼（2026-10-13、P1）
依存: [p003](../phase003/phase.md)（編集の画面・保存・削除）

## ユーザーの指摘（2026-10-13 UAT、原文）

「・Calenderで追加した予定を編集する方法がないように見えます。予定をダブルクリックで編集可能にしてほしいです。」

補足（2026-10-13、Q1 経由の原文）:「イベントが編集できないのは、月表示だけですね。」

## 調べたこと

- p003 で編集の画面（題・All day・From/To・calendar・Save・Cancel、保存済みは Delete）は作ってあり、右下の日の card・Week・Day の一覧の行の click で開く（`view_edit`）。
- 月の表示の cell は cell 全体が 1 つの hit（click で日を選ぶ）で、pill に入力が無かった。これが「編集の方法が無い」の原因。
- libkeiland の `kl_ui_hit` は `KL_HIT_DOUBLE` を返す（double click も、指の double tap も。`ui.c` の 400 ms）。libkeiland の変更は要らない。

## 設計と判断

- `view.c` の `view_cell`: cell の hit が `KL_HIT_DOUBLE` なら `view_pill_open` が入力の点（`kl_ui_pointer`）の下の pill を `view_cell` と同じ並べ方（cell の上 30 から 18+3 ずつ、入る数、"+N more" の分を引く）で探し、その項目を既存の `view_edit` で開く。pill の外・"+N more" の上では何も開かない。1 回目の click は今まで通り日を選ぶ。
  - pill ごとの hit を記録しない理由: cell の描画は clip の外で早く戻る（ws210-p014 の速度の対策）ため、全ての cell で予定を引く費用を増やさない。double click の時だけ並べ方を計算する。
- 編集の画面・保存（`cal_store_save_event` の index 指定の上書き、UID と file は保つ）・取り消し・削除は p003 の物をそのまま使う。削除は既に編集の画面にあるので足さない。メモの pill は p003 の通り削除だけの画面で開く。
- 週・日・日の card の一覧（単一 click で編集）の動きは変えない（補足の指示「既存の経路を使い回し、他の表示の動きは変えない」）。
- 保存の形式は変えない。

## 変えた path

- `userland/desktop/calendar/view.c`（`view_pill_open`、`view_cell` の double の処理、冒頭の注釈）
- `plan/ws155/tests/host-calendar.c`（double click・double tap・Cancel・Save の上書き・pill の外の試験、`test_double`）
- `tests/scenarios/apps/calendar/event.md`（3 段目: pill の double click で編集、Cancel）、`plan/tools/aat/scenarios/helpers_calendar.py`（その自動の段）

## 確かめ（2026-10-13、P1）

- host: `sh plan/ws155/tests/run-host-calendar.sh build/ws155-p006/host-calendar` → PASS 19（p003 の 13 に pill-open・pill-cancel・pill-tap（指の double tap）・pill-save（`EVENT saved … date=2026-10-22 … start=540 end=600 error=0`）・pill-count（項目の数が変わらない＝上書き）・pill-none を足した）。絵 `build/ws155-p006/host-calendar-pill-editor.png`（「Edit Event」、Delete・Cancel・Save）を目で見た。
- build warning 0: amd64 `make -j16 ZEDBSD_CONFIG=plan/ws155/tests/config-amd64-calendar.mk BUILD=build/ws155-p006-zed build/ws155-p006-zed/bin/calendar`、rpi4 `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/ws155-p006-rpi4 build/ws155-p006-rpi4/bin/calendar`、`make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/ws155-p006-linux all`（成功・warning 0。ただし keiland-linux は calendar を build しない、`keiland-linux.mk` に calendar の規則が無い）。
- style-check（view.c・host-calendar.c）: 新しい指摘 0（既存の 3 件は変えていない所）。`check-scenarios.py` PASS。
- 未実施: QEMU（T1 に AAT `apps.calendar.event` を依頼）、実機。

## T1 への依頼

- image: `plan/tools/guest/test-image.sh plan/ws155/tests/config-amd64-calendar.mk BUILD`。
- 流す: AAT `apps.calendar.event`（`plan/tools/aat/scenarios/helpers_calendar.py`）。
- 合格: `CALENDAR EDIT new list=Work`、`CALENDAR EVENT saved … error=0`、3 段目の `CALENDAR EDIT open index=N memo=0` と撮影 `edit`（Edit Event の form に New Work Event・09:00〜10:00）、`CALENDAR EDIT cancel`、時計の `KWL HOME open name=Calendar via=clock`。
- 実機で見る点: touch panel で月の pill の double tap が開くこと、double click の 1 回目で日の選択（日めくりの頁）が動いても 2 回目で編集が開くこと。
