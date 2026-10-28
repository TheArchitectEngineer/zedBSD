<!-- awesome-plan project=zedbsd record=ws307-p002 -->

# ws307-p002 OS 名「Kei」を削除して zedBSD に統一する

- 親: [WS307](../ws.md)。仕様: [spec.md](../spec.md) §13a（U15・U26〜U29・J3）。Queue: q1665 / q1665-i01（担当 P4、2026-10-29）。
- Status: in-progress（A〜E・G と H の段 1 を commit。F は案の PNG でユーザーの選択待ち、H の段 2 は asset の upload 待ち、compositor の 4 file と試験が残る。下の「残り」）
- Queue: q1665-i01（前の P4）、q1665-i02（2026-10-29、P4、worktree p4-ws307f、基点 35d2a373c）。
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

- H の段 1（mergeable、asset の名前・URL・SHA-256 は不変）: git mv（Q1 の OK、2026-10-29）`tools/release/kei-nightly.mk`→`zedbsd-nightly.mk`、`kei-nightly/`→`zedbsd-nightly/`、`make-kei-nightly-{base,zip}.py`→`make-zedbsd-nightly-{base,zip}.py`。target `zedbsd-nightly-base`・`zedbsd-nightly-zip`、変数 `ZEDBSD_NIGHTLY_*`、出力 `$(BUILD)/zedbsd-nightly.zip`、CI の `ci.yml`・`release.yml`（release の title「zedBSD <名前>」）、`plan/tools/hw5330/README.md` の path。`make -n … zedbsd-nightly-zip` で取得の URL が今の asset のままであることを確かめた。`plan/bugs/BUG-191.md:42` の旧い path は ticket なので Q1 に。

## 確かめ（2026-10-29、P4）

- build（`ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws307-main`）: getty・sh・monitor・sessiond、変えた base の 29 の program と service → rc 0、`error:`・`warning:` 0。os-release の生成（上）。
- 未実施: image の起動（boot-test.sh）、AAT の login（user/user）、`plan/tools` の道具の回帰を古い（kei）・新しい（user）の両方の image で（spec §13a の受け入れ）。

## ユーザーの決定（q1665-i02 の間、Q1 の中継）

- 2026-10-29 形式の識別子（原文、クリック）:「全部置き換え」。Q1 の中継: `/KeiNotesImage`・`kei-notes.bin`・`application/x-kei-notes`・libpdf の `Kei` の接頭辞・`~/.config/kei/ime` を zedBSD の名に置き換え、古い名は読まない（移行の code は書かない）。
- 2026-10-29 Q1 の判断: 試験の data の SSID `Kei Lab` と mail の試験の人名 `Kei`・`kei@example.net` は OS 名でないので残す（下の「残す物の一覧」）。完了 WS の試験 3 本の git rm は OK。`plan/tools/pdf/run-pdf-tools.sh` の golden の壊れは q1665 の範囲で直す。

## 結果 q1665-i02（2026-10-29、P4）

- C（commit f19ffa414）: Settings は Linux・FreeBSD でも build されるので、system の名前は os-release でなく build の定数 `ZEDBSD_OS_NAME`（`include/kern/os-name.h`）。About の「Operating system」の行だけ今どおり os-release の `PRETTY_NAME`（無ければ定数）。翻訳の key は名前を `{1}` にした（`Change how {1} looks and works.`・`Welcome to {1}, {2}`・`Start using {1}` など 8 つ、`settings.keys` と `ja/settings.tr`）。`se_named`（pages.c）が `kl_tr` の後に `{1}` を埋める。表の頁でない見出し（検索の結果）は埋めない（利用者の入れた `{` を守る）。About の hero は mark と名前、「powered by zedBSD」は OS 名が kernel 名と違う時だけ出す（技術の判断）。翻訳されない文（「later version of …」・About の menu・IPv6 の行）は文字列の連結で定数を使う。
- C の残り・D・形式（commit e0ce79063・224177be1）: Files の hero と help、imageview・terminal・textedit の About、libGL・libGLESv2・libEGL・GLX・X server の vendor と version、libbrowser の `navigator.platform` を定数に。browser の `start.html` は名前を言わない文に（data なので）。mailer の入力の例を `Alex Example`・`alex@example.net` に。IME の利用者の辞書の header、gpudemo（os-release の `NAME` を読む）、kuidemo・ipv6-probe の data、comment の「Kei の look・blue・mark」を desktop の物に。
  - D: `KEI_GLES_COMPUTE_TRACE` → `ZEDBSD_GLES_COMPUTE_TRACE`（libegl・libglesv2・gpudemo）、IME の log の印 `KEI-IME` → `ZEDBSD-IME`。`plan/tools/gpu-compute` の 2 本は古い image のために両方の変数を渡す、`plan/tools.md` の記述も。`plan/tools/x11/x11-p004.sh`・`x11-p005.sh` の期待は `(zedBSD|Kei)`。
  - 形式（上のユーザーの決定）: `/ZedBSDNotesImage`、libpdf の更新の名前の接頭辞 `ZedBSD`（`ZedBSD1_`〜`ZedBSD9_`）、Notes の添付 `zedbsd-notes.bin`・`application/x-zedbsd-notes`、IME の利用者の辞書 `~/.config/zedbsd/ime`。技術の判断: 形式の識別子は OS 名の macro でなく固定の文字（distribution の名前が変わっても形式は変わらない）。PDF の `/Producer`・`/Desc` の文は OS 名の定数。`plan/tools/aat/make-edit-samples.py` の `/KeiIm0` → `/ZedBSDIm0`（前の版の名前と重なる場面の試料）。
- 試験の道具の直し: `plan/ws089/tests/host-kl-system.c` に `kl_system_phone_*`・`kl_system_displays_rotation*` の stub 7 つ（commit b7578341a、host-build.sh の link の既存の失敗）。`plan/tools/pdf/run-pdf-tools.sh` の golden（commit da90c27b7）: 基点の f9077504e は object としてあるが、その時 libpdf は `userland/base/libpdf/` にあったので path を直し、`-I.` を足した（`userland/desktop/paths.h` と os-name.h）。完了 WS の試験 3 本（`plan/ws095/tests/ime-p004.sh`・`plan/ws127/tests/bug177-guest.sh`・`plan/ws128/tests/terminal-p011-ime.sh`）を git rm、`userland/tests/ime-probe` の comment の参照を外した（commit 1b7b7b7fe）。
- E（commit edf0dbe17）: loader・kernel の splash・HAL の bsp の実装（`cons.c`）の comment の「Kei splash」を「boot splash」に（comment だけ）。`platform/amd64/vmunix.mk`・`tools/build/make-boot-splash.py` の file の名前は F の後。
- README.md の用語の「Kei」の行を「Disk images」に（commit 081df0d8e）。`tests/scenarios` の利用者の名前（passkey の行 `user:1000`、`auth\nuser`）、About の `PRETTY_NAME` の例、文（commit d1b3911c7）。
- F（U29・J3、案）: 今の splash から「Kei」と「powered by zedBSD」を消して背景で埋め、Lato Light（OFL）で「zedBSD」を mark の幅に置いた案 1（mark は今のまま）と、mark を「Z」（上下の淡い青の glass の棒と、左下の濃い青から右上へ渡る葉）に描き直した案 2。PNG は `build/ws307-test/logo/`（P4 の worktree、git に入れない）。Q1 経由でユーザーの選択待ち。

## 確かめ q1665-i02（2026-10-29、P4）

- build（`ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws307-main`）: settings・files・imageview・notes・terminal・textedit・mailer・browser・keiland-ime・pdfviewer・xserver・kuidemo と `dynamic/libGL.so`・`libGLESv2.so`・`libEGL.so`・`libGLX.so` → rc 0、`warning:`・`error:` 0。ipv6-probe（config で n）は build していない（data の 8 byte の文字だけの変更）。
- host: `plan/tools/keiland/run-host-welcome.sh` → `run-host-welcome: PASS`（PNG で「Welcome to zedBSD, …」「Start using zedBSD」、About の hero「zedBSD」と「This computer and the version of zedBSD.」を目で見た）。`tools/i18n/tr.py check --strict` の settings の catalog は変更の前と同じ 5 件の未訳だけ（既存）。`plan/tools/pdf/host-pdf-find-l.sh` PASS、`plan/tools/pdf/run-pdf-tools.sh` → `run-pdf-tools: ok`（`golden: 600 strokes, 0 differ`）。
- 未実施: image の起動と AAT、`plan/tools` の道具の回帰を古い（kei）・新しい（user）の image で、Linux・FreeBSD の Keiland の build。

## 残す物の一覧（p002 の後に `git grep -I -i kei`（keiland を除く）に残ってよい物）

- Keiland・libkeiland と内部名（U28）、`plan/` の履歴、`docs/release/zedbsd-1.0.0-beta2*.md`（出した版の記録、Q1 の判断）、`config/release/config-amd64-beta2.mk` の「beta2 の release では kei」の注記。
- 旧い名前の互換・注記: `keiland-linux.mk`・`keiland-freebsd.mk` の `libkeiui` を消す一覧、`SKK-JISYO.kei`（旧い file の名前、`ime/dict/Makefile`・`SKK-JISYO.ja`・`merge.sh`）、`plan/ws035/kei-identity-design.md` への参照（plan の file の名前）。
- 試験の data（2026-10-29 Q1 の判断）: SSID `Kei Lab`（`userland/tests/network-probe`、aat の helper、scenario、他の WS の試験）、mail の試験の人名 `Kei`・`kei@example.net`（`plan/tools/mail`・aat・scenario）。
- 偽の一致: `include/libc/GLES2/gl2ext.h` の `MakeImageHandle…`、GLSL の `"kEi"`（builtins.c の型の符号）、`plan/tools/keiui`、`userland/x11/zwm/backgrounds/*.ppm`（binary）、`libffi/Makefile` の `makeinfo`。

## 残り（次の attempt への引き継ぎ）

1. **F**: ユーザーの選択（案 1・2、字体）の後、`kei-boot-splash.png` を git mv で `zedbsd-boot-splash.png` にして中身を替え、`platform/amd64/vmunix.mk`・`tools/build/make-boot-splash.py`・`artwork/README.md` の名前と comment を直す。案 2 なら `artwork/mark.c` の mark を Z に描き直す（Settings・Files・imageview・compositor が使う）。
2. **compositor**（`wayland/greeter.c`・`glass.c`・`glass.h`・`shell.c`・`notify-popup.c`）の「Kei」の文字と comment: 最後に、P1（WS309）に Q1 経由で知らせてから小さい commit。
3. **H の段 2**: archive の中の `Kei-nightly/` と `tools/release/zedbsd-nightly/` の README・THIRD-PARTY・inputs・licenses の文、新しい asset の名前と SHA-256。upload はユーザーの指示まで止める。asset の名前・URL を変える commit は upload の前に main に merge しない。
4. 試験: image（`plan/ws307/tests/` の config）で boot-test.sh の login prompt（新しい logo）、AAT の login（user/user）、`plan/tools` の道具の回帰を古い kei の image と新しい user の image の両方で、nightly の取得、`git grep -I -i kei` の残りが上の「残す物の一覧」だけ。
