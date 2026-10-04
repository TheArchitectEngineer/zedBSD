<!-- awesome-plan project=zedbsd record=ws129-p012 -->
# ws129-p012: REmacs のコマンド名を /bin/emacs に

Status: cleared（2026-10-04 Q1 の判定: T1-082 で install・C-x C-s の保存・C-x C-c の終了 ok、PNG に editor、boot-test PASS。FAIL の「emacs runs」は試験の誤り（ps の args は `emacs`）で P2 が 3876079 で直した。再試験は省き、実機は uat.md F7 で確かめる）
Disposition: normal
Parent: [WS129](../ws.md)

## 範囲（2026-10-04 ユーザー「remacs のコマンド名は/bin/emacsでお願いします」、Q1 の指示）

`userland/base/emacs/Makefile` は `/usr/bin/remacs.nap`（target の noct が実行）を入れていた。`/bin/emacs` で起動できるようにする。辞書の path（`/usr/share/remacs/skkjisyo.dic`）は今のまま。package・menuconfig の名前は emacs に揃えてよい（CI の config の選択も）。`plan/ws129/tests/remacs-guest.sh` を /bin/emacs で起動する形に、license の生成物も更新。

## 読み（.nap の実行のされ方）

- remacs.nap は `noct --compile --app` の Noct application で、先頭が `#!/usr/bin/noct\n`、mode 0755（NoctLang の `src/cli/app_file.c`）。noct は実行の時にこの行を完全一致で取り除き、内容の magic（`Noct App 1.0`）で判定する（`src/cli/cli-run.c` の `classify_program_input`）。拡張子は見ない。
- zedBSD の kernel は `#!` を解釈する（`src/kern/exec.c` の `exec_shebang_parse`、interpreter は `file_openat` で開くので symlink を辿る）。
- target の noct は `/bin/noct`（noct の package）で、`/usr/bin/noct` は無かった。→ `.nap` を `/bin/emacs` の名前で入れ、`/usr/bin/noct -> /bin/noct` の link を足せば `/bin/emacs` が直接動く（起動の script は要らない）。shebang の行を書き換えると noct が取り除けないので、link の方にした。

## 変更

- `userland/base/emacs/Makefile`: package の名前 `remacs` → `emacs`（label「Emacs editor」、menu は packages/editors のまま）。`--file /bin/emacs=…remacs.nap`、`--mode /bin/emacs=0755`、`ZEDBSD_PACKAGE_LINKS += /usr/bin/noct=/bin/noct`。作った nap の先頭が `#!/usr/bin/noct` でなければ build を失敗に。phony の target `remacs` → `emacs`。README の zedBSD の節。
- `config/ci/config-amd64.mk`: `remacs` → `emacs`。
- `tools/release/license-components.json`: id・when を `emacs` に。`plan/ws129/licenses-generated.md`・`licenses-index.txt` を再生成。`plan/ws129/tests/license-inventory-test.sh`・`plan/tools/menuconfig-target-host-test.py` の名前。
- `plan/ws129/tests/remacs-guest.sh`: `/bin/emacs`（実行可、先頭の行）・`/usr/bin/noct`・辞書が在り、古い `/usr/bin/remacs.nap` が無い。terminal で `emacs /tmp/remacs-test.txt`、process `/usr/bin/noct /bin/emacs` を確かめる。

## 確かめ

- `make ZEDBSD_CONFIG=plan/ws129/tests/config-amd64-ci-noclang.mk BUILD=build/p2-ci build/p2-ci/rootfs/.stamp` exit 0、warning 0（filter の後）。rootfs に `bin/emacs`（0755、先頭 `#!/usr/bin/noct`）、`usr/bin/noct -> /bin/noct`、`usr/share/remacs/skkjisyo.dic`、`usr/bin/remacs.nap` は無い。
- host: host の noct（`build/NoctLang/build-static/noct`）で rootfs の `bin/emacs`（拡張子なし、shebang 付き）を pty で起動 → `hello`・C-x C-s で保存・C-x C-c で終了を確かめた。
- `license-inventory.py`（CI と demo の config）25 components・0 open items。`license-inventory-test.sh` PASS、`menuconfig-target-host-test.py` PASS。
- QEMU: 未実施（T1 に remacs-guest.sh を依頼）。zedBSD の kernel で `#!` + symlink の interpreter を通す確認はこの試験で。

## Q1 の判断と追加の変更（2026-10-04、6a84def の統合の後）

- (1) Files の opener（`userland/desktop/files/apps.c:113`）の `@terminal remacs %f`（存在しない command）を `Emacs`・`@terminal emacs %f`・必要な program `emacs` に直した（見つけた担当がすぐ直す方針）。`plan/tools/files/host-default.sh` PASS（その試験の「Remacs」は試験が自分で作る選択で builtin に依らないので変えない）、zedBSD の `bin/files` の build exit 0・warning 0。
- (2) 試験の config の `remacs` を `emacs` に一括で: ws001 `config-amd64-lean-guest.mk`、ws004 `config-ax211-desktop.mk`・`config-ax211-usb.mk`、ws034 `config-amd64-usr.mk`、ws045 `config-amd64-base.mk`（注記だけ）、ws118 `config-remote-log.mk`・`build-remote-log-image.sh`（注記）。多くは `filter-out … remacs` で、名前を変えないままだと CI の config から来る `emacs` が外れずに入るところだった。
- (3) `/usr/bin/noct -> /bin/noct` の link は emacs の package に置く（Q1 の判断）。理由: Noct の application の先頭の行は Noct が `#!/usr/bin/noct` と書き、実行の時にその完全一致の行を取り除く。target の noct は `/bin/noct` なので、その path に interpreter が要る。本来は noct の package の役だが、`userland/base/noct/` の build の規則は toolchain の扱いで main の許可が要るため、それを要する emacs の package が持つ。

## 残り

- kernel の `src/kern/exec.c:804` の `REMACS_SKK_DICT` の環境変数は辞書の path のままで変更不要。
- QEMU（T1 の remacs-guest）の結果待ち。

## T1-082 の結果と試験の直し（2026-10-04）

- T1-082（6dbcc5b、CI の config の image、Venus KVM）: `remacs-guest: FAIL` ×2。落ちたのは「emacs runs」だけ。file の確認・C-x C-s の保存・C-x C-c の終了は ok、remacs.png に terminal の中の emacs（hello、`Wrote /tmp/remacs-test.txt`）。boot-test PASS、license-inventory（`--rootfs`）open 0。
- 読み: T1 の `ps-during.txt` で該当の process の args は `emacs` だけ（`noct /bin/emacs` の形でない）。editor は動いており、試験の探す文字列の誤り。process の確認を `(^|[ /])emacs( |$)` に直した（`emacs`・`/usr/bin/noct /bin/emacs …` の両方に合い、`grep`・`terminal` には合わない）。T1 に再試験を依頼する。zedBSD の ps が `#!` で起きた process をどう見せるか（argv の書き換えの有無）は今回調べていない。

## IME の辞書を tree に（2026-10-04、ユーザー「IME の辞書はコピーして取り込んでください。」）

- `userland/base/emacs/dict/SKK-JISYO.X` を `userland/desktop/ime/dict/SKK-JISYO.X` に写した（SHA-256 `73819384…ab9`、前の archive からの取得と同じ byte）。`userland/desktop/ime/dict/Makefile` は REmacs の archive の取得（`ZEDBSD_EXTERNAL_SOURCE`）をやめて tree の file を入れる。`keiland-linux.mk`・`keiland-freebsd.mk` も archive の取得をやめ、tree の file を `*_DATA` で写す。
- `tools/release/license-components.json` の ime-dict-ja（version を空、paths を `userland/desktop/ime/dict/`）、`plan/ws129/licenses-generated.md`・`licenses-index.txt` を再生成（25 components、open 0）、`plan/tools/packages/audit-licenses.sh` の GPL の archive の一覧から REmacs の archive を外した。
- 確かめ: CI（clang 抜き）の rootfs の build exit 0・warning 0、`usr/share/kei/ime/ja/SKK-JISYO.X` の SHA-256 が同じ。`make keiland-linux` exit 0・warning 0、curl・tar の取得なしで同じ SHA-256。FreeBSD は `make -n keiland-freebsd` で tree からの cp を確かめただけ（FreeBSD の上の build は未実施）。`license-inventory-test.sh` PASS。QEMU の IME の試験は未実施（辞書の byte が同じなので挙動は変わらない見込み）。
