# ws079-p017: Notes に文字を打つ text box（IME 対応）

Status: planning（2026-10-06 Q1 が作成）
WS: [WS079](../ws.md)
Related: [ws095-p007](../../ws095/phase007/phase.md)

## 出典

ws095-p007（IME）の受け入れの「Notes に text input」について、P1 が「Notes は手書きの notebook で文字を打つ欄が無い」と報告。2026-10-06 ユーザー（クリック）「Notes に text box を作る（別の Phase）」。

## 範囲（設計で詰める）

- Notes の page の上に text box を置き、文字を打ち、移動・大きさの変更・削除ができる。IME（`kl_window_text_input`・preedit・候補）に対応する。
- 保存の形（Notes の model と PDF の書き戻し、WS175 の editor と合わせる）、手書きとの重なり、選択・編集の操作。
- まず設計（model・操作・保存の形）を書き、未決の点はユーザーへ。その後に実装と試験（host、QEMU は T1）。
