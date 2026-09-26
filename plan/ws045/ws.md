<!-- awesome-plan project=zedbsd record=ws045 -->

# WS045: base の text utility の GNU 拡張（sed・awk・grep ほか）

<!-- awesome-plan-current:start -->
Status: incomplete（p001〜p009 cleared。WS の受け入れは下の「判断が要る点」と main への merge を待つ）
Primary Milestone: MG002
Related Milestones: MG006
Objectives: O1, O2
Parent: [Master](../master.md)
Queue: なし（2026-09-27 ユーザー指示のサブエージェントが worktree で実行。main の session が merge する）
Resume point: 「判断が要る点」のユーザーの判断、main の session の merge と、amd64 以外の image の build・boot test（未実施）
<!-- awesome-plan-current:end -->

## 目標

2026-09-24 ユーザー指示: 「sed, awk, grepなどは、GNU拡張も実装したいので、まずPOSIXの範囲で実装したあと、あとでGNU拡張も少しずつ使えるようにしていきましょう。」

WS043 で POSIX の範囲に作り直した base の utility に、GNU の拡張を少しずつ足し、GNU を前提にした script（package の build、
configure、利用者の script）がそのまま動くようにする。

2026-09-27 ユーザー指示（サブエージェントの起動）: 「さらに4つのサブエージェントを起動して、依存関係が満たされているWSに取り組んでください。WS049, WS045, WS048, WS001あたりがいいと思います。…なるべく長く実行できるといいですね。」
WS001（POSIX 準拠）が並行して utility と libc の POSIX の振る舞いを扱うので、この WS は GNU の拡張に限り、POSIX の振る舞いを書き換えない。

## 方針

- 実際の script で使われる順に入れる。使われ方は ws045-p001 の調査（[survey](phase001/phase.md)）で確かめた。
- 出荷する utility（各 config の `ZEDBSD_USER_PROGRAMS`）だけを対象にする。出荷していない `mktemp`・`install`・`base64` は足さない（下の「判断が要る点」）。
- regex の GNU 拡張（BRE の `\+`・`\?`・`\|`、`\w`・`\s`・`\b`・`\<`・`\>`）は libc の TRE（`src/libc/regex`）が既に持つ。utility の側は
  regcomp に渡す前の変換（bracket の中の `\t`・`\n` など）と、regex の外の拡張を担う。
- 差分試験: `plan/tools/utils/util-diff.py --cases plan/ws045/tests/cases --gnu`（GNU の道具を POSIXLY_CORRECT 無しで比べる）。
  host の build（`plan/tools/utils/build-host-utils.sh`）は ws045-p001 から zedBSD の TRE を link する（guest と同じ regex）。
  WS043 の POSIX の差分試験（`cases/`、POSIXLY_CORRECT あり）も毎 Phase 全件を流す。
- 規約（plan/coding-style.md）の全文を適用する。`python3 plan/tools/style-check.py` で触った file の findings を増やさない（現在 0）。

## Phase 一覧

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [ws045-p001](phase001/phase.md) | 調査（実際の script の GNU 拡張の使われ方）と差分試験の土台（TRE の host build、GNU の case） | cleared | — |
| [ws045-p002](phase002/phase.md) | `grep`: `-w`・`-o`・`-A`/`-B`/`-C`・`-r`/`-R`・`-h`/`-H`・`-L`・`-m`・`--include`/`--exclude`・long option・`--color`・`-Z`/`-z`・binary の扱い | cleared（GNU の case 98/98） | p001 |
| [ws045-p003](phase003/phase.md) | `sed` の option: `-i`・`-s`・`-z`・long option・`-u`・`-l`・`--posix`・`--follow-symlinks` | cleared（option の case 全件） | p001 |
| [ws045-p004](phase004/phase.md) | `sed` の script: bracket の中の escape、`\U`・`\L`・`\u`・`\l`・`\E`、`0,/re/`・`first~step`・`addr,+N`・`addr,~N`、`M`、`Q`・`T`・`F`・`z`・`W`・`R`・`e`・`v`、`l N` | cleared（sed の GNU の case 174/174） | p003 |
| [ws045-p005](phase005/phase.md) | build script が使う他の utility: `sort -V`/`-h`、`head`/`tail` の負の数と long option、`cmp -i`、`touch --reference`/`-d`、`date -d`/`-r`、`find -maxdepth` ほか、`readlink -f`/`-e`、`stat --format`、`/bin/echo -e`、`expr` の keyword | cleared（p005 の case 全件。`xargs` は範囲外: WS001 #152） | p001 |
| [ws045-p006](phase006/phase.md) | `awk`: gawk の拡張（`gensub`、`**`、`func`、`\x`、`systime`/`strftime`、RS の regex と `RT`、`match` の配列、`and` ほか、`--version`・`-e`・long option、`system` の値） | cleared（gawk の case 58/58。IGNORECASE・switch・BEGINFILE は範囲外） | p001 |
| [ws045-p007](phase007/phase.md) | file utility の long option と小さな拡張（`cp -v`/`-t`、`mv -v`/`-n`、`rm -v`、`mkdir -v`、`ln -n`/`-r`、`basename -s`/`-a`、`cut --complement` ほか） | cleared（GNU の case 全 517 件） | p001 |
| [ws045-p008](phase008/phase.md) | 実際の script と guest の回帰（GNU の case を guest で、configure-diff を拡張を使う package で） | cleared（guest の GNU の case 515/515、POSIX 492/492、configure-diff 7 package same、amd64 の boot test PASS。amd64 以外の image と実機は未実施） | p002〜p007 |
| [ws045-p009](phase009/phase.md) | 全文の規約確認（WS の全 source 変更） | cleared（新規・書き直しの file 0、legacy の file は全て減るか同じ、足した行の findings 0、3 architecture で warning 0） | p002〜p008 |

2026-09-24 追記（ws042-p006）: guest の sh の試験で要ると分かった GNU 拡張: `grep -o`（`egrep -o` を含む）、`tail --bytes`（長い option）、`echo -e`（`/bin/echo`）。 ws042-p012: `grep -A`（oils の `set | grep -A1`）。→ p002・p005 に入れた。

## 判断が要る点

- `dirname` の複数の operand（GNU の拡張）は、WS001 の試験 `plan/ws001/tests/dirname-test.sh` が「dirname accepted too many operands」を失敗としているため入れていない。
  GNU に合わせるか（WS001 の試験を直す）、POSIX の厳しさを保つかはユーザーの判断。

- 出荷していない utility: `mktemp`（30 package の configure が `mktemp -d` を試す。無ければ自前の fallback に落ちる）、`install`（autoconf は `install-sh` に落ちる）、`base64`。
  足すかはユーザーの判断（この WS の範囲外。F-NNN の候補）。

- `xargs` は POSIX の option も持たない（WS001 の台帳 #152）。GNU の `-0`・`-r`・`-d` はその後に足す。automake の `am__xargs_n` は fallback に落ちる。
  WS001 の後にこの WS（または新しい WS）で扱うかはユーザーの判断。

## 記録

- 調査の道具: [tests/survey.py](tests/survey.py)。GNU の case: [tests/cases](tests/cases/)。
