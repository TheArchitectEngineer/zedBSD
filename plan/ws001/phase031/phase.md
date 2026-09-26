# ws001-p031: guest の回帰と台帳の照合

Status: cleared（2026-09-27）
Parent: [WS001](../ws.md)
Queue: なし（2026-09-27 ユーザー指示のサブエージェントの作業。main session が merge する）

## 目的

p024〜p030 の全 case と WS043 の case を amd64 guest でまとめて流し、起動を確かめ、台帳（ws.md §12）を WS042・WS043 の結果と照合する。

## 受け入れ条件

1. host: `plan/tools/utils/util-diff.py --bin build/ws001/bin` の全 case、`pinned-cases.py`、`tty-host-test.py` が全件一致。
2. amd64 guest（lean image）で全 case（root で成り立たない `cp-user.sh` を除く）と期待値の case が一致。
3. `plan/tools/boot-test.sh` で lean image が login prompt まで起動する。
4. §12 で WS042・WS043 が作り直した utility の行を、その証拠で更新する。q136 の観察を確かめる。

## 記録（2026-09-27）

| 確認 | 結果 |
| --- | --- |
| host の全 case（WS043 の case を含む、WS001 の build を PATH の先頭に） | 945/945 |
| 期待値の case・端末の case | 5/5・10/10 |
| amd64 guest `DUMP=1 sh plan/ws001/tests/guest-run.sh build/ws001/guest-p031.out <cp-user 以外の 39 の case file> pinned` | 948/948 |
| `OUTPUT=build/ws001/boot-test plan/tools/boot-test.sh build/ws001-guest/hdd-image.img` | PASS（`build/ws001/boot-test/login.png`、画面に `login:`） |
| q136 の観察（見つからない command の次の行の `$?`）`python3 plan/ws001/tests/status-after-not-found.py <serial socket>` | 127（見つからない）、126（実行できない）。現在の sh では再現しない |
| 実機 | 未実施 |

### 台帳の照合

- WS043 が作り直した 21 行（sed・awk・grep・cut・wc・head・tail・sort・uniq・tr・od・paste・join・rm・ln・touch・printf・echo・test・true・false）と、
  WS042 が作り直した shell と builtin の 10 行（sh・cd・read・umask・command・type・wait・getopts・alias・unalias）を、
  `implemented-unreviewed (WS043)`・`(WS042)` と、その差分試験の結果と残り（locale・多 byte・GNU 拡張は WS045）に書き換えた。
- p024〜p030 で 29 行を `implemented-unreviewed (ws001-p02x/p030)` にした。
- 結果: §12 の 111 行のうち implemented-unreviewed 67、P1 27、P2 17（2026-08-31 は P1 73・P2 38）。
- 残りの P1: admin・bc・cflow・compress・cxref・delta・df・du・ed・file・get・iconv・locale・localedef・ls・m4・nm・patch・pax・prs・ps・rmdel・sccs・stty・tput・val・who。
- 残りの P2: ar・basename・cat・cksum・dd・find・fuser・ipcrm・ipcs・mesg・newgrp・sact・tabs・uncompress・unget・what・zcat。
