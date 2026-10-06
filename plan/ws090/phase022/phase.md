# ws090-p022: 全ての文字の入力で IME を受け付ける、自前の text box を libkeiland の部品へ

Status: planned（2026-10-06 Q1 が作成）
WS: [WS090](../ws.md)
Related: [WS095](../../ws095/ws.md)（IME）

## 出典

2026-10-06 ユーザー:「テキスト入力のあるすべてのアプリで、IMEを受け付けることをチェックしてください。また、テキストボックスはlibkeilandのUI要素で実現していると理解していますが、逸脱して自前で用意しているKeilandアプリがあれば教えてください。特に理由がなければlibkeilandのUIパーツを使ってほしいです。」
Q1 が source を調べた（読むだけ、2026-10-06）。IME を受けるのは libkeiland の `kl_field`（ただし app が `kl_ui_text`・`kl_ui_text_wanted`・`kl_window_text_input` を自分で結線した時だけ、`kl_app` は結線しない）と、自前で扱う Text Editor の本文・Terminal、compositor の title bar の欄（検索・breadcrumb）。

## 直す所（秘密の欄は IME 無しが正しいので除く）

1. `kl_app` が `kl_field` の IME（text の event の配達、`kl_ui_text_wanted` で text input の on・off と caret）を自動で結線する。→ file chooser（場所・Save As の名前、全 app）、Text Editor の置換の dialog が直る。
2. 複数行の text の部品を libkeiland に足し（IME・caret・選択）、Mailer の本文と Calendar の日の memo を置き換える（今は ASCII だけ・IME 無し）。
3. Files の改名の欄（自前の `fm_field`）を `kl_field` に。
4. Settings の `se_field`（自前、ASCII だけ）を `kl_field` に（秘密の欄は `kl_field` の秘密の mode で IME 無し）。管理の新しい利用者の名前・氏名で日本語を打てるように。
5. Terminal の検索の欄の preedit と候補の位置を検索の欄に。
6. Browser の web の form（input・textarea）に text input を結線（libbrowser の shell）。
7. App Home の検索で IME を受ける（今は compositor が意図して外している。日本語の app 名・翻訳の後に要る）。
- 秘密の欄（Wi-Fi の鍵・password・PIN・PDF の password・greeter・lock）は IME 無しのまま。
- 試験: host の試験と、T1 で各欄に日本語を打つ guest の試験（ime-probe 系）。
