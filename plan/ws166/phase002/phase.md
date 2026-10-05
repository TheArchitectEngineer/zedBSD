<!-- awesome-plan project=zedbsd record=ws166-p002 -->

# ws166-p002: 予測の候補の生成（engine の側）

Status: in-progress（前半: 索引と候補の生成、host の試験は済み。後半（composing の key と学習、engine への組み込み）はユーザーの判断（p001 の 1〜4）の後）
Disposition: normal
Parent: [WS166](../ws.md)
Queue: q737（Q1、2026-10-05）
依存: [p001](../phase001/phase.md)（D1・D2・D3・D5・D8）

## 前半の実装（2026-10-05、P2）

- `userland/desktop/ime/ja-predict.c`（`ja.h` に `struct ja_prediction`・`struct ja_predict_index`・`ja_predict_index`・`ja_predict_index_free`・`ja_predict`）:
  - 索引（D1）: 辞書の見出しのうち送り仮名の無い物を、読みの byte の順に並べた pointer の配列（UTF-8 は仮名の順を保つ）。前方一致の範囲は二分探索で出す。
  - 候補（D2）: (1) 利用者の辞書の、読みが入力で始まり入力より長い語（新しく選んだ順、各読みの候補は利用者の順）、(2) 辞書の同じ条件の見出し（読みの短い順、同じ長さは索引の順。各見出しの 1 つ目の候補を先に、2 つ目以降を後に）、(3) 読みが入力と同じ語（利用者 → 辞書）。同じ語は 1 度、最大 9（`JA_PREDICT_MAX`）。送り仮名のある読み（`かk`）は予測しない。
  - 各予測は語と読みを持つ（確定した時に読み → 語で学習するため、D5）。
  - 並べ方は `ja_predict` の 1 つの関数にまとめた（D8: WS098 で置き換えられる）。
- 3 つの Makefile（zedBSD・Linux・FreeBSD）の keiland-ime に足した（まだ engine から呼ばない）。

## 確かめ（host）

- `sh plan/ws166/tests/run-host-predict.sh`（ASan・UBSan・pattern、gcc と clang）: 試験の辞書で 長い読みが先・1 つ目の候補が先、読みと同じ語は最後、送り仮名の読みは出ない・9 個まで、上限、一致の無い読み、利用者の語が先・利用者の同じ読みは辞書より先、利用者の送り仮名の読みは出ない・新しい順。`host-predict: PASS`。image の辞書（`SKK-JISYO.ja`、送り仮名の無い見出し 14,116）で 1 回の予測 27 µs（ASan の build の host、目安）。
- build: zedBSD の `bin/keiland-ime`、Linux の Keiland は warning 0。style-check は違反 0。

## 後半（判断の後）

- `ja-keys.c` の composing の key（D4、判断 1）: 予測の窓に入る・動く・選ぶ・抜ける。
- `ja-engine.c` で読みが 2 かな以上の時に予測を出力に入れる（`struct ime_output` の候補と、予測であることの印）、確定と学習（D5）。
- 既定の on・off（判断 2）。次の語の予測（判断 3）、SKK（判断 4）は判断しだい。
