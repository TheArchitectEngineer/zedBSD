<!-- awesome-plan project=zedbsd record=ws092-p004 -->

# ws092-p004: 規約の全文との照合と回帰

Status: cleared（2026-09-29、subagent の worktree `wt/ws092`）
Disposition: normal
Parent: [WS092](../ws.md)
Queue: main の依頼（2026-09-29「次は ws092-p004（p002・p003 の source を規約の全文と照合し、boot test）」）
依存: p002・p003（cleared）

## 範囲と受け入れ

- WS092 の全 source（`userland/desktop/textedit/` の 20 の .c と .h、libkeiland に足した `chooser*.c`・`chooser.h`・`paint*.c`・`paint.h`）を
  [coding-style.md](../../coding-style.md) の全文と照合し、範囲内の違反を直す。
- build（`-Werror`、warning 0）、host 試験（editor の核、chooser）、boot test。
- 意味を変える修正は無い見込み（直すのは書き方）。変えた場合は該当の host 試験で確かめる。

## 照合の方法

1. `python3 plan/tools/style-check.py <全 .c>`（call-in-condition・blank-after-brace・paragraph-comment・nested-declaration・conditional・goto・
   forward-declaration・comment-form・name・multi-line-body）。
2. checker が見ない規則を補助の script で洗い出した（`build/ws092/review.txt`、script は scratchpad）: copyright の header と file の説明（§13）、
   型と file-scope の変数の注釈（§2）、公開と静的の関数の注釈（§3）、初期化子の中の呼び出し（§4）、式で作る真偽値（§6）、
   意味のある呼び出しの結果の直接の return（§11）、関数の最後の裸の `return error;`（§11）、条件演算子（§6）、`goto`（§14）、
   if を含む loop の波括弧と if・else の対称（§8）、分割した呼び出しの 1 行 1 引数（§8）、§10 が禁じる注釈の形。
3. 残りは読んで判断した（下の「見つけて直したもの」）。

## 見つけて直したもの

- `libkeiland/chooser-draw.c` の配置: `draw_set(...)` を分割した 7 か所が 1 行に引数を 2 つ以上残していた → 1 行 1 引数（§8）。
- `textedit/main.c`: `main_body` と `main_ui` が 1 つの注釈を共有していた → それぞれに注釈（§2）。`state->selected = end > start;` → if で作る（§6）。
- `libkeiland/chooser.c`: `chooser_kept_display` に自分の注釈（§2）。
- `textedit/text.c`・`libkeiland/paint-text.c`: glyph の cache の key に `(bold != 0)` の式 → `weight` を if で作る（§6）。
- `libkeiland/paint.c`: `return sqrt(...) + ...;` → 値を変数に置いてから返す（§11）。
- 対象外と判断したもの: 表の初期化子（keys の文字の表、menu・titlebar の項目の表、Wayland の listener の表、present の頂点）は呼び出しではない。
  `textedit/shaders.h` は `shaders/regenerate.py` が作る file（header と生成の注記あり）。複数行の注釈の中の行を拾った 2 件は誤検出。

## 結果（2026-09-29、main 4d164169 を merge した上で）

手順は `build/ws092/p004-run.sh`（worktree の build、git の外）。出力は `build/ws092/p004-run.out`。

- 規約: `python3 plan/tools/style-check.py userland/desktop/textedit/*.c userland/desktop/libkeiland/*.c` → **違反 0**。補助の走査で出た指摘は上の
  「見つけて直したもの」で全て直した（残りは誤検出の 2 件）。
- build（`make -j64 ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/amd64 build/amd64/bin/textedit build/amd64/dynamic/libkeiland.so build/amd64/bin/wayland`、`-Werror`）:
  exit 0、warning 0。
- host: `sh plan/ws092/tests/host-chooser.sh` → 75/75、`sh plan/ws092/tests/host-core.sh` → 34/34（直した glyph の key と paint の距離の書き換えを含む）。
- boot test: `sh plan/tools/guest/build-ssh-image.sh build/amd64`（exit 0。log の warning 477 行は host の perl の locale と外部 package の openssh のもので、
  WS092 の file は 0）→ `OUTPUT=build/ws092/boot-p004 plan/tools/boot-test.sh build/amd64/hdd-image.img` → **PASS**（`build/ws092/boot-p004/login.png`）。
- 未実施: この Phase の書き換えの後の QEMU の desktop での再確認（書き方だけの変更で、host 試験で確かめた）、実機。
- 限界: 補助の走査は正規表現で、§10 の「注釈が文を言い換えていないか」「全ての呼び出しに注釈があるか」は機械では判定できない。
  禁止形の注釈の grep と、p003 で書いた file の読み直しで確かめたが、p002 の約 1.3 万行は全行を読み直してはいない。
