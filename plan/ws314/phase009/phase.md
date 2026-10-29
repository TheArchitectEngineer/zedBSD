<!-- awesome-plan project=zedbsd record=ws314-p009 -->

# ws314-p009 解説に新しい API（post・process・instance・launch・language）の章を足す

- 親: [WS314](../ws.md)。設計: [design.md](../design.md) §3〜§6・§13。Queue: q1690（前半は担当 P6、2026-10-30）。
- Status: in-progress（前半の構成と例は済み。後半の文章の書き直しは Sonnet）
- Disposition: normal
- ユーザーの決定（ws.md の原文）: 8「…fdの削除後、作成されたAPIに関する説明を追記します。」、11（完全に動く source の全体を示し、
  帰納と演繹で読ませる。構成は Opus、文章は Sonnet）、12・13（`KL_APP_LANGUAGE` と言語の名前）。

## 目的・受け入れ

- 前半（Opus、この記録の「構成」と「確かめ」）: 新しい章と節を設計し、章ごとに完全に動く例を `docs/howto/keiland-examples/` に足し、
  `docs/howto/keiland-programming.md` に例の全文（`<!-- example: ... -->` の印）と書き直しの指示（`<!-- WRITE: ... -->`）を入れる。
  既存の文章は変えない（目次・章のつなぎ・古くなった文に WRITE を足すのは可）。host（compile・link・文書と file の一致）と
  QEMU（zedBSD の guest で各例を動かし画面を撮る）で確かめる。
- 後半（Sonnet）: WRITE に沿って文章を書き、WRITE を消す。

## 構成（前半の設計）

- 章は 7 章の後に 3 つ（決定 8 の「追記」）。各章は p011 の型（例が何をするかと画面 → 例の全文 → 行の link と引用で読む → 規則 → 落とし穴
  と次の章へ）。2 本目の例は 4 章の 04-counter-parts.c と同じく「###」の節。例はどれも 04-counter.c の形（struct・resize・draw・frame・
  input・dirty など）から育て、新しい所だけを説明させる。thread は C11 の `<threads.h>`（zedBSD の libc にある。UNIX の呼び出しを例に
  出さない）。

  | file | 章 | 何をするか | 確かめる API |
  | --- | --- | --- | --- |
  | 08-worker.c | 8 Work on other threads | 2,000 万未満の素数を worker の thread で数える。進みは `kl_window_post`（malloc の報告、受け手が free）、結果は `kl_app_post` で record を返す。Stop は atomic の flag。窓を閉じる時は stop・join・dispatch(0) で残りの post を回収 | `kl_app_post`・`kl_window_post`、`KL_WINDOW_POST`・`KL_APP_POST`（閉じた窓宛ては window NULL） |
  | 08-language.c | 8 の後半 When only the latest answer counts | `KL_APP_LANGUAGE` ごとに世代を進め、reader の thread（mtx・cnd）が `kl_tr_read`、`kl_app_post` で返し、最新の世代だけ `kl_tr_use`、古いのは `kl_tr_discard`。Switch Language は `kl_settings_set_int("ui.language")` で desktop に頼み、変化は event で戻る | `KL_APP_LANGUAGE`・`kl_app_language`・`kl_tr_read/use/discard`（design §13.3 の別 thread の形、決定 13） |
  | 09-sort.c | 9 Other programs | `sort` を INPUT・OUTPUT・ERROR で起こし、単語を書き（短い書きは READY_INPUT で続き）、`close_input`、行で読む（LINE_CUT の続きは捨てる）、stderr、終了の code。Reverse（-r）と Bad Option（stderr と code 2 を見せる） | `kl_process_start/write/close_input/read_line/exit/close` |
  | 09-shell.c | 9 の後半 A program in a terminal | `/bin/sh` を `KL_PROCESS_TERMINAL`（TERM=dumb・PS1）で起こし、byte で読み、制御列を捨てて行に、field の行を書く。窓の大きさで `kl_process_resize`、出力の後に `kl_process_terminal_mode` で echo 無し・行の入力なら field を伏せる。閉じる時は HANGUP | `KL_PROCESS_TERMINAL`・`kl_process_read/resize/terminal_mode/kill` |
  | 10-instance.c | 10 Running once and starting programs | `kl_instance_open` を接続の前に（3 つの結果）、`kl_instance_bind`、`KL_APP_INSTANCE` で `kl_instance_take` を 0 まで、token で `kl_activate`。Start Another は `kl_launch`（argv[0] と要求、from=窓）で自分を起こし、要求が event で戻る | `kl_instance_*`・`KL_APP_INSTANCE`・`kl_launch`・`kl_activate` |

- 7 章（07-services.c）: 翻訳を `kl_settings_open`＋`kl_tr_follow`＋`kl_settings_dispatch` から、`kl_app_language` と `KL_APP_LANGUAGE` の
  `kl_tr_open`（同じ thread の形、design §13.3）に直した。7 章の行の link は差分の対応で機械的に付け替えた（文章は変えていない）。
  「Text in the user's language」の節に書き直しの WRITE（古い引用と存在しない行 157-158 の link を消す、`kl_tr_follow` は kl_app を
  使わない program 向けと一文で、catalog の形式と置き場所）。「The desktop's settings and system」に ui.language と KL_APP_LANGUAGE の
  関係の WRITE。
- 日本語の catalog: `docs/howto/keiland-examples/locale/ja/services.tr`・`language.tr`（文書に ```text の block で全文）。
- 古くなった既存の文（「全ての呼び出しは開いた 1 つの thread から」が 2 か所、1 章の event の kind の表、1 章の落とし穴、目次、例の表、
  build の注意（FreeBSD の -lstdthreads、10-instance の XDG_RUNTIME_DIR））に WRITE を足した。文書の先頭に p009 の全体の WRITE。

## 確かめ

- host（`sh plan/ws314/tests/doc-examples.sh build/p009-linux/lib`、worktree の Linux build
  `make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/p009-linux build/p009-linux/lib/libkeiland.so`、warning 0）:
  13 本の例を clang・gcc で `-std=gnu17 -Wall -Wextra -Werror`、libkeiland.so に link、catalog 2 つ（`kl_tr_open_directory` で
  全ての msg・plural が訳になる。誤った訳を渡すと FAIL することも確かめた）、文書の block と file の一致 15、marker 15。
  結果は下の「結果」。
- QEMU: 下の「結果」。

## 結果

（記入中）

## 残り

- 後半（Sonnet）: WRITE に沿って文章を書き、WRITE を消す。画面の PNG（`docs/howto/keiland-examples/NN-*.png`）を各章に。
