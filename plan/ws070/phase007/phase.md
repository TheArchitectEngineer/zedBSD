<!-- awesome-plan project=zedbsd record=ws070p007 -->

# ws070-p007: Titlebar Presentation の設計

Phase ID: `ws070-p007`
Parent: [WS070](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示で WS071 のサブエージェントが計画・実行。main の Queue への反映は main の session）

## 範囲

[titlebar-spec.md](../titlebar-spec.md)（ユーザーの仕様案）の設計: protocol、libzdesktop の API、zdesktop の配置・縮退・描画・入力・
animation、非 ASCII、試験、後の Phase の分割。同日のユーザーの補足（zdesktop-files の窓の中の navigation bar をタイトルバーへ統合し、
fallback は持たない）を設計の受け入れに入れる。

## 受け入れ

1. 設計の文書（[titlebar-design.md](../titlebar-design.md)）が仕様案の §1〜§28 に答え、既存の System Menu（design.md）との関係を決める。
2. 人間の判断が要る点を既定と共に示し、Phase の分割と順序を ws.md（WS070・WS071）に入れる。

## 結果（2026-09-27）

cleared。

- 設計: 新しい global `zed_titlebar_manager_v1` と `zed_titlebar_v1`（mode・controls・tabs の model、menu と同じ transaction、
  event、error）、libzdesktop の `zdesktop_titlebar_*`、zdesktop の titlebar.c（model）と titlebar-shell.c（配置・縮退・描画・入力）、
  既存の浮いたタイトルバー・システムバーの zone・docking の animation の一般化、overflow の popup（隠れた control と窓の menu）、
  role の icon を atlas に rasterize、glass の UTF-8 の glyph cache、試験（titlebar-probe、Venus の場面、回帰）。
- 既存の実装との照合: 浮いたタイトルバー（44）、システムバー（34）の 5 つの zone、220 ms の docking の animation、double click の対称、
  pull で restore は WS035 p059〜p071 と WS070 p003 に既にある（shell.c・menu-shell.c を読んで確かめた）。新しいのは Presentation の
  場所の CONTROLS・TABS と、その入力・縮退。
- 判断が要る点: [titlebar-design.md](../titlebar-design.md) §13。§13-1（CONTROLS・TABS の窓の menu の置き場、既定 A: overflow の
  popup）を main 経由でユーザーに示し、2026-09-27 ユーザーが A に決めた（「CONTROLS / TABS モードのウィンドウのアプリメニューの
  置き場所は、Aの推奨でお願いします。」）。他は技術の既定。
- 分割: ws070-p008（protocol と model）→ p009（glyph cache と icon、WS035 と調整）→ p010（CONTROLS の presentation、WS035 と調整）→
  ws071-p014（zdesktop-files の toolbar → CONTROLS）→ … → ws070-p011（TABS）→ p006 → p012（規約・回帰・実機）。
- 試験: 設計の Phase なので build・実行の試験は無い（未実施ではなく対象外）。
