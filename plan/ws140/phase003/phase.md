<!-- awesome-plan project=zedbsd record=ws140-p003 -->

# ws140-p003: 規約の見直し、build、回帰、結果の反映の案

Status: in-progress（規約の見直しと build は済み、desktop の回帰は T1 待ち）
Disposition: normal
Parent: [WS140](../ws.md)
Queue: q729（Q1、2026-10-05）
設計: [ws.md](../ws.md) の D9 と完了の条件 3〜6
依存: p001・p002（2026-10-05 Q1: T1-164 PASS で cleared）

## 範囲

- **入る**: WS140 で変えた code（`src/rtld/rtld.c`・`rtld.h`・`elf.h`、`userland/tests/dyntest.c`、`plan/ws140/tests/*.c`）の全文規約の見直し。amd64 の build と bss の記録。desktop の回帰（Files の PDF の縮小表示、`dlopen("libpdf.so")`）の T1 への依頼。F-070 と ws115-p010 への反映の案（記録は Q1）。
- **入らない**: 変えていない関数の古い書き方（D9）。arm64・i386・sparcv9 の build（sysroot が要る。Q1 2026-10-05: subagent は sysroot を作らず、amd64 の build と host・guest の試験で判定する）。GTK4 の起動の確認（ws115-p010 の再開の時、ws.md の完了の条件 6）。

## 見直し（2026-10-05、P2）

[全文規約](../../coding-style.md) の §14 の checklist で、WS140 の新しい関数と、新しく書いた行・変えた行を読んだ。直した物:

- 中身の無い comment（`/* The slot. */`・`/* The headers. */`・`/* Places it. */` など 20 か所）を、動詞と目的語で何をするかを言う文にした。preflight の `/* Reports operation failure. */`（禁止の形）を、何を拒むかの文にした。
- 分けた呼び出しで 1 行に引数が 2 つ以上あった所（`read_program_headers` 2 か所、`pread` の `syscall6`、`map_call`、`layout_static_tls` の `rtld_memcpy`）を、1 行 1 引数にした。
- `plan/tools/style-check.py` の違反: rtld.c は 241（作業の前）→ 227。関数ごとに増えた所は無い。新しい関数・試験の C・`rtld.h`・`elf.h` は 0。dyntest は 44 → 40。
- 機械で見られない項目（comment の中身、1 行 1 引数、名前、成功の return が最後、file の大域の変数の comment、critical section の空行）も読んで確かめた。

## build（2026-10-05、amd64）

- `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws140-p002 …/ld.so …/libc.so …/dyntest …/tlstest.so`: warning 0（-Werror）。`dynamic-userland-check`: PASS。手順 0 の sysroot の確かめは 0。
- bss（ld.so）: 217,944（WS140 の前）→ **94,076**。text 33,049 前後、data 428。
- 未実施: arm64・i386・sparcv9（上の理由）。

## 回帰（T1）

- p001・p002 の試験（T1-164 PASS、Q1 の判定）: rtld-many の 16 段、tls-check（動的・静的）、dyntest の `HANDLES-200`・`PLUGIN-TLS`、boot-test。
- T1-163 で見つかった、segment の map の場所の欠陥（`object_reserve_span`、p002 の「T1-163 の後」）も T1-164 で確かめた。
- **依頼する物**: Files の PDF の縮小表示。`plan/tools/files/build-files-image.sh BUILD`（今の main、WS140 の ld.so が入っている）で作った image で、`plan/ws127/tests/files-p002.sh` の 4（`THUMB error=0 cached=0` と、2 回目の窓での `cached=1`、pdf.png）。script は 1〜5 を流すので、全体を流して 4 の行を見る。

## 反映の案（Q1 が記録する）

- **F-070**（`plan/future-work.md`）: Disposition の後ろに「WS140 で実装した（p001 0484201b・p002 360c7dbe・map の直し a555c47a、T1-164 PASS）。依存・object・handle・TLS module の数、TLSDESC の引数、program header、名前の長さの上限は無い」を足す。
- **ws115-p010**: 手順 1（定数を 64・128 に上げる）は WS140 で不要になった。再開の時は手順 1 を飛ばし、`gtk4-widget-factory` の起動から確かめる（WS140 の完了の条件 6）。

## 結果

（T1 の desktop の回帰の後に書く）
