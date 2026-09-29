<!-- awesome-plan project=zedbsd record=ws086-p003 -->

# ws086-p003: 規約の全文との照合、回帰

Status: cleared
Disposition: normal
Parent: [WS086](../ws.md)
Queue: main が subagent（worktree `wt/ws086`、main 5991a890 に合わせてから）へ依頼（2026-09-29）
Approval: ユーザー（2026-09-29）「lsの結果が1つずつ改行されています。GNU lsと同じにしたいです。」

## 範囲と受け入れ

- WS086 で変えた source（`userland/base/ls/main.c`。書き直しなので file の全体）を [coding-style.md](../../coding-style.md) の全文
  （§1〜§14 の review checklist）と照らし、範囲の中の違反を直す。
- 回帰: host の build（warning 0）、GNU との比較、POSIX の utility の試験（WS001・WS043 の case）、guest の比較、boot test。

## 照合の方法

- 機械的な検査: `python3 plan/tools/style-check.py userland/base/ls/main.c`（照合の前 0 件、直した後 0 件）。
- 目視: file の全体（約 2900 行）を上から読み、checklist の項目ごとに確かめた。

## 見つけて直したもの（commit 4d4a82d5）

- §4・§5（段落）: `main` の `parse_options` の検査と `settle_layout` を別の段落に。`main` の終わりを 1 つの経路にまとめ、`finish_output` の
  結果を直接返さない形に（§11）。`list_directory` の見出しと一覧を別の段落に。`-w` の `width_given`、`list_operands` の見出しの決定と
  一覧のループ、`print_link_target` の矢印、`ls_time` の recent の判定と書式を別の段落に。
- §6（判断）: 同じ Boolean を決める独立の `if` の並び（`settle_layout` の `uses_width`、`shell_class` の `alphanumeric`、`list_operands` の
  `header`）を `else if` の連鎖に。`COLUMNS` の `parsed > 0`・`< 0` を連鎖にし各腕に comment。3 つ以上の節や `&&` と `||` の混在の条件
  （`parse_count` の空白、`read_entries` の `.`・`..`、`compare` の `-t`、`uid_name`・`gid_name`、`quote_shell` の `quoted_too`、`print_separated`、
  `ls_time`）を 1 行 1 節に。
- §9（割り当て）: `list_operands` の 2 つの `calloc` を 1 つずつ割り当てて検査する形に。`copy_string` の失敗を検査していなかった
  （NULL の名前が後で使われる）のを直し、`ls: out of memory` を出してその operand を除く。
- §2（file-scope の変数）: `ls_long_options` の comment に、誰が読み、どれだけ生きるかを書いた。§13: file の説明の段落の折り返しを直した。
- §4（名前）: `prepare_names` の `quoted`（成否の値で、entry の `quoted` と紛らわしい）を `written` に。
- **振る舞いの誤り（照合の途中で見つけた）**: `'` を含む名前の二重引用符の判定で、`#`・`~`（先頭以外）と `{`・`}`（2 文字以上の名前）を
  「二重引用符に入れてよい」と扱い、先頭の `#`・`~` を「入れてはいけない」と扱っていた（GNU の `c_and_shell_quote_compat` と逆）。
  `LS_SHELL_BARE` の分類を足して直した: `it's#x` → `'it'\''s#x'`、`#it's` → `"#it's"`（host の GNU で確かめた）。比較の名前に 7 つ加えた。

## 範囲外として残したもの

- `ls_time` は UTC で書く（GNU は localtime）。旧 ls からで、WS086 の範囲外（Kei の時間帯の扱いに依る）。host の比較は `TZ=UTC` で行う。
- `read_entries` の配列の倍増の大きさの桁あふれの検査が無い（旧 ls から。数十億の entry でしか起きない）。
- p002 の「残る GNU との差」（`-t` の nanosecond、`-h` の切り上げ、`-L` の dangling、message の文）は main が F-055 に記録した。

## 試験

### host

- build: `cc -std=c11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Wno-format-truncation -I. -Iinclude userland/base/ls/main.c userland/base/common/command.c -o build/ws086/host/ls` → warning 0。`git diff --check` → 0。
- [tests/compare-gnu.py](../tests/compare-gnu.py): compared=3362、差 1 件は `/` の `-li` の `/proc` の link の数（host の process の数で
  刻々と変わる。`/proc` を除いて 5 回とも一致）で ls の差ではない。既知の差 48（p002 と同じ）。
- `build-host-utils.sh build/ws086/bin` と `util-diff.py --bin build/ws086/bin` → TOTAL 1080/1080（WS001・WS043 の POSIX の case を含む）。
- `ls-compare.sh`（GNU、`TZ=UTC LC_ALL=C`）: 差は `t/missing` の message、`-L`・`-lL` の dangling、`-t`、`-h`（どれも既知）だけ。

### guest（QEMU、amd64、KVM）

- image: `plan/tools/titlebar/build-menu-image.sh build/amd64`（ls は `-Wall -Wextra -Werror` で compile、log の warning 0）。
- [tests/guest-compare.py](../tests/guest-compare.py) → compared=180 differ=0。guest で `it's#x` を作り `ls` で確かめた（端末の出力は目視せず、
  比較の script に任せた）。
- boot test: `OUTPUT=build/ws086/boot-test-p003 plan/tools/boot-test.sh build/amd64/hdd-image.img` → PASS（`build/ws086/boot-test-p003/login.png`）。
- 実機: 未実施。

## 結果

- cleared。規約の全文との照合で見つけた違反と、1 つの振る舞いの誤りを直し、回帰は全て通った。
- WS086 の Phase はこれで全て cleared。WS の完了（ws.md を完了の形にし、Phase のディレクトリを消し、試験を `plan/tools/` へ移して
  Tools 節に登録する）は main に依頼する（`plan/tools/` と `plan/master.md` は subagent の範囲の外）。
