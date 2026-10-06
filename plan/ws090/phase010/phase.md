<!-- awesome-plan project=zedbsd record=ws090-p010 -->

# ws090-p010: Files の欄を libkeiland の部品へ（名前の変更を kl_field に）

Status: planned（2026-10-06 P1 が作成、q817）
Disposition: normal
Parent: [WS090](../ws.md)、設計 [design.md](../design.md) §10
Queue: q817
依存: p009（Files の描画が `kl_canvas`）。窓は WS131 p020 で `kl_app` に移り済み（design.md の「窓の土台を kui_window に」は済み）
所有 path: `userland/desktop/files/`、`plan/ws090/`、Files の host 試験

## 範囲

- Files の名前の変更の欄（grid・list・desktop、自前の `fm_field`）を libkeiland の `kl_field` に。Files は自前の hit と入力の model を持つので、欄の間だけ
  `kl_ui` を 1 つ持ち、窓の input を `kl_ui_window_input` で渡し、frame の後に `kl_ui_window_text` で text input と caret を伝える。q816 の暫定の IME
  （`fm_field_text_input`・`main_text_input`・preedit の描画）を除く。
- 場所と検索の欄は compositor の title bar の欄（既に IME あり）。自前の `fm_field` が残る所（場所の欄の model など）は使われ方を見て決める。
- list・sidebar・dialog・chip の部品への置き換えは範囲の外（見た目が変わる物は別の Phase、ユーザーに確かめてから）。

## 受け入れ

- 改名の欄が `kl_field`（IME・選択・caret・Enter で確定・Esc で取り消し、`/` は入らない）。暫定の IME の code が無い。
- build、host（files-render の改名、files-model）、QEMU（T1）で日本語の改名。
