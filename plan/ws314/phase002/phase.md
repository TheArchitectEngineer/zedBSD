<!-- awesome-plan project=zedbsd record=ws314-p002 -->

# ws314-p002 Keiland のプログラミングの解説（英語）

- 親: [WS314](../ws.md)。Queue: q1690 / q1690-i01（担当 P2、2026-10-30）。
- Status: cleared（2026-10-30。画面での実行は未実施）
- Disposition: normal

## 目的・受け入れ

- ユーザーの決定 8（ws.md）: `docs/` に Keiland のプログラミングの解説を英語で。章の順は app の作成 → window の作成 → titlebar の作成 →
  ui の作成と main loop → widget の作成 → menu の作成 → その他の機能。人間が読んで分かる構成と短い完全な code の例。
- kl_ui の immediate mode のモデルを正確に（widget は frame ごとの関数の呼び出し、状態は app の構造体、id と index、入力は届いた時に前の frame の
  hit の記録で振り分け、結果は次の frame の戻り値と kl_ui_take、frame を作る契機と damage・KL_PACING_FRAME、窓の枠は宣言型で retained）。
- fd の API（`kl_app_watch_fd`・`kl_instance_fd`・`KL_APP_FD` の event）は書かない。docs/ から plan/ へ link しない。

## 成果

- [docs/howto/keiland-programming.md](../../../docs/howto/keiland-programming.md)（how-to の分類: docs/style.md の「procedures for a user's goal」、
  build と実行の観察と失敗の時の見方を含む）。[docs/howto/README.md](../../../docs/howto/README.md) から link。
- 章: 1 The application（`kl_app_open`・queue・`KL_APP_WINDOW`・`KL_APP_THEME`）、2 The window（options・present の方式・frame と canvas・
  `EAGAIN`・入力の種類）、3 The titlebar（control の table・`KL_WINDOW_ACTION`・action の状態・search・tab、宣言型で retained）、
  4 The user interface and the main loop（immediate mode の 5 つの規則、frame、完全な program `counter.c`、frame の pace、damage と
  `kl_ui_take_frame`・`kl_window_present_parts`）、5 The widgets（表と field・list の例、focus、`kl_ui_hit`、scroll）、
  6 Menus（`kl_window_set_menu` の table、role・shortcut・状態、context menu）、7 Other services（appearance と theme、glass、翻訳、clipboard、
  file chooser、通知、settings と system、音）。
- 書かなかった物: fd の API（指示どおり）、`kl_instance_*`（今の API は fd を通すので p009 で新しい API とともに）、`kl_system_media_*`（fd を返す）。

## 確かめ

- 例の code（16 の block）を、p001 の header（comment の掃除の後）に対して host の clang 19（`-std=gnu17 -Wall -Wextra -Werror`）で compile:
  完全な program（最初の例、`counter.c`）はそのまま、断片は使う名前を宣言した harness の関数・file scope に入れて。全部が通った
  （断片の harness では未使用の変数・引数の警告だけを外した）。
- `counter.c` を p001 の Linux の build（build/ws314-p001-linux-after/lib/libkeiland.so）に link して実行形式ができた（host の cc、`-Werror`）。
  compositor の上での実行（窓が出て数が変わること）は未実施。
- 例の code の抜き出しと harness は scratchpad に置き commit しない（手順: 各 ```c の block を file にし、完全な program は直に、断片は関数の中に）。

## 残り

- p009: post・process・instance の新しい API の章を足す。
