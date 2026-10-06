# ws090-p019: 慣性 scroll を libkeiland に一本化する（全ての app）

Status: planned（2026-10-06 Q1 が作成）
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
