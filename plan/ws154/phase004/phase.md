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

## T1-173・T1-174 の FAIL（2026-10-05、main 555e105e、P2 の解析）

- p002 の `preedit=▼漢字`・`commit=漢字` の MISSING は試験の期待の誤り。image の SKK-JISYO.X は `かんじ /感じ/漢字/幹事/完治/` で、最初の候補は 感じ（host で同じ辞書と engine を動かして Kanji＋Space → 感じ を確かめた）。engine と辞書は正しく動いている（`▽かんじ` まで ok、辞書は image の `/usr/share/keiland/ime/skk/` にある）。直し: `languages-p002.sh` を Space 1 回で `▼感じ`、2 回目で `▼漢字`、Enter で `commit=漢字` に。
- p004 の SKK の段がまとめて MISSING は試験の手順の誤り。`keiland-settings` は zdesktop の Wayland の client なので、desktop を起動する前の `keiland-settings set ime.method 2` は失敗し（出力は捨てていた）、IME は既定の ja で起動した。直し: desktop の起動の後に `set ime.method 2` を流し、`--method=skk` での起動し直しを待つ。製品側は、保存済みの ime.method が起動時に適用される（settings.c の starting の経路）ので直さない。
- UAT の image（`plan/ws159/tests/config-amd64-uat.mk`）: keiland-ime の requires で ime-dict-skk が解決され、`make -n` で SKK-JISYO.X が image に入ることを確かめた。SKK の動作は T1-173 と同じ（Kanji＋Space は 感じ が先）。
- T1-173b（2f36b792 の後）: p002 は PASS。p004 は `commit=アイ` だけ MISSING。これも試験の期待の誤り: SKK のカナ・かなの入力では文字の仮名がその場で確定するので、"ai" は `commit=ア` と `commit=イ` の 2 回（host で同じ engine に q・a・i・k・a を打ち、ア・イ・(k)・カ を確かめた）。直し: `languages-p004.sh` を `commit=ア$` と `commit=イ$` の 2 行に。
