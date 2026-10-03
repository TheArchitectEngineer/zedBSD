<!-- awesome-plan project=zedbsd record=ws074p002 -->

# ws074-p002: 骨組み（directory、build の登録、base/、headless の入口、host の build、suite の取得）

Phase ID: `ws074-p002`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）

## 範囲

`userland/desktop/browser/` の骨組み: package の Makefile（amd64、既定 n）、amd64 の link の規則、`main.c`（command line と
mode の振り分け）、`base/`（arena、byte と UTF-16 の buffer、配列、UTF-8/UTF-16（WHATWG の decode の規則）、hash、file）、
`shell/` の仮の窓の mode。host の build（plain と ASan・UBSan）と単体の試験、suite の取得の script、guest の image と guest の操作の
script。

## 受け入れ

1. amd64 で build が通る（warning 0）。
2. host の試験 `host-base` が plain と ASan で全部通る。
3. guest で `browser --version` が動き、未知の option が status 2、窓の mode が明示の失敗。
4. boot test が通る。
5. 新しい C の file が `style-check.py` で 0 件。

## 結果（2026-09-27）

cleared。

- 追加: `userland/desktop/browser/{Makefile,main.c,base/*.c,base/base.h,shell/shell.[ch]}`、
  `platform/amd64/vmunix.mk` の link の規則（`-Iuserland/desktop/browser`、libc だけ）と基本の command の除外の list への追加。
  試験: `plan/ws074/tests/{host-build.sh,host-shell.c,host-base.c,fetch-suites.sh,config-amd64-browser.mk,build-browser-image.sh,browser-guest.sh}`。
- build: `make ZEDBSD_CONFIG=plan/ws074/tests/config-amd64-browser.mk build/amd64/bin/browser`（-Werror、warning 0）。
- host: `host-base` 2038 検査、plain・ASan とも 0 件の失敗（arena、buffer、配列、UTF-8 の不正な列 13 例、UTF-16 の surrogate、
  hash、file の読み書き）。
- guest（QEMU、GPU 無しの guest、`browser-guest.sh plain`）: `browser --version` → `browser 0.1 (ws074)`、
  `--bogus` → status 2、引数無し → 窓の mode が未実装の 1 行と status 1。
- boot test: PASS（`build/ws074-boot-p002/login.png` → `/home/awe/zedBSD-rpi4/build/ws074-shots/p002-20260927-boot-login.png`）。
- style-check: 新しい file 0 件。
- suite: `fetch-suites.sh html5lib` で html5lib-tests の固定 commit（`224991ec…`、MIT を確認）を取得。
- **見つけた事実**:
  - kernel の exec は `PT_GNU_STACK` の 1 MiB（`EXEC_STACK_HARD_MAX`）を超える stack を拒む（8 MiB を頼んだ最初の build は
    `ENOEXEC` になり sh が script として読んだ）。1 MiB に戻し、深い再帰が要る時は大きな stack の thread で回すことを
    design.md §1 に書いた。
  - html5lib-tests の tree-construction の試験は WPT へ移った（html5lib-tests の最新の commit の message「Tree construction tests have
    moved to WPT」）。p005 の runner は WPT の `html/syntax/parsing/` の `.dat` を読む（WPT の取得の範囲に既に入っている）。
