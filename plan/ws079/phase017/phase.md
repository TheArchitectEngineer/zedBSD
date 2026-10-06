# ws079-p017: Notes に文字を打つ text box（IME 対応）

Status: in-progress（2026-10-06 Q1 の指示で ws175-p008 の文字の段と一つの作業として P2 が実装、build warning 0 と host 試験 PASS。画面と IME は ws175-p010 の T1）
WS: [WS079](../ws.md)
Related: [ws095-p007](../../ws095/phase007/phase.md)

## 出典

ws095-p007（IME）の受け入れの「Notes に text input」について、P1 が「Notes は手書きの notebook で文字を打つ欄が無い」と報告。2026-10-06 ユーザー（クリック）「Notes に text box を作る（別の Phase）」。

## 範囲（設計で詰める）

- Notes の page の上に text box を置き、文字を打ち、移動・大きさの変更・削除ができる。IME（`kl_window_text_input`・preedit・候補）に対応する。
- 保存の形（Notes の model と PDF の書き戻し、WS175 の editor と合わせる）、手書きとの重なり、選択・編集の操作。
- まず設計（model・操作・保存の形）を書き、未決の点はユーザーへ。その後に実装と試験（host、QEMU は T1）。

## 設計（2026-10-06 P2、ws175 と合わせる）

Q1（2026-10-06）「p008 の文字の段（Notes の文字の編集と挿入の UI、IME、font の選択、Font/Size）を、q810（ws079-p017 Notes の IME 対応の text box）と
一緒に 1 つの作業として（同じ機能なので）」。この Phase の text box は [ws175-p008](../../ws175/phase008/phase.md) の文字の段の Text の道具そのもので、
設計は [ws175 の design.md](../../ws175/phase001/design.md) §3.6・§4・§6・§7 に従う。

- **model**: 頁に挿入した文字は WS175 の edit（`NOTES_EDIT_INSERTED | NOTES_EDIT_TEXT`: UTF-8 の文字、font（Sans・Mono・Japanese）、size（pt）、色、
  折り返しの幅、box の左上を頁に置く変換）。手書きの stroke とは別の list（頁の editor の物）で、stroke は常にその上に描く。
- **操作**: Text の道具で頁を click（drag で折り返しの幅）→ box に打つ（IME の preedit・候補・screen keyboard は libkeiland の `kl_ui_window_input`・
  `kl_ui_window_text`・`kl_text_area`）→ Esc か box の外の click で確定（1 つの undo）。Select の道具で選び、移動・大きさ・削除・Font・A-/A+、
  double-click か Edit で文字を直す。確定の前に頁の editor で書けるか確かめ、書けない文字は box に残して理由を出す。
- **保存の形**: WS175 の ZNOT 2.0 の EDIT chunk（文字・font・size・色・幅）と journal 版 2。PDF には増分の更新で、埋め込みの subset の TrueType
  （Type0・ToUnicode）で文字として書く（他の reader で選択・検索できる）。
- 未決の点: 無し（ws175 の D1〜D7 の決定の範囲）。

## 結果

実装と試験は [ws175-p008](../../ws175/phase008/phase.md) の「文字の段」。AAT は `apps.notes.pdf-insert-text-font`（draft、日本語の IME を含む）を
ws175-p010 で T1 が流す。ws095-p007 の受け入れの「Notes に text input」もこの AAT の結果で判定できる。
