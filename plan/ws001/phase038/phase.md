# ws001-p038: mktemp・install・base64

Status: cleared（2026-09-27）
Parent: [WS001](../ws.md)
Queue: なし（2026-09-27 ユーザー指示のサブエージェントの作業。main session が merge する）

## 目的

WS045 の未決の 1 件をユーザーが決め、WS001 に割り当てた（coordinator 経由、2026-09-27）:
`mktemp`（GNU: -d, -u, -p/--tmpdir, -t, X の後ろの suffix）、`install`（POSIX 風と GNU: -d, -m, -o, -g, -c, -D, -t, -v, -s は無視か strip）、
`base64`（-d, -w, -i）を base の utility として足し、package の一覧と image に入れ、GNU と比べる util-diff の case を作る。
3 つとも POSIX の utility ではないので、§12 の台帳には行が無い。

## 範囲と結果

| utility | 内容 |
| --- | --- |
| mktemp（新設） | template の末尾（suffix の前）の 3 つ以上の X を乱数の英数字に替え、新しい名前で file（0600）か `-d` で directory（0700）を作り名前を書く。`-u`（名前だけ）、`-q`（作れない失敗を黙る。template の誤りは黙らない、GNU と同じ）、`-p dir`・`--tmpdir[=dir]`・`-t`（`$TMPDIR` と `/tmp` の順も GNU と同じ）、`--suffix`、X で終わらない template の後ろを suffix とする。template 無しは `tmp.XXXXXXXXXX`。乱数は `getentropy` |
| install（新設） | 既存の destination を消して新しい file を作り、内容を写し、owner・group（`-o`・`-g`、名前か数）、mode（既定 0755、`-m` は 8 進か記号、chmod の `mode.c` を共有）、`-p` で時刻。directory の中へ、複数の source、`-t`・`-T`、`-D`（destination の親、`-t` の directory）、`-d`（親を 0755 で作り最後に mode・owner・group）、`-v`、`-C`（同じ内容と mode なら触らない）、`-c`（何もしない）。destination の symbolic link は辿らず置き換える。同じ file は拒む。`-s` は受けて strip しない（base に strip が無い。GNU は strip を走らせる） |
| base64（新設） | RFC 4648 の encode（76 桁で折る、`-w cols`・`-w 0`）と `-d`（改行を飛ばす、`=` の後に続く group も読む、padding の無い 2・3 文字の最後の group を受ける、それ以外は「invalid input」で状態 1）、`-i`（Base64 でない文字を飛ばす）、file operand と `-` |
| `config/ci/config-{amd64,pcat,pc98,intelmac}.mk` | program の一覧の `pwd` の後に `mktemp install base64` を足した（rpi4 の設定は一覧を持たない） |
| `plan/ws001/tests/build-host-ws001.sh` | host の build の一覧に stty・dirname・mktemp・install・base64・who を足し、install に `chmod/mode.c` を足した |

## 受け入れ条件と結果（2026-09-27）

| 確認 | 結果 |
| --- | --- |
| host の差分試験（GNU coreutils と）`mktemp`・`base64`・`install` | 11/11・11/11・13/13 |
| amd64 guest `DUMP=1 sh plan/ws001/tests/guest-run.sh build/ws001/guest-p038.out mktemp base64 install`（lean image に 3 つが入る） | 35/35 |
| style（3 つの `main.c`） | 違反 0、`cc -Wall -Wextra` の warning 0。guest image の build の新しい warning 0 |
| 実機 | 未実施 |

## GNU との違い（意図したもの）

- install `-s` は strip しない（状態 0）。GNU は strip を走らせ、strip できない file で失敗する。case では比べない。
- install `-b`（backup）、`-S`、`--preserve-context`、SELinux の option は無い。
