<!-- awesome-plan project=zedbsd record=ws155-p006 -->

# ws155-p006: 月の表示の予定の double click（double tap）で編集を開く・日の空き地の click で新規作成

Status: cleared（2026-10-13 ユーザー「UATは終わっています。OKでした。」。旧: in-progress（2026-10-13 Q1: T1-561（QEMU の AAT）PASS、残りは実機の UAT。旧: T1 の AAT `apps.calendar.event` の 3・4 段目。Q1 が結果を判定するまで cleared にしない）
Disposition: normal
Parent: [WS155](../ws.md)
Queue: Q1 の依頼（2026-10-13、P1）
依存: [p003](../phase003/phase.md)（編集の画面・保存・削除）

## ユーザーの指摘（2026-10-13 UAT、原文）

「・Calenderで追加した予定を編集する方法がないように見えます。予定をダブルクリックで編集可能にしてほしいです。」

補足（2026-10-13、Q1 経由の原文）:「イベントが編集できないのは、月表示だけですね。」

追加の指摘（2026-10-13 UAT、Q1 経由の原文）:「・Calendarの右ペインはディスプレイが小さかったりウィンドウが小さいと表示できません。そこで、月・週・日の表示では日の領域の何もないところをクリックすると新規イベント作成にしたいです。」

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

## 追加: 日の予定の無い所の click で新規作成（2026-10-13、P1）

### 調べたこと

- 編集の画面（`view_editor`）は右の panel の中にしか描かれず、窓の幅が `CAL_NO_PANEL`（1020）未満で panel が消えると、drop も含めて新規作成の画面が見えなかった（ユーザーの指摘の前提）。
- 週・日の表示（`view_list`）は日ごとの列に項目の行を並べた一覧で、時刻の軸（時間の格子）が無い。

### 設計と判断

- 月の表示（`view_cell`）: cell の hit が click の時、`view_pill_at` が点の下を「予定の pill」「"+N more"」「どちらでもない」に分ける（p006 の `view_pill_open` を、判定だけを返す形にした）。どちらでもない（cell の地・日の数字）の 1 回の click か tap で `view_blank` が日を選び（今どおり）、drop と同じ `view_new` で新規作成の画面を開く。
  - double click（double tap）: 予定の pill の上なら 2 回目で `view_edit`（p006 のまま。1 回目は日を選ぶだけ）。**判断**: 予定の外の double click は 1 回目が新規作成を開き、2 回目は何もしない（同じ新規作成のまま、`EDIT new` を 2 度出さない）。
  - "+N more" の上・前後の月の灰色の日・月の見出し・曜日の行は対象外（見出しと曜日の行は cell の外で hit が無い）。
- 週・日の表示（`view_list`）: 各日の列の名前の行より下（列の上から 34 px 以下）に `CAL_ID_DAY_BLANK` の hit を、項目の行の hit より先に記録する（後に記録した行が上で勝つ）。行の上の 1 click は今どおり編集、行の外の 1 click で `view_blank`（2 回目の click は無視）。
  - **判断（時刻）**: 週・日の表示には時刻の軸が無いので、位置から時刻を取らず、drop と同じ 09:00〜10:00 にする（30 分・1 時間の丸めは時刻の軸ができた時に決める）。
- **判断（calendar）**: 新規作成の calendar は sidebar で表示中の最初の物（Work→Personal→Study→Family の順、全部隠れていれば Work）。題はその「New … Event」。
- **判断（panel の無い窓）**: panel が無い時（幅 1020 未満）は、編集の画面を months の card の右上（bar の下、panel と同じ幅 300、狭ければ months の card の幅まで）に不透明の card として重ねる（`view_editor_over`）。開いている間は months の card 全体を `CAL_ID_EDIT_VEIL` の hit で覆い、下の日の click を取らない（閉じるのは Cancel・Save・Delete）。glass の上でも読めるよう card は不透明（`CAL_COLOR_SURFACE`）。この重ねる表示が無いと、狭い窓では click で新規作成を開いても見えないため、指摘の目的に要ると判断した。
- 編集の画面を開いている間に別の日の空き地を click すると（panel のある窓）、編集中の内容は捨てて新規作成に置き換わる（drop と同じ）。
- 新しい log: `CALENDAR BLANK date=YYYY-MM-DD mode=Month|Week|Day`（`EDIT new list=…` の前）。

### 変えた path

- `userland/desktop/calendar/view.c`（`view_pill_at`・`view_blank`・`view_new`・`view_editor_over`、`view_cell`・`view_list`・`view_drop`・`cal_view_draw`、冒頭の注釈）
- `plan/ws155/tests/host-calendar.c`（blank-*・week-row*・narrow-* の試験、`test_has`・`test_absent`・`test_narrow`、狭い窓の幅）
- `tests/scenarios/apps/calendar/event.md`（4 段目: 今日の cell の pill の下の click で新規作成、Cancel。旧 4 段目は 5 段目）、`plan/tools/aat/scenarios/helpers_calendar.py`（その自動の段）
- `tests/scenarios/apps/calendar/navigate.md`・`plan/tools/aat/scenarios/helpers_apps.py`（`apps.calendar.navigate` の窓の真ん中の click が空き地に当たって新規作成が開いたら panel の Cancel で閉じる）

### 確かめ（2026-10-13、P1）

- host: `sh plan/ws155/tests/run-host-calendar.sh build/ws155-p006b/host-calendar` → PASS 37、FAIL 0（前の 19 に、月の空き地の click（blank-month・blank-new・blank-cancel）、pill の外の double click（blank-double・pill-none・blank-double-cancel）、狭い窓 900 px（narrow-blank・narrow-new・narrow-veil＝重ねた画面の左の日の click が通らない・narrow-save＝27 日に 09:00〜10:00 で保存）、週の行の click が新規作成にならず編集（week-row・week-row-only・week-row-cancel）、週・日の空き地（blank-week・blank-week-new＝Work を隠した後なので Personal・blank-day・…）を足した。select・drop・pill-* の既存の段はそのまま PASS）。絵 `build/ws155-p006b/host-calendar-narrow-editor.png`（狭い窓、months の上の「New Event」の card）・`host-calendar-week-new.png`（週の 7 日の新規作成）を目で見た。
- build warning 0（`-Werror`、rc=0）: amd64 `make -j16 ZEDBSD_CONFIG=plan/ws155/tests/config-amd64-calendar.mk BUILD=build/ws155-p006b-zed build/ws155-p006b-zed/bin/calendar`、rpi4 `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/ws155-p006b-rpi4 build/ws155-p006b-rpi4/bin/calendar`（log `build/ws155-p006b/rpi4.log`、warning 0）。
- style-check（view.c・host-calendar.c）: 新しい指摘 0（既存の 3 件のみ）。`git diff --check` OK。`check-scenarios.py` PASS（152）。
- 未実施: QEMU（T1 に AAT を依頼）、実機。

### T1 への依頼（追記、T1-561 の `apps.calendar.event` に足す）

- 4 段目の合格: 今日の cell の pill の下の click で `CALENDAR BLANK date=<今日> mode=Month` と `CALENDAR EDIT new list=Work`、撮影 `blank-new` に panel の「New Event」の form（New Work Event、09:00〜10:00）、Cancel で `CALENDAR EDIT cancel`。3 段目（pill の double click）で `CALENDAR BLANK` が出ないこと（1 回目の click が pill の上なので新規作成を開かない）。
- `apps.calendar.navigate` も同じ image で流せるなら: 合格は従来どおり（SELECT・FLIP・T）、窓の真ん中の click が空き地なら `EDIT new` の後に `EDIT cancel`。
- 実機で見る点: 狭い窓（幅 1020 未満、または小さい画面）で日の空き地の click・tap で months の上に編集の card が出て Save・Cancel が押せること。touch の 1 tap で新規作成が開き、pill の double tap は編集を開く（1 回目の tap で新規作成が開かない）こと。週・日の表示の列の空き地の tap。
