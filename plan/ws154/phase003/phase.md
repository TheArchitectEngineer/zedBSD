<!-- awesome-plan project=zedbsd record=ws154-p003 -->

# ws154-p003: SKK の engine と辞書

Status: in-progress（実装と host の試験は済み。選択への組み込みは p004、判定は Q1）
Disposition: normal
Parent: [WS154](../ws.md)
Queue: Q1（2026-10-05、P2）
依存: [p001](../phase001/phase.md)（設計。D4・D5）

## 範囲

p001 の D4（SKK の engine）と D5（辞書の package）。IME の選択（p002）と、SKK の mode を言語の ID にすること（p004）は入らない。p001 の「Q1 に確かめる点」の 3（`>`・`/`・`#`・Tab・注釈を後にする）はユーザーの判断待ちで、案のとおり入れていない。

## 実装（2026-10-05、P2）

- `userland/desktop/ime/skk.h`・`skk-engine.c`: engine.h の口の SKK の engine（ID `skk`、label「あ」）。日本語の engine のローマ字（`ja-romaji.c`）・かな（`ja-kana.c`）・SKK の辞書（`ja-dict.c`）・利用者の辞書（`ja-user.c`）をそのまま使う。
  - mode: かな・カナ（`q` で切替）、英数（`l`、C-j で戻る、key は app へ）、全英（`L`、全角で入れる、C-j で戻る）。
  - ▽: 大文字で始まり、読みの途中の大文字で送り仮名（`KaKu` → 辞書の「かk」）。送り仮名の letter が全て仮名になった時に変換（`TaTte` → 立って）。`Q` で文字無しに ▽。`q` で読みをカナで確定。Enter・C-j で読みのまま確定。C-g で取り消し。Backspace で送り仮名・読みを 1 字ずつ。
  - ▼: Space で次、`x` で前。5 個目からは 7 個ずつ候補の窓に `a:` から `l:` の印で並べ、`asdfjkl` で選ぶ（Space で次の 7 個、`x` で前）。Enter・C-j で確定、続けて打つと確定してから打つ。C-g・Backspace で ▽ に戻る（送り仮名は読みに戻る、ddskk と同じ）。
  - 登録: 候補が無い・尽きた時。preedit は `[登録]読み 語`。登録の中でも変換でき（さらに登録、3 段まで）、Enter で利用者の辞書に足して確定、空の Enter・C-g で取り消し（▽ に戻る）。
  - 学習: 確定した候補を利用者の辞書の先頭へ（`ja_user_learn`）。保存は `save` と `destroy` で writer thread へ（日本語の engine と同じ）。password・PIN・hidden・sensitive の欄では学習しない。
  - Enter の扱い: 確定だけで改行は入れない（▽・▼ の時）。何も変換していない時は pending の文字を確定してから Enter を app に返す。
- `userland/desktop/ime/skk-dict/`: package `ime-dict-skk`。`userland/base/emacs/dict/` の `SKK-JISYO.X`・`SKK-JISYO.remacs` を複写して置き、別に管理する（ユーザー）。image の `/usr/share/keiland/ime/skk/`。license は Zlib（Awe Morris の著作、WS095 の D1 の relicense）。image への選択（config）と keiland-ime の requires への追加は p004。
- build: `skk-engine.c` を keiland-ime の 3 つの Makefile（zedBSD・Linux・FreeBSD）に足した（まだ呼ばれない）。Linux・FreeBSD の `keiland-*.mk` に辞書の data の 2 行。
- 見つけて直した欠陥: `ja_romaji_flush` は result を空にしない（呼ぶ側が count を 0 にする約束）。それを知らずに呼ぶと、初期化していない count で配列を越えて読んだ（gcc の最適化の build で segfault）。`flush_pending` で count を 0 にした。試験は自動変数を pattern で埋めて build するようにし、この欠陥を捕まえることを確かめた。

## 確かめ（host）

- `sh plan/ws154/tests/run-host-skk.sh`（ASan・UBSan・`-ftrivial-auto-var-init=pattern`、gcc と clang）: 24 case（かな、変換、次・前の候補、送り仮名、促音の送り仮名、続けて打つ確定、候補の窓と選択、カナの切替、▽のカナ確定、英数・全英と戻り、C-g、Backspace、注釈、Enter、登録・登録と学習・登録の取り消し、学習、password の欄で学習しない）、候補の窓の 5 個の表示、保存して新しい engine で読み直し、image の辞書（REmacs の 2 つ）で「書く」「日本語」。`host-skk: PASS`。
- build: zedBSD の `bin/keiland-ime`（warning 0）、Linux の Keiland（`make -f userland/desktop/keiland-linux.mk`、warning 0、辞書が `share/keiland/ime/skk/` に入る）。style-check は違反 0。
- 未実施: QEMU（p004）、FreeBSD の build。
