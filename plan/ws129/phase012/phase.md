<!-- awesome-plan project=zedbsd record=ws129-p012 -->
# ws129-p012: REmacs のコマンド名を /bin/emacs に

Status: in-progress（P2、2026-10-04、q670 の途中の割り込み。実装・build・host 試験済み、T1 の remacs-guest 待ち）
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

## 残り

- Files の「Remacs」で開く（`userland/desktop/files/apps.c:113`、`@terminal remacs %f`）は存在しない `remacs` の command を呼ぶ。`emacs` に直すのは Files の WS の file なので Q1 に知らせた。
- 他の WS の試験の config（ws001・ws004・ws034・ws045・ws118 の `config-*.mk`）は `remacs` を選んでいる。名前の変更で黙って外れる（未登録の名前は無視される）。Q1 に知らせた。
- kernel の `src/kern/exec.c:804` の `REMACS_SKK_DICT` の環境変数は辞書の path のままで変更不要。
