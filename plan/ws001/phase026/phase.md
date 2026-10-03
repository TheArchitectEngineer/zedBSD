# ws001-p026: id・chown・chgrp・chmod・mkdir・mkfifo・rmdir

Status: cleared（2026-09-27）
Parent: [WS001](../ws.md)
Queue: なし（2026-09-27 ユーザー指示のサブエージェントの作業。main session が merge する）

## 目的

台帳（ws.md §12）の #59 id、#18 chown、#16 chgrp、#17 chmod、#79 mkdir、#80 mkfifo、#106 rmdir を XCU（POSIX.1-2024）の要求に合わせる。
mkdir -m・mkfifo -m は chmod の mode operand を使うので、chmod の記号 mode を作り直して共有する（計画の時点では chmod を含めていなかったが、
mode の共有のため範囲に入れた）。

## 範囲

- mode operand（`userland/base/chmod/mode.c`、chmod・mkdir・mkfifo が共有）: 8 進と記号 mode の全文法（clause の列、who の列、
  1 つの clause の複数の action、`r w x X s t`、permcopy `u g o`）。who の無い action は umask の bit を変えない。
  directory の set-ID bit は `s` を書くか 5 桁以上の 8 進でない限り保つ（GNU と同じ。POSIX は directory の set-ID の扱いを実装に任せる）。
- `chmod [-R] mode file...`: `-w` のような mode を option と区別する、operand の link は辿る、`-R` の中の link は辿らず変えない。
- `mkdir [-p] [-m mode] dir...`: `-m` は a=rwx から計算して umask に関わらず正確に設定、`-p` の親は umask の bit に u+wx を足す、既存の directory は `-p` で可。
- `mkfifo [-m mode] file...`: `-m` は a=rw から、許可 bit 以外（set-ID・sticky）は拒否（GNU と同じ）。
- `rmdir [-p] dir...`: `-p` は pathname の接頭辞を順に消し、最初の失敗で止まる。
- `chown [-h] owner[:group] file...`・`chown -R [-H|-L|-P] ...`・`chgrp` 同様（`userland/base/chown/change.c` を共有）:
  名前か数字、`:group`、`owner:`（login group）、`-h`、`-R` の link の規則（`-P` 既定で link 自身、`-H` は operand、`-L` は全部）。
- `id [user...]`・`-G [-n]`・`-g [-nr]`・`-u [-nr]`: 既定の書式（名前の無い ID は括弧ごと省く）、group の列（実 group・実効 group・補助 group の順、重複なし）、
  user operand は passwd と group の database から（複数の user も受ける。GNU と同じ）。

範囲外: LC_MESSAGES、chown の古い `owner.group` の書き方。

## 受け入れ条件

1. host の差分試験: `plan/tools/utils/cases/{id,chown,chmod,mkdir,mkfifo,rmdir,files}.sh` が全件一致。
2. 7 つの utility の source（`chmod/mode.c`・`chmod/main.c`・`mkdir`・`mkfifo`・`rmdir`・`chown/change.c`・`chown/main.c`・`chgrp`・`id`）が style 違反 0。
3. amd64 guest（root で走る）で同じ case が一致。case は host の user（awe）と guest の root の両方で成り立つように、名前や group を
   その場で `/etc/passwd`・`/etc/group`・`stat -c` から求める。

## 記録

### 試験（2026-09-27）

| 確認 | 結果 |
| --- | --- |
| host の差分試験 | id 16/16、chown 18/18、chmod 19/19、mkdir 15/15、mkfifo 8/8、rmdir 10/10、files 42/42 |
| amd64 guest `DUMP=1 sh plan/ws001/tests/guest-run.sh build/ws001/guest-p026b.out id chown chmod mkdir mkfifo rmdir files` | 128/128（最初は 124/128: case が host の環境に依っていた。`ls -lR` の見出しの違い（ls は #73 で未対応）、root が権限の無い directory を辿れること、group 0 の名前（host は root、guest は wheel）、umask。case を `stat -c %a`・その場の計算・`umask 022` に直した） |
| style（9 file） | 違反 0 |
| 実機 | 未実施 |

### GNU との違い（意図したもの）

- `mkdir -m +t`: GNU は `drwxr-xr-t`（umask が残る）、ここは `drwxrwxrwt`（POSIX の「mode を設定する」どおり）。case から外した。
- `chown -R -H`: traversal の中の link を、GNU は指す先を変え、ここは link 自身を変える（`-P` と同じ。tree の外の file を変えない）。

### 残り

- `ls -n`（数字の owner）が無い（#73 ls の範囲）。
- 権限の失敗（非 root）の case は host だけで確かめた（guest は root）。
