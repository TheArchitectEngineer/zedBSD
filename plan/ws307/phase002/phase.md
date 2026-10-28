<!-- awesome-plan project=zedbsd record=ws307-p002 -->

# ws307-p002 OS 名「Kei」を削除して zedBSD に統一する

- 親: [WS307](../ws.md)。仕様: [spec.md](../spec.md) §13a（U15・U26〜U29・J3）。Queue: q1665 / q1665-i01（担当 P4、2026-10-29）。
- Status: in-progress（A・B・base の C・E・G を commit。残りは下の「残り」）
- 出典: 2026-10-28 ユーザー「P4に伝えてください。起動ロゴ画像を含めて、KeiというOS名を削除して、zedBSDに統一します。インストーラだけでなくすべてに影響します。そういうphaseをこのWSに追加してください。」、U26・U27・U28・U29・J3（ws.md の原文）。

## 前の段: plan/tools を user → kei の順に（Q1 が q1665 に足した、2026-10-29）

- commit bfc63703d（main に merge 済み 0281b40f5 の前の分）: `plan/tools/aat/scenarios/aatlib.py`（Run の作成の時に target の uid 1000 の名前を読み USER・PASSWORD・HOME を決める、`AAT_USER` で上書き、`--local` では読まない）と helper の `/home/kei`・`chown kei`・`su kei`、`plan/tools/aat/aat`（sudo の password の既定は login の利用者の名前）、`plan/tools/hw5330/5330-{run,bootcycle,survey}.sh`（user → kei の順に sshpass）、`plan/tools/gpu-perf/measure.sh`（uid 1000 の名前で su）、`plan/tools/guest/serial.py` の comment。`plan/tools/aat/tests/run-host.sh` PASS。

## 棚卸し（2026-10-29、基点 61630bb0b）

`git grep -I -i kei`（keiland を除く、plan/ 以外）499 行。分類と扱いは Q1 に送った（2026-10-29 の SendMessage）: A 利用者・hostname、B OS 名の定義、C program の文字、D 診断の識別子、E comment（GPU の protocol など）、F 絵（U29・J3）、G license の記録の文（J3）、H release の nightly、I docs/release（出した版の記録）。

## 結果（2026-10-29、P4）

- A（commit 539a8c25a）: `userland/base/etc/passwd`（`user:x:1000:1000:User:/home/user:/bin/sh`）・`shadow`（`user` の password `user` の SHA-512 crypt、`openssl passwd -6`）・`group`（wheel・network の member と `user:x:1000:`）・`rc.conf`（`hostname: zedbsd`）、getty・sh・monitor の既定の host `zedbsd`、`userland/desktop/sessiond/autologin`（`user`）と comment、`config/current-uat.mk`・`config/release/config-amd64-beta2.mk` の comment、`tests/scenarios` の 58 file（利用者の語 `kei` → `user`、`/home/kei` → `/home/user`。`Kei Lab`・`Kei Example`・`kei@example.net` は C の残り）。
- B（commit a0cc36354）: OS 名の 1 か所は `include/kern/os-name.h`（`ZEDBSD_OS_NAME` "zedBSD"・`ZEDBSD_OS_ID` "zedbsd"）。build は `config/os-name.mk` がその header から同じ 2 つを読む（`Makefile` が include）。`/etc/os-release` は `NAME="$(ZEDBSD_OS_NAME)"`・`ID=$(ZEDBSD_OS_ID)`・`PRETTY_NAME="$(ZEDBSD_OS_NAME) $(ZEDBSD_VERSION_NAME)"`（`Kei/zedBSD` を外した）。生成の確認: `make … version` で `NAME="zedBSD"`・`PRETTY_NAME="zedBSD 1.0.0 Beta 2"`。
  - 技術の判断 T26（spec §13a の「make の断片に置く」からの変更、Q1 の確認を求める）: 値の置き場を make の断片でなく C の header にし、make が header を読む。理由: 全ての architecture の userland の CPPFLAGS（amd64・arm64・riscv64・x68k・sparcv9（ユーザーの物）…）と Linux・FreeBSD の Keiland の build は `-I.` を持つので、`#include "include/kern/os-name.h"` で全てに同じ値が届き、各 platform の mk に `-D` を足さずに済む（sparcv9 の mk に触れない）。定義は 1 か所のまま。
- C の base の分（commit a0cc36354）: base の 31 file の版の表示 `(Kei)`、`ar` の `Kei ar 1`、`at` の job の header、`fetch` の User-Agent、service console の題を `ZEDBSD_OS_NAME` に。`ls` の comment。
- E・G（commit 08b64e95f）: GPU の command protocol の名前（J3）を「zedBSD GPU command protocol」に（`include/uapi/gpu-op.h`、5 つの driver の header、2 つの `vk-stream.c`、libvulkan の Makefile と 2 つの noct の道具、`include/libc/vulkan/API-PROVENANCE.md`。識別子・番号・形は変えていない）。license の記録の文（J3）: `ime/dict/Makefile`・`ime/skk-dict/Makefile`・`SKK-JISYO.ja` の header・それを作る `merge.sh` の雛形の「for Kei」「Kei's input method」を zedBSD に（日付と原文の引用は保つ、「formerly SKK-JISYO.kei」は旧い file 名の注記として残す）。

## 確かめ（2026-10-29、P4）

- build（`ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws307-main`）: getty・sh・monitor・sessiond、変えた base の 29 の program と service → rc 0、`error:`・`warning:` 0。os-release の生成（上）。
- 未実施: image の起動（boot-test.sh）、AAT の login（user/user）、`plan/tools` の道具の回帰を古い（kei）・新しい（user）の両方の image で（spec §13a の受け入れ）。

## 残り（次の attempt への引き継ぎ）

`git grep -I -i kei`（keiland を除く、plan/ 以外）は 332 行が残る。

1. **C の残り（desktop）**: Settings（`pages.c`・`welcome.c`・`page-about.c`・`page-home.c`・`page-soon.c`・`page-wired.c`・`page-sharing.c`・`page-network.c`・`page-languages.c`・`page-input.c`・`menu.c`・`search.c`・`settings.h` と翻訳の key `userland/desktop/locale/settings.keys`・`locale/ja/settings.tr`。走っている system を言う文は os-release の NAME、`kl_tr_format` の `{n}` で名前を差し込む形に key を変える）、Files（`ui-help.c`、`ui-home.c` の hero の文字）、imageview の message、libGL・libegl・libglesv2 の vendor・version の文字、libbrowser の platform、browser の `start.html`（data。build で置き換えるか名前を言わない文に）、IME の `USER_HEADER`、mailer の例の `Kei Example`、test の Wi-Fi の SSID `Kei Lab`（`userland/tests/network-probe` と scenario）、`userland/tests` の表示（gpudemo の echo など）。
2. **D 診断の識別子**: `KEI_GLES_COMPUTE_TRACE` → `ZEDBSD_GLES_COMPUTE_TRACE`（libegl・libglesv2、使う道具も）、IME の log の印 `KEI-IME` → `ZEDBSD-IME`（使う AAT・scenario も同じ commit で）。
3. **残すかの判断（Q1）**: file の形の識別子 — libpdf の `/KeiNotesImage`・名前の接頭辞 `Kei`、notes の添付 `kei-notes.bin`・`application/x-kei-notes`、IME の利用者の辞書の置き場 `~/.config/kei/ime`。変えると既存の PDF・notebook・辞書が読めなくなる。案: 書く名前を zedBSD に変え、読む時は両方を受ける（または残す物の一覧に入れる）。`docs/release/zedbsd-1.0.0-beta2*.md`（出した版の記録）は残す案。
4. **E の残り**: splash・logo・mark の comment（`bootloader/*/logo.c`・`bootx64.c`・`include/kern/text-display.h`・`src/drivers/platform/pcat/graphics/splash.[ch]`・`src/hal/amd64/bsp-pcat/cons.c`（HAL の実装、comment だけ）・`platform/amd64/vmunix.mk`・`tools/build/make-boot-splash.py`）は F と一緒に。
5. **F 絵（U29・J3）**: `userland/desktop/artwork/kei-boot-splash.png` を「zedBSD」の文字の同じ様式の絵に作り直し（git mv で `zedbsd-boot-splash.png`）、`artwork/mark.c` の mark を描き直す。案の PNG を Q1 経由でユーザーに見せてから入れる。compositor（`wayland/greeter.c`・`glass.c`・`shell.c`・`notify-popup.c`）の「Kei」の文字と comment は P1（WS309）と衝突しうるので、最後に小さい commit で Q1 に知らせて。
6. **H release**: `tools/release/kei-nightly.mk` ほかの git mv（一覧は Q1 に送った、OK 待ち）、`.github/workflows/ci.yml`・`release.yml`（target・変数・title「zedBSD <名前>」）、archive の中の `Kei-nightly/` の名前に WINQ-EMU の launcher が頼るかの確かめ、新しい asset の名前と SHA-256。upload はユーザーの指示で Q1、upload の前に URL を変える commit を main に merge しない（spec §13a の移行の手順）。
7. **README.md**・`config/rootfs-options.list`・`config/ci/*.mk` の comment、`userland/packages`・`userland/x11/zwm/backgrounds`（偽の一致かを確かめる）。
8. 試験: image（`plan/ws307/tests/` の config）で boot-test.sh の login prompt（新しい logo）、AAT の login（user/user）、`plan/tools` の道具の回帰を古い kei の image と新しい user の image の両方で、nightly の取得、`git grep -I -i kei` の残りが「残す物の一覧」だけ。
