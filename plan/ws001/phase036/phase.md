# ws001-p036: stty

Status: cleared（2026-09-27）
Parent: [WS001](../ws.md)
Queue: なし（2026-09-27 ユーザー指示のサブエージェントの作業。main session が merge する）

## 目的

台帳（ws.md §12）の #116 stty を XCU（POSIX.1-2024）の要求に合わせ、規約の全文で書き直す。
以前の stty は echo・icanon・isig・ixon・opost・raw だけを扱い、`-g`・速度・制御文字・窓の大きさが無かった。

## 範囲と結果

| 対象 | 変更 |
| --- | --- |
| `userland/base/stty/main.c` | 書き直し。`-a`（速度、窓の大きさ、制御文字と min・time、制御・入力・出力・局所の mode を 1 行ずつ）、`-g`（16 進を `:` で区切り、同じ stty が 1 つの operand として読み戻す）。operand: 全 mode とその否定、`cs5`〜`cs8`、遅延の組（`nl0`/`nl1`、`cr0`〜`cr3`、`tab0`〜`tab3`、`bs0`/`bs1`、`vt0`/`vt1`、`ff0`/`ff1`）、速度（数、`ispeed`・`ospeed`）、制御文字（1 文字、`^X`、`^?`、`^-`・`undef`）、`min`・`time`、組み合わせ（`evenp`/`parity`、`oddp`、`-parity`/`-evenp`/`-oddp`、`raw`、`-raw`/`cooked`、`nl`/`-nl`、`ek`、`sane`、`tabs`/`-tabs`、`hup`/`-hup`）、`-g` の設定、`rows`・`cols`（`columns`）・`size`。全部の operand を読んでからまとめて設定し、読み戻して違えば「unable to perform all requested operations」で状態 1。知らない operand・値の無い operand・悪い値は状態 1 |
| `include/uapi/termios.h` | XSI の `OFILL`・`OFDEL` と遅延の mask と値（`NLDLY` `CRDLY` `TABDLY` `BSDLY` `VTDLY` `FFDLY`）を足した。kernel は出力に詰め物も遅延も入れないので、bit は運ぶだけ（`CSTOPB` と同じ扱い） |
| `include/libc/unistd.h` | POSIX の要る `_POSIX_VDISABLE`（0xff、kernel の `TTY_VDISABLE` と同じ）を足した |

`sane` と `ek` の erase は delete（0x7f、getty が console に設定する値）。kernel の既定（getty の前）は ^H のまま。

## 受け入れ条件と結果（2026-09-27）

| 確認 | 結果 |
| --- | --- |
| host の pty の試験 `python3 plan/ws001/tests/stty-host-test.py`（全 mode の on/off、組の各値と否定の拒否、速度、制御文字・min・time、組み合わせ、`-g` の往復、窓の大きさと `size`、`-a` の内容、zedBSD stty が設定したものを GNU stty が読む、誤り。Linux の pty が拒む設定（parity、cs5〜cs7、`-cread`）は別の pty の GNU stty と状態と結果の設定を比べる） | 89/89 |
| amd64 guest `DUMP=1 sh plan/ws001/tests/guest-run.sh build/ws001/guest-p036.out pinned`（`pinned/guest.sh` の stty: console に `-echo tostop intr ^A ofill tab3 19200 rows 40 cols 100` を設定し `-a`・`size` で確かめ、`-g` と size で元に戻す。kernel が拒む `cs7` は状態 1 で設定は変わらない、知らない operand、端末でない入力） | 30/30 |
| style（`stty/main.c`） | 違反 0、`cc -Wall -Wextra` の warning 0。guest image の build の warning 0 |
| 実機 | 未実施 |

## 見つけたこと

- guest の case は `timeout` が作る背景の process group で走るので、console に `tcsetattr` すると SIGTTOU で止まる。case の中で `trap '' TTOU` とした（POSIX どおり、無視していれば設定は通る）。
- zedBSD の kernel は `CS8` 以外の文字の大きさを EINVAL で拒む（`src/kern/tty.c` `tty_termios_valid`）。`evenp`・`oddp`・`cs7` は stty が状態 1 で報告する。
- Linux の pty は parity・cs5〜cs7・`-cread` を拒み、入出力の速度を分けられない（host の試験はこれらを GNU stty と比べる）。

## 残り

- 既定の出力（operand 無し）は `-a` と同じ（POSIX は「一部」を許す）。
