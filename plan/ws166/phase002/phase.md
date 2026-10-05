<!-- awesome-plan project=zedbsd record=ws166-p002 -->

# ws166-p002: 予測の候補の生成（engine の側）

Status: cleared（2026-10-05 Q1: T1-196c PASS（QEMU、画面キーボードの予測の候補・確定・学習）、osk-guest の回帰も T1-196 で PASS）。以前: in-progress（2026-10-05 夕 q768: 実装と host の試験は済み、QEMU は p004）
Disposition: normal
Parent: [WS166](../ws.md)
Queue: q737（Q1、2026-10-05）、q768（後半）
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

## 後半（2026-10-05 夕、q768、p001 の改訂: 画面キーボード）

- `libwayland/ime-status-protocol.c`・`zed-ime-status-v1-client-protocol.h`: `keiland_ime_status_v1`・manager を version 2 に。request 3 `predictions(u serial, s list)`、event 2 `predict(u serial, s reading)`、event 3 `learn(s reading, s word)`（signature の since 2）。
- compositor（`wayland/input-method.c`・`ime.h`・`protocol.c`）: global の version 2、`zwl_ime_predict`・`zwl_ime_learn`（status の version が 2 未満なら ENOTSUP・何もしない）、`predictions` を `zwl_keyboard_predictions` へ。
- IME（`ime/engine.h`・`main.c`・`method.c`）: ops に `predict`・`learn`（direct・SKK は NULL）、status を version 2 で bind、`status_predict`（最初の predict を持つ engine、無ければ空の list で答える。log `KEI-IME PREDICT`）・`status_learn`（log `KEI-IME LEARN`、保存は既存の 3 分の遅延の保存）。
- 日本語の engine（`ja-engine.c`・`ja.h`・`ja-predict.c`）: `engine_predict`（索引を最初に作る、`ja_predict_keyboard` で読みと同じ語を先に、最大 12、「語\t読み\n」の行、入らない行は切る）、`engine_learn`（`ja_user_learn`、`user_unsaved`）。索引は `ja_core_close` で解放。
- 試験: `plan/ws166/tests/host-predict.c` に画面キーボードの順の 2 例、`host-engine-predict.c`（新規、engine の ops を通して list の形・学習の後の順・小さい room で行ごとに切る・一致なし）、`run-host-predict.sh` に追加。`plan/ws095/tests/host-engine.sh` に `ja-predict.c` を足した（engine が link できなくなるため）。

### 確かめ（host、2026-10-05 夕）

- `sh plan/ws166/tests/run-host-predict.sh`（gcc と clang、ASan・UBSan）: `host-predict: PASS`、`host-engine-predict: PASS`。
- `sh plan/ws095/tests/host-engine.sh`: 233 passed、0 failed。
- build: zedBSD の `bin/wayland`・`bin/keiland-ime`、`make keiland-linux` とも warning 0。style-check: 新しい関数の findings 0（変えた file の既存の件数は増やしていない）。
