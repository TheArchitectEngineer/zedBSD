<!-- awesome-plan project=zedbsd record=ws314-p011 -->

# ws314-p011 解説の構成の設計（Opus）: 章ごとの完全な例と、書き直しの指示の comment

- 親: [WS314](../ws.md)。Queue: q1691 / q1691-i01（担当 P6、2026-10-30）。
- Status: in-progress（2026-10-30）
- Disposition: normal

## 目的・受け入れ

- ユーザーの決定 11（ws.md、原文はそこ）: snippet ではなく完全に動く source の全体を毎回なるべく示す。kl_ui の章は、kl_ui のサンプルの全体を
  まず示し、その中で kl_button がどう使われているかを示し、サンプルから帰納的に kl_ui・frame・immediate mode を理解しつつ、演繹的な説明も
  参照できる構成に。構成の設計は Opus（この Phase）、文章の書き直しは Sonnet（p012）。
- この Phase は文章を書き直さない。`docs/howto/keiland-programming.md` に「ここにこう書く」の comment（`<!-- WRITE: ... -->`）を入れ、
  章ごとの完全な例（`docs/howto/keiland-examples/*.c`）を作って文書に全文を載せ、compile・link・実行で確かめる。
- 章の順はユーザーの指定（決定 8）のまま: app → window → titlebar → ui と main loop → widget → menu → その他。
  fd の API・`kl_instance_*`・`kl_system_media_*` は使わない（p009 で足す）。docs/ から plan/ へ link しない。

## 構成の設計（要約）

- 例（`docs/howto/keiland-examples/`、各 file は coding-style の全文に従う、Zlib の header 付き）:

  | file | 章 | 何をするか | 前の例から |
  | --- | --- | --- | --- |
  | 01-app.c | 1 | 接続、通知、2 秒だけ queue を読む | — |
  | 02-window.c | 2 | 窓、frame（pixel と canvas）、pointer を追う点、main loop の型（dirty・resized・closed） | 01 に窓 |
  | 03-titlebar.c | 3 | Back・Forward・検索の control、title、action の状態 | 02 に titlebar |
  | 04-counter.c | 4 | kl_ui と kl_button 3 個（Less・Reset・More）、状態は app の構造体、Up・Down は kl_ui_take | 02 に kl_ui |
  | 04-counter-parts.c | 4 の後半 | KL_PACING_MOTION・FRAME、kl_window_frame_wait、kl_ui_take_frame と present_parts | 04 に pace と部分の描画 |
  | 05-widgets.c | 5 | kl_field・kl_list・kl_switch・kl_slider・kl_button と kl_ui_hit の自前の swatch（id と index）、Tab の focus | 04 の型 |
  | 06-menu.c | 6 | 窓の menu（shortcut・radio）と context menu（右 click・長押し）、button・key・menu が同じ action | 04 に menu |
  | 07-services.c | 7 | glass、appearance、翻訳（kl_settings_dispatch を含む）、file chooser と recent、clipboard、通知 | 04 の型 |

- 各章の型（文書の先頭の WRITE comment に全体の規則）: (a) 例が何をするかと画面 → (b) 例の全文 → (c) 例を読む（帰納、
  `[lines A-B](keiland-examples/NN.c#LA-LB)` の link と引用で行を指す）→ (d) 規則（演繹、例の行への参照付き）→ (e) 落とし穴と次の章へ。
  用語は初めて出る章で一度だけ定義する（章ごとの WRITE comment に列挙）。「frame」が pixel の frame（2 章）と widget の 1 回の描画
  （4 章）の両方を指し、同じ出来事の両面であることを 4 章で明示させる。
- kl_ui の章（4 章）: 例の全体 → setup → frame を実行順に読む（kl_ui_begin → 自前の描画 → kl_button 3 個 → kl_ui_end）→ 1 回の click を
  frame N・N+1・N+2 の時系列で追う → immediate mode の規則 7 つ（widget は呼び出し、状態は program、id と index、入力は届いた時に
  前の frame の record で解決、結果は次の frame で 1 回、frame は変化の時だけ、窓の枠は宣言型）→ 落とし穴。後半に pace と部分の描画の例。
- 今の本文は各章の末尾の `<!-- OLD TEXT ... -->` の中に残した（描画されない）。再利用してよい所・持ち込まない誤りを comment に書いた。

## 今の本文（p002）の誤り（WRITE comment に記録、p012 で直す）

1. 「Drawing only what changed」と規則 3: `kl_ui_window_input` は widget の種類の input（pointer・主 button・wheel・key・指・input method）に
   常に 1 を返す（libkeiland/ui/text-input.c の kl_ui_window_input）。「明かりが変わった時だけ 1」「描き直しが要る時に 1」ではない。
   明かりだけの変化を知るには `kl_ui_pointer_motion`・`kl_ui_pointer_leave` を直に呼ぶ（04-counter-parts.c）。
2. list の snippet: double click の `KL_LIST_ACTIVATED` は `kl_list_row` の戻り値で来る（ui/list.c）。`kl_list_begin` の戻り値だけ見ると
   取りこぼす（05-widgets.c は両方を OR）。
3. 補足の不正確: 規則 5 の「kl_ui_end は widget の animation か scroll の fling の間 nonzero」は不足（指・drag・待つ key・静止した pointer の下の
   明かりの変化・scroll の glide と bar の fade、ui/ui.c の kl_ui_end）。翻訳の snippet に `kl_settings_dispatch` が無い（無いと実行中に
   言語が変わらない、keiland_settings.h・keiland_tr.h の kl_tr_follow）。

## 確かめ

- `sh plan/ws314/tests/doc-examples.sh`（host）: 8 本の例を clang 19 と gcc で `-std=gnu17 -Wall -Wextra -Werror`、worktree の Linux build
  （`make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/p011-linux build/p011-linux/lib/libkeiland.so`、warning 0）の
  libkeiland.so と link、文書の block が file と byte で一致、marker の file が在る: 2026-10-30 SUMMARY pass=40 fail=0 not-done=0。
- QEMU（zedBSD の Venus の guest）: 実行中（下に追記）。
- 実機: 対象外。

## 残り

- p012（Sonnet）: WRITE comment に沿って文章を書き直し、OLD TEXT と WRITE の comment を消す。画面の PNG を文書に入れるかは p012 で。
