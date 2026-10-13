<!-- awesome-plan project=zedbsd record=ws228-p003 -->
# ws228-p003: 規約の全文の見直し

Status: cleared（2026-10-14 Q1: 規約の全文の見直し、build warning 0・host 試験 PASS、見直しの後の vi で T1-601 PASS）
Disposition: normal
Parent: [WS228](../ws.md)
依頼: Q1（ws228-p003、P3）
依存: p001・p004（merge 済み）

## 範囲

WS228 が足した・変えた C を `plan/coding-style.md` の全文と照らして読み、書き方だけを直した（動作・API・ABI・計算の順序・error の値は変えない）。

- `userland/base/vi/` の全部（`vi.h`・`main.c`・`terminal.c`・`buffer.c`・`undo.c`・`file.c`・`screen.c`・`command.c`・`ex.c`・`search.c`）。
- `src/kern/tty-csi.c`・`tty-csi.h`（p004）。
- `src/kern/tty.c` の WS228 の変更（0e7d613ed 〜 8caf1c799、344aae030: `console_escape[]`・`console_attribute[]`・`console_attribute_marked[]`、`tty_console_csi`・`tty_console_text`・`tty_console_attributed_byte`・`tty_render`・`tty_vt_activate`・`tty_console_init`・pty の winsize の 0 化）。
- `tests/tty-csi-host-test.c`。

## 手順と結果

機械の検査（`python3 plan/tools/style-check.py`）→ 全文を読む → 直す、を file ごとに行った。

- style-check の最初の件数: vi の 9 file で 207 件（blank-after-brace 93、paragraph-comment 約 90、call-in-condition 9、goto 10）。`tty-csi.c`・`tty-csi.h`・`tty-csi-host-test.c`・`tty.c` の WS228 の変更の範囲は 0 件。**直した後の件数: 0 件**（vi の全 .c）。
- 直した規則の種類（vi）:
  - §5 閉じ括弧の後の空行と、その後の段落の目的の comment（約 93 箇所）。
  - §5 段落の頭の comment（`x = &y;` のように空行の後で comment が無い段落、約 90 箇所。buffer.c 7、undo.c 12、terminal.c 9 など）。
  - §6 条件の中で関数（マクロ）を呼ぶ形: `ex.c` の `name_is()` の else-if 6 本を「`is_command = name_is(...)` と `if (is_command)` と `return`」の並びに、`file.c` の `S_ISREG`（2 箇所）・`S_ISLNK`（1 箇所）を変数に取ってから判定する形に。動作は同じ（`name_is` は副作用が無い）。
  - §14 の goto: `command.c` の `command_join` の `goto fail` 3 つは、その場で `report_error` と `return` に。`file.c` の `write_replacing` の `goto fail` 7 つは、書き込み・所有者・mode・fsync・close を `write_temporary()` に分け（失敗でも descriptor は関数が閉じる）、`write_replacing` が失敗で一時 file を `unlink` する形に。順序・error の値は同じ。
  - §11 関数の最後を成功の return に: `search.c` の `vi_search_find` を、`matched` を立てて loop を抜け、loop の後で「見つからない」を返し最後に成功を返す形に。
  - §8 呼び出しを分ける時は引数を 1 行に 1 つ: `ex.c`・`file.c` の `snprintf(...)` の 5 箇所。
  - §10 comment: 成功の return の comment（`The total.` → `Reports the bytes the group keeps.`、`The change fits.` → `Succeeded: ...`）、case の最初の `if`・入れ子の判断の comment（`motion()` の j・k・`operator()`・`command_replace` など）。
- 読んで確かめて直す所が無かった規則: §2（file の順序、型・file scope 変数の comment）、§3（forward declaration は 1 行、公開関数の comment）、§4（宣言は関数の先頭、`for` の中の宣言なし、initializer に呼び出しなし）、§7（loop・switch の comment、`case` の同じ行に文なし）、§9（確保と検査は 1 つずつ）、§12（試験用の環境変数なし）、§13（著作権の header）。
- `tty-csi.c`・`tty-csi.h`・`tty-csi-host-test.c`・`tty.c` の WS228 の変更: 読んだ結果、直す所は無かった（機械の検査 0 件。`tty_console_csi` の `case` ごとの comment、`tty_console_attributed_byte` の複数 line の comment、`tty_render` の loop の comment、`tty_csi_feed` の最後が成功の return、を確認）。
- p001 の「if ごとの注釈」: 空行の後の段落の頭の comment は上のとおり全部付けた。`case` の直後・`{` の直後の最初の `if` で、囲んでいる comment が別の判断を説明している物（上の §10）にも comment を足した。**直さなかった物**: 1 つの comment が段落の中の連続する guard の `if`（例 `delete_text` の 2 つの範囲検査）や、loop の body が 1 つの `if` で loop の comment がその目的を言っている物（例 `buffer.c` の newline を数える loop）は、§5 の「1 つの comment が chain を導く」に従い、各 `if` に個別の comment は付けていない。

## 直さなかった物・残り

- **clang-format**: `clang-format-19` は `plan/coding-style.md` §3（forward declaration は 1 行、定義は引数を 1 行に 1 つ）と衝突する（vi の全 .c が違反と出る）ので適用していない。規約の全文を優先した（`git diff --check` は通る）。
- **`vi.h` の `UNUSED_PARAMETER` マクロ**: 繰り返しの文を揃えるための macro ではなく、規約 §4 が使う名前そのもの（他の file と同じ）なので残した。
- **欠陥に見える所**: 見つからなかった（動作は変えていない）。
- 他の WS の code の既存の違反: `src/kern/tty.c` の WS228 以外の範囲は読んでいない（所有者の WS の見直しで扱う）。
- 残りの file: 無し。

## 確かめ

- 機械の検査: `python3 plan/tools/style-check.py userland/base/vi/*.c src/kern/tty-csi.c tests/tty-csi-host-test.c` → 0 件（`goto` を除いた後）。
- build（amd64、warning 0）: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk ZEDBSD_USER_PROGRAMS=vi -o <worktree>/build/amd64/sysroot/.zedbsd-sysroot-complete build/amd64/bin/vi`（`-Wall -Wextra -Werror` で全 file）。kernel: `build/amd64/vmunix`（`tty.c`・`tty-csi.c` は変えていない。`make ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/vmunix` が完了し `kernel include check: PASS`・`amd64 vmunix check: PASS`、-Werror 付き）。
- host 試験: `python3 plan/ws228/tests/run-host-vi.py` → `host-vi: ok (44 cases)`（5 回、ASan・UBSan 付きも 1 回 PASS）。1 回だけ `screen-status` の画面の読み取りが FAIL した（build が並行して走っていた時。次の実行から PASS。判定は pty の画面を読む時刻に依る。直した code の欠陥ではない）。
- `cc -std=c99 -Wall -Wextra -Werror -Isrc src/kern/tty-csi.c tests/tty-csi-host-test.c` → `tty-csi-host-test: PASS`。
- `git diff --check`: 通る。
- QEMU・実機: 未実施（この Phase は書き方だけ。vi の guest での確認は p002 の T1）。

## commit

6f2c76a75（buffer・undo・terminal・search・main・ex・file）、320ce997a（screen・command）、5af1a70db（goto の除去・comment・write_temporary）。全部 `WIP`、worktree は `agent/p3`。
