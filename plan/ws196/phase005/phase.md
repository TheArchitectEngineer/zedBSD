<!-- awesome-plan project=zedbsd record=ws196-p005 -->
# ws196-p005: 規約の全文の見直し（WS196 が変えた C）

Status: cleared（2026-10-14 Q1: 規約の全文の見直し、build warning 0・host 試験 4 本 PASS、見直しの後の tree で T1-603 PASS）
Disposition: normal
Parent: [WS196](../ws.md)
依存: p004 の結果（コードの書き方だけの変更で動作は変えていないので、p004 の試験は見直し前後で同じ。T1 が見直し前の tree を流した場合、見直しの差分の再試験は Q1 が決める）

## 範囲

`git diff d7e38eeae..main` で WS196 が変えた C: `userland/base/account-admin/{main.c,edit.c,edit.h}`、`userland/base/common/account.c`
（lock の部分）、`userland/desktop/sessiond/main.c`（autologin の部分）、`userland/base/user-control/{front.c,front.h}`、
`userland/base/{useradd,usermod,userdel}/main.c`。全 file を `plan/coding-style.md` の全文（890 行）と照らして読んだ。
動作・API・ABI・計算の順序・error の値は変えていない。

## 機械の検査

- `python3 plan/tools/style-check.py <10 files> --summary` — 見直し前も後も total 0。
- `clang-format --dry-run` — 10 file とも 0。`git diff --check` — 0。

## 直した書き方（人の目で見つけた物）

| 規則（節） | 直し | 場所 |
| --- | --- | --- |
| 引数の中の入れ子の呼び出しを名前の付いた変数に（8） | `strerror(...)` を `message` に（13 か所）、`dirfd(...)` を `directory_descriptor`・`skeleton_descriptor` に（`fstatat`・`openat`・`unlinkat`・再帰の呼び出し、5 か所）、`admin_operation_word`・`admin_reason_word` を `operation_word`・`reason_word`・`word` に、`strlen(...)` を `password_length`・`line_length`・`value_length` に | `account-admin/main.c`（`main`・`admin_check_caller`・`admin_apply`・`admin_files_open`・`admin_write`・`admin_highest_in_directory`・`admin_passkey_forget`・`admin_after_work`・`admin_copy_skeleton`・`admin_remove_home`・`admin_remove_tree`・`admin_spool_forget`・`admin_autologin_forget`・`admin_answer`・`admin_system_language`）、`account-admin/edit.c`（`admin_line_append`・`edit_root_value`）、`user-control/front.c`（`user_front_send`） |
| 1 つずつ割り当てて 1 つずつ検査する（9） | `admin_passkey_forget` の 2 つの `malloc` を続けて 1 回で検査していたのを、`text` の割り当てと検査、次に `output` の割り当てと検査にした（`malloc` の順序・`ENOMEM` の値・解放の順序は同じ） | `account-admin/main.c` |
| 関数を直接返さない（11） | `admin_number_field` の `return edit_number(...)` を `number = edit_number(...); return number;` に | `account-admin/edit.c` |
| 関数は成功の return で終わる（11） | `account_files_lock` が失敗の `return EBUSY` で終わっていたのを、取れたかの `taken` を持って loop を `break` し、取れなかった時の `EBUSY` を先に、成功の（`account_lock_descriptor` を残す）return を最後にした（試行回数・待ち・errno の扱いは同じ） | `common/account.c` |
| 3 つ以上の clause は行を分ける（6） | `admin_name_valid`（4 clause）・`admin_display_valid`・`admin_line_append`・`admin_dot` の `..` の判定 | `account-admin/edit.c`・`main.c` |
| return の前の comment（11）・comment の訂正（10） | `edit_put` の成功の return に comment。`admin_language_valid` の失敗側の return の comment が "Succeeded: not one of them." だったのを訂正 | `account-admin/edit.c` |
| comment の行の長さ（10） | `account.c` の先頭の comment の 1 行が長すぎたのを折り返し | `common/account.c` |

件数: 入れ子の呼び出し 約 28 か所、割り当て 1 か所、直接の return 1、成功の return で終わる形 1、clause の分け 5、comment 3。

## 読んで直さなかった所（規約に合う、または動作を変えるので）

- `useradd`・`usermod`・`userdel` の `main.c` と `front.c`・`front.h`: 規約に合っていた（1 段落に判断 1 つとその comment、
  条件の中で関数を呼ばない、成功の return で終わる、引数の中の入れ子の呼び出しは `front.c` の `strerror` 1 か所のみで直した）。
- `sessiond/main.c` の `main_autologin_allowed`: 規約に合っていた（3 clause の条件は既に行を分けてある）。
- `account-admin/main.c` の `main` の `(void)argc; (void)argv;`: userland の `UNUSED_PARAMETER` は共通の header に無い（bluetoothd が局所に定義）ので、そのままにした。
- 短い comment（`/* The file. */`・`/* Set. */` など）: 禁止の「文を言い直すだけ」の型ではなく、直前の段落の続きとして読めるので残した。
- 連続する `if` の短い guard（`if (id >= 0) return ...; id = ...; if (id >= 0) return ...;`）: 1 つの comment が束ねる guard の並びとして規約 6 の例に合うので残した。

## 欠陥に見えた所（直していない、動作を変えるので）

- なし。`admin_make_home` の `home = openat(...); (void)close(base); if (home < 0) return errno;` は `close` の後に `errno` を読む形だが、
  `close` が成功した時に `errno` は変わらないので実害は無い。（記録のみ）

## 他の WS の既存の違反

見直しの範囲は WS196 の変えた行を含む上の file だけで、他の WS の行（`login/verify.c`・`passkey/` など）は読んでいない。

## 確かめ（2026-10-14、worktree `/home/awe/zedBSD-worktrees/p2`、branch agent/p2-ws215、main f2df56634 を ff で取り込んだ上）

| コマンド | 結果 |
| --- | --- |
| `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws196-p005/zed "ZEDBSD_USER_PROGRAMS=useradd usermod userdel account-admin passwd su sudo sessiond" build/ws196-p005/zed/bin/{useradd,usermod,userdel,account-admin,passwd,sessiond}` | rc 0、`-Werror`、warning 0（log `build/ws196-p005/build-zed.log`） |
| `sh plan/ws089/tests/run-host-account-admin.sh` | PASS（95 checks, 0 failed） |
| `sh plan/ws158/tests/run-host-admin-language.sh` | PASS（ok, 13 checks） |
| `sh plan/ws196/tests/account-admin-root-host-test.sh`（内部で sudo） | PASS（74 checks） |
| `sh plan/ws196/tests/user-commands-host-test.sh`（root でなく走らせる。内部で sudo。root で外から走らせると not-root の確認が落ちる） | PASS（148 checks） |

root の作った tmp: `build/tmp/ws196-account-admin.*`（4）・`build/tmp/ws196-user-commands.*`（2）— Q1 が片付ける。

未実施: QEMU・実機の確認（p004 の T1 に依頼済みの内容は見直しで変えていない）。
