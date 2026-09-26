<!-- awesome-plan project=zedbsd record=ws065p004 -->

# ws065-p004: WS065 の変更の規約の適合

Phase ID: `ws065-p004`
Parent: [WS065](../ws.md)
Status: cleared
Queue: 2026-09-27 ユーザー指示（サブエージェントで WS を完了まで。main session が Queue を記録する）
Disposition: normal

## 範囲

WS065（p001〜p003）で変えた `userland/base/sh/` と `userland/base/common/builtin-standalone.c` を coding-style.md の全文（§14 の checklist）で読んで見直し、`plan/tools/style-check.py` の指摘を 0 にする（2026-09-26 ユーザーの指示で規約は最後）。コードの意味は変えない。

## 受け入れ

style-check の指摘が 0、全文の規約で見直した記録。build（warning 0）、sh の差分試験（host・guest）が前と同じ、expat の configure・make が同じ、boot test。

## 見直したこと（2026-09-27）

比べた元は WS065 の前（`1e867fbf`）と p003 の後（`9e5d76a5`）。WS065 が足した関数はすべて読み直して書き直した。

| file | style-check（WS065 の前 → p003 の後 → 今） | 主な変更 |
| --- | --- | --- |
| `cond.c`（新） | — → 18 → 0 | `switch` の直接の return を `eval_not`・`eval_and`・`eval_or`・`eval_word` に分けた。条件演算子と式で作る真偽を `if` に。`strcmp` の連鎖を名前の表（`integer_compare_names`）と `switch` に。`<`・`>` を `eval_string_order` に。関数の直接の return を保存してから |
| `parser.c` | 0 → 30 → 0 | `[[ ]]` を `parse_cond_command`・`cond_unary_operator`・`cond_ends_after` に分け、`parse_cond_or` の使わない引数を外した。`for (( ))` の読みを `arith_for_opened`・`split_arith_for` に。`$'...'` の escape を `dollar_single_letter`・`dollar_single_number`・`digit_in_base` に分け、種類を `DOLLAR_ESCAPE_*` に。条件の中の呼び出し・閉じ括弧の後の段落・条件演算子 |
| `expand.c` | 0 → 23 → 0 | `${v:o:l}` を `struct substring` と `substring_positionals`・`substring_value`・`substring_split` に、置換を `struct pattern_place` と `longest_match`・`append_replacement`・`transform_words`・`transform_positionals`・`changed_case` に、`${!v}` の判定を `brace_is_indirect` に、locale の判定を `codeset_is_utf8`・`utf8_at` に分けた。`transform_one` の使わない引数を外した |
| `exec.c` | 39 → 52 → 0 | WS065 の分: `eval_arith_part`、`process_substitution_child`（noreturn）・`process_substitution_remember`、`builtin_prefix`。file の先頭の順（macro → 型 → 変数）を直し、`word_list` の説明が別の struct の上に離れていたのを戻し、file 変数に 1 つずつ説明を付けた。残りの 39 件は WS064（ws064-p004 の posix_spawn・command substitution・unset の試し）のコードで、[ws064-p003](../../ws064/phase003/phase.md) の分としてここで一緒に 0 にした |
| `cd.c` | 0 → 22 → 0 | `pushd` を `pushd_directory`・`pushd_swap`・`pushd_rotate` に、`dirs` の option を `dirs_options`・`dirs_letters`（`DIRS_*`）に、`is_position_word` を共通に。file 変数に説明 |
| `command.c` | 0 → 9 → 0 | `declare` を `declare_letters`・`declare_print`・`declare_all_functions`・`is_option_word` に、`.` の operand を `dot_parameters_replace` に。`DECLARE_*` を file の先頭の macro へ。分けた呼び出しは 1 行に 1 引数 |
| `redir.c` | 0 → 10 → 0 | `n>&m-` を `apply_move` に、here-string を `herestring_descriptor` に |
| `printf.c` | 0 → 8 → 0 | `-v` の読みを `option_variable` に。file 変数に 1 つずつ説明。`%q` の組み立て |
| `arithmetic.c` | 0 → 4 → 0 | `++`・`--` の判定を `prefix_step`・`postfix_step` に。`step_variable` は `op` を取る |
| `vars.c` | 0 → 4 → 0 | 属性の合成、`declare -p` の引用 |
| `test.c`・`options.c`・`input.c` | 0 → 0 → 0 | 直接の return、3 つ以上の節の条件、protocol の counter（`sh_parameters_generation`）の説明 |
| `shell.h` | — | 余分な空行 |
| `builtin-standalone.c` | 0 → 0 → 0 | 公開関数の説明の comment、`sh_realloc` の段落 |

規約の読み直しで見つけた、意味の変わらない誤りの訂正: `substring_bounds` の説明が「引用も」と書いていた（実装は括弧と `?` だけ）、`declare -l -u` の説明が「後が勝つ（bash）」と書いていた（実装は `-u` が残る。host の bash 5 は両方を落とす。振る舞いは変えていない）。

## 検証（2026-09-27）

| 確認 | 結果 |
| --- | --- |
| style-check（`userland/base/sh/*.[ch]`、`builtin-standalone.c`） | 0 |
| build（lean の amd64 image、`-Werror`、`plan/ws045/tests/config-amd64-base.mk`） | warning 0、image 32 秒 |
| host の sh の差分試験（`sh-diff.py`、1458 件） | 1438/1458。変更前の sh（`9e5d76a5`）は 1434/1458。差の 4 件は変更に関係しない: `$0` の 3 件は試験の binary の名前に依存（前の sh を同じ名前にすると 7/7）、`noclobber on &> >` は順序に依存（既知）。bash を参照にする case 32/32 |
| guest の sh の差分試験（`guest-batches.sh` に `GUEST_SH=` を足し、main の guest image の複写の `/bin/sh` を差し替え、NVMe、8 GiB 4 vCPU） | 変更前の sh 1413/1458、変更後 1413/1458。失敗の一覧が同一 |
| expat の configure・`make -j4`・`tests/runtests`（guest、`guest-expat.sh`、同じ方法で `/bin/sh` を差し替え） | configure status 0、生成物 9 file（`config.status`・`Makefile`×4・`expat_config.h`・`libtool`・`expat.pc`・`run.sh`）の cksum が変更前の sh と同一、`make -j4` status 0、`tests/runtests` 4932/4932 |
| boot test（`build/ws065/image`、`BOOT_MODE=uefi-nvme`） | PASS（`build/boot-test-ws065p004/login.png`） |

時間の測定は未実施（他の agent が機械を使っている。受け入れに時間は無い）。実機は未実施。

### 足した道具（`plan/tools/sh/`）

- `build-guest-sh.sh`: この tree の sh を、既存の guest image の build の `libc.so` に対する動的な program として作る（image を作り直さずに guest で試す）。ws045 の `build-guest-utils.sh` から。
- `guest-batches.sh` の `GUEST_SH=FILE`: 起動のたびに guest の複写の `/bin/sh` をその sh に差し替える。
- `guest-expat.sh SH`: guest で expat の configure・`make -j4`・`tests/runtests` をその sh で走らせ、各段の status と configure の生成物の cksum を出す（2 つの sh の比較用）。

## 結果（2026-09-27、cleared）

WS065 の変更は規約の全文に合い、style-check 0。sh の振る舞いは host・guest の差分試験と expat の configure の生成物で変わらないことを確かめた。
