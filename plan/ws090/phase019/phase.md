# ws090-p019: 慣性 scroll を libkeiland に一本化する（全ての app）

Status: in-progress（q806、P1、2026-10-06）
WS: [WS090](../ws.md)
Related: [BUG-211](../../bugs/BUG-211.md)・[ws090-p017](../phase017/phase.md)

## 出典

2026-10-06 ユーザー:「慣性スクロールはlibkeilandに実装してほしいのですが、Settingsも含め、各appで独自実装してしまっていませんか？」
Q1 の確認（main 065e75461）: libkeiland の慣性（`kl_scroll_axis`・`kl_ui_axis`・`kl_scroller`）を使うのは Phone・Mailer・Calendar・chooser だけ。Settings は速度の計測に `kl_axis_track` を使うが減速と停止は `settings/ui.c` の `ui_kinetic_step`（時定数 325 ms）で自前。Files・Terminal・Browser の shell・PDF Viewer・Image Viewer・Notes はそれぞれの `touch.c` にタッチの慣性を自前で持ち、libkeiland を呼ばない。Text Editor は wheel の滑りを自前で持つ。

## 範囲

- libkeiland に慣性の共通の部品（指・wheel・touch の速度の計測、減速、端での停止（と必要なら端の伸び）、時刻は compositor の時刻）を 1 つにまとめ、どの app も同じ手触りにする。p017 の `kl_scroller`・`kl_axis_track` を土台に、touch の drag の慣性も同じ部品で扱えるようにする。
- Settings の `ui_kinetic_step` と、Files・Terminal・Browser・PDF Viewer・Image Viewer・Notes・Text Editor の自前の慣性を、その部品の呼び出しに置き換える（app に慣性の式・定数を残さない）。zoom・pinch・page の送りなど慣性以外の touch の処理は app に残してよい。
- 試験: libkeiland の host 試験（速度・減速・停止）、各 app の既存の touch・scroll の host と guest の試験の回帰、kinetic-guest.sh。QEMU は T1。

## 受け入れ

- `grep` で app の source に慣性の減速の式・定数が無い（libkeiland の部品だけ）。
- 各 app の touch・2 本指の scroll で慣性が同じ部品から出る（log）。既存の試験が PASS。

## q806（P1、2026-10-06）

### 調べた事実（source の照合）

- 自前の**減速の式**を持つのは Settings だけ（`settings/ui.c` の `ui_kinetic_step`: 純粋な指数、時定数 325 ms、20 px/s で停止、端で即停止、Y だけ）。
- Files・Terminal・Browser の shell・PDF Viewer・Image Viewer・Notes の `touch.c` は、どれも libkeiland の `kl_scroller`（減速・rubber band・spring）と `kl_gesture`（速度）の
  同じ糊のコピーで、式も定数も libkeiland のもの。範囲は touch screen だけ。Text Editor は `kl_scroll`＋`kl_ui`（touch の fling は libkeiland）。
- 本当の欠け: **touch pad の 2 本指の慣性**は Settings（自前）と `kl_ui_axis` の app（Phone・Mailer・Calendar・chooser）にしか無く、Files・Terminal・Browser・PDF・
  Image・Notes・Text Editor は axis の source と axis_stop を捨てている。

### 段 1: libkeiland と Settings（実装済み）

- libkeiland（KL_VERSION 41）: `kl_scroller` を 1 つの慣性の部品にした。touch pad の指: `kl_scroller_axis(s, dx, dy, event_us, now_us)`（初めの動きで press・飛行を捕まえる、
  動きは drag、速度は compositor の時刻で `kl_axis_track` に）、`kl_scroller_axis_stop(s, event_us, now_us, &vx, &vy)`（速度で fling、1 を返す）、`kl_scroller_axis_holding`。
  `kl_scroller_release` は fling の時 1 を返す。`kl_axis_track` の実装は `libkeiland/scroll.c` に移した（`ui/axis-track.c` は build から外し、file の削除は Q1 に依頼）。
  `kl_scroll_axis`・`_axis_stop` は scroller に委ね、`struct kl_scroll` から axis の欄を除いた。
- Settings: `se_kinetic` を「指が持つ pane・飛んでいるか・`kl_scroller`」に。`ui_kinetic_step` は scroller の位置を pane に置くだけ（端を越えたら端で止める）。
  `UI_KINETIC_TAU_MS`・`UI_KINETIC_SLOWEST` と指数の式を削除。手触りは他の app と同じ（tau 0.45 s＋摩擦 300 px/s²、最低 300 px/s）。log `KINETIC hold|start|none|stop` は保つ。
- 試験: `plan/ws090/tests/host-input.c` に scroller の素の touch pad の試験（compositor の時刻が 5 s ずれても step の時計で飛ぶ、速度 3000 px/s、飛行を捕まえる、休んだ指は投げない）。
  host-input 78/78、ws081 の host-scroll 88、host-widgets 94/94、host-core 53/53、host-inset・phone・calendar PASS、host-chooser 85/85、ws089 host-build、
  zedBSD の libkeiland.so・settings、keiland-linux（warning 0）。FreeBSD の build は未実施。
