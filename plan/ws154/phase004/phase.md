<!-- awesome-plan project=zedbsd record=ws154-p004 -->

# ws154-p004: SKK の mode を言語に、辞書を image に、試験

Status: in-progress（実装と host の試験は済み、QEMU は T1 待ち。全文の規約は WS の最後に）
Disposition: normal
Parent: [WS154](../ws.md)
Queue: Q1（2026-10-05、P2）
依存: p002・p003

## 実装（2026-10-05、P2）

- **mode を言語の ID に**（p001 の D3、Q1 が承認）:
  - `engine.h` の `struct ime_engine_ops` の末尾に `mode`（今の mode の ID と label を返す）と `select`（ID で mode を選ぶ。その engine の mode でなければ false）を足した。direct と ja の engine は NULL（1 つの言語のまま）。
  - SKK の engine（`skk-engine.c`）: `skk`（かな、「あ」）・`skk-katakana`（「ア」）・`skk-latin`（「A」）・`skk-wide`（「Ａ」）。
  - IME の program（`method.c`）: `program_announce_language` は mode を持つ engine なら mode の ID と label を送り、送った ID を覚える。key の後に mode が変わっていたら送り直す（`method_follow_mode`）。compositor の `select(id)` は、engine の ID に無ければ各 engine の `select` に尋ね、受けた engine を選んで mode を送る。
  - compositor は変えていない。ws095-p016 の app ごとの記憶は言語の ID で持つので、SKK の mode もそのまま app ごとに記憶される。
- **辞書を image に**（Q1 の判断、release に約 470 KB）: package `keiland-ime` の requires に `desktop/ime/skk-dict` を足した。keiland-ime の入る image には `ime-dict-skk` も入る。

## 確かめ

- host: `run-host-skk.sh` に mode の case を足した（`q` の後の ID が `skk-katakana`、`select("skk-latin")` で英数になり key が app へ、`select("ja")` は false）。`host-skk: PASS`（gcc・clang）。日本語の engine の試験 `plan/ws095/tests/host-engine.sh` も 233 passed, 0 failed（engine.h の変更で壊れていない）。
- build: zedBSD の `bin/keiland-ime`・`bin/ime-probe`、Linux の Keiland は warning 0。style-check は SKK の file が違反 0、`method.c` は関数ごとに増えていない。
- QEMU（T1 に依頼）: `plan/ws154/tests/languages-p004.sh`（image は p002 と同じ `config-amd64-languages.mk`）。probe A で skk → q で skk-katakana（アイ、system bar の ア の PNG）、probe B は desktop の言語、B が終わると A の skk-katakana が戻る（from=remembered、カ）、l で skk-latin（x は app へ）、C-j で skk（あ）。
- 未実施: 実機の UAT（SKK の操作感）、FreeBSD の build、WS の最後の全文の規約の見直し。
