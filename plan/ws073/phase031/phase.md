<!-- awesome-plan project=zedbsd record=ws073-p031 -->

# ws073-p031: less の Ctrl-F・Ctrl-B のページ送り（BUG-104）

Status: cleared（2026-09-29）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-104](../../bugs/BUG-104.md)
Queue: main が WS073 のサブエージェント（`wt/ws073`）に割り当て（2026-09-29、ユーザーの要望「lessコマンドで、Ctrl-F, Ctrl-Bでページ送りできるようにしてほしいです。」）。Queue の ID は main が記録する

## 範囲と受け入れ

- 共通の pager（`userland/base/common/pager.c`）の less の style で、Ctrl-F（1 画面進む）と Ctrl-B（1 画面戻る）を扱う。
- more の振る舞いは変えない。
- 受け入れ: pty で less に鍵を送り、位置が 1 画面ずつ進み・戻る。more は従来どおり。

## 設計

`interactive()` の鍵の分岐の前で、less の style に限り Ctrl-F（0x06）と `f` を space に、Ctrl-B（0x02）を `b` に読み替える
（`PAGER_KEY_SCREEN_FORWARD`・`PAGER_KEY_SCREEN_BACK`）。ticket は「f・space と同じ」と書いていたが、以前の pager には `f` が無かったので、
GNU less と同じく less に `f` も足した（more には足していない）。端末は ICANON・ECHO だけを切る raw で、kernel の tty の既定の制御文字に
0x02・0x06 は無い（`src/kern/tty.c` の既定の `c_cc`）ので、鍵はそのまま pager に届く。

## 手順と結果

- style: `python3 plan/ws073/tests/style-diff.py userland/base/common/pager.c` findings 0、`git diff --check` PASS、
  host の `cc -std=c11 -Wall -Wextra -pedantic -Werror -c userland/base/common/pager.c` PASS。
- host 試験 [tests/pager-keys.py](../tests/pager-keys.py)（host の cc で build した less・more を 24 行の pty で 200 行の file に）:
  - 修正後: less `[1, 24, 47, 24, 47, 24, 47, 24, 1]`（^F ^F ^B f b space ^B ^B の後の先頭の行）、more `[23, 23, 23, 46, 23]`（^F と f は動かない、space・b は従来どおり）→ `PAGER-KEYS:PASS`。
  - 修正前の pager.c（`git show HEAD:`、build/ws073-p031/old）: less `[1, 1, 1, 1, 1, 1, 24, 24, 24]` → FAIL（^F・^B・f が効かない、症状の再現）。
- build: `sh plan/ws073/tests/build-image-noclang.sh build/amd64`（main c8076ed5 を merge した tree）`check-amd64-native-image: OK`、pager.c の warning 0。
- QEMU の guest（`tests/g.sh start build/amd64/hdd-image.img`、`python3 plan/ws073/tests/pager-keys.py --ssh PORT` で guest の /bin/less・/bin/more を `ssh -tt` で）:
  less・more とも host と同じ位置の列、`PAGER-KEYS:PASS`。
- boot test: `OUTPUT=build/ws073-p031/boot-test plan/tools/boot-test.sh build/amd64/hdd-image.img` PASS（`build/ws073-p031/boot-test/login.png`）。
- 未実施: 実機、graphical の terminal（Keiland の terminal の中での鍵の送り）。

## Resume point

完了。WS073 の次は ws073-p030（BUG-051）の再開。
