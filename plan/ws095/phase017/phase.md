<!-- awesome-plan project=zedbsd record=ws095-p017 -->

# ws095-p017: IME の辞書を /usr/share/keiland/ime へ移し、1 つの file にまとめる

Status: planning
Disposition: normal
Parent: [WS095](../ws.md)
Queue: q708

## ユーザーの指摘と決定（2026-10-04 夜〜2026-10-05）

- 指摘:「/usr/share/kei/imeというフォルダがありますが、/usr/share/keiland/imeにするべきと思いました。」
- 決定（2026-10-05）:「IME の辞書	/usr/share/keiland/ime への移動と、辞書を 1 つにまとめる。」

## 範囲

1. install の場所を `/usr/share/kei/ime/ja/` → `/usr/share/keiland/ime/ja/` に（`userland/desktop/ime/main.c:36-37` の path、`userland/desktop/ime/dict/Makefile`、`userland/desktop/keiland-linux.mk:151-152`・`keiland-freebsd.mk:152-153`、試験・文書）。
2. `SKK-JISYO.X`（REmacs の本体、約 387 KB）と `SKK-JISYO.kei`（Kei の補い、約千語）を **1 つの file** にまとめる（名前の案 `SKK-JISYO.ja`）。今の引く順（`.kei` を先に）を保つため、`.kei` の語を各見出しの候補の先頭に入れ、`.X` の候補を後ろに続ける（同じ候補は重ねない）。header には両方の出所と license（どちらも Awe Morris の著作、zlib。`.X` は REmacs の辞書を 2026-09-29 に zlib で再 license、WS095 D1・D3）を書く。まとめる script を tree に置き、まとめた結果を commit する（元の 2 file は tree から外す。履歴は git に残る）。
3. IME の load は 1 file だけを読む（補いの辞書の別の load をやめる）。利用者の辞書の場所は変えない。
4. WS154（SKK の IME）は Emacs の辞書を別に複写して持つ（ユーザーの決定、ws154）ので、この統合の影響を受けない。

## 受け入れ

- 変換の結果が統合の前と同じ（host の試験: ws095 の held-out の当たりの数が変わらない、`.kei` の語が先に出る）。3 OS の build warning 0。QEMU の IME の試験（T1）。C の全文の規約。
