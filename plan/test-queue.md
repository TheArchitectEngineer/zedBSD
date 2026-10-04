<!-- awesome-plan project=zedbsd record=test-queue -->
# 試験のキュー（次に流す試験と手順）

2026-10-04 ユーザー「これらのテストを別な能力の低いセッションで流せるように、手順を記録しておいてください。テストキューのドキュメントを作って、master.mdに、次に実行すべきテストの一覧と手順はこちら、ということをリンクして書いておいてください。」

試験の担当（T1・T2）を 2026-10-04 にラップアップした時点で、予約されたまま流していない試験の一覧と手順。上から順に流す。
流す人は試験だけを行い、source を直さず、FAIL の原因も解析しない（結果と証拠を返すだけ。`.claude/agents/test-runner.md` と同じ）。

## 守ること（AGENTS.md の「検証」から）

- **QEMU は host 全体で同時に 1 つ。** 始める前に `pgrep -af qemu-system` で他に動いていないことを確かめる。
- QEMU の console log・serial log を読んで合否を決めない。判定は各 script の PASS/FAIL、SSH、QMP の PNG で行う。
- 起動の確認は `plan/tools/boot-test.sh` だけ。撮れた PNG はユーザーに見せる。
- 自分の作業の directory（例 `build/tq-NNN/`）だけに書く。共有の `build/` の物（`build/llvm*`・`build/NoctLang`・他の人の `build/…`）を消さない・上書きしない。
- toolchain（`toolchain/`・`userland/base/noct/`・clang・libcxx）を build・変更しない。
- image は `plan/tools/guest/test-image.sh CONFIG BUILD [--file 宛先=元 ...]`（WS の `tests/` の config.mk と個別の file の複写だけ）か、各 WS の `build-*-image.sh` で作る。過去の build の成果を image の入力にしない。
- commit はしない（結果は Q1 に返す。Q1 がこの文書と台帳に書く）。権限の確認で止まったら、別の方法を試さずに止めて報告する。
- 証拠は QEMU と実機を分けて書く。流していない物は「未実施」と書く。

## 結果の書き方

各項目の「結果」の欄に、日時・source の commit・image の path・各 script の PASS/FAIL と所要時間・証拠の path を書いて Q1 に返す（Q1 が記入する）。

## キュー

| # | 対象 | 前提 | 優先 | 結果 |
| --- | --- | --- | --- | --- |
| TQ-1 | ws131-p013・p014（libkeiui・libkeiland の名前の改名）の 3 OS の回帰 | source は **agent/p2 40b242a**（main には未 merge。PASS の後に Q1 が merge）。p014 は途中（phase014 の Resume）で、P2 が T1 に送った依頼は T1 の終了の後に届いたので未実行 | 高 | 未実施 |
| TQ-2 | WS127 Files: `files-open.sh`（mouse・always・info）（元 T1-008） | なし | 中 | 未実施 |
| TQ-3 | ws131-p007 の残り: demo-s8-s9（元 T1-044 の 3） | なし（TQ-1 の A に含まれるので、TQ-1 を流したら不要） | 低 | 未実施 |
| TQ-4 | WS005 の venus-session-check の再実施（元 T1-007、image の作り方が原因の候補） | WS005 を再開するとき | 低 | 未実施 |
| TQ-5 | BUG-053 の swaphog 1300 MiB（元 T1-049、負荷試験） | **ユーザーの確認を取ってから夜間に**（AGENTS.md: 負荷試験は別枠） | 低 | 未実施 |

取り下げた物（流さない）: T1-009 の C9 の 2 回目（BUG-127 は「再現しなければ close」）、T1-050（BUG-033 は resolved）、T2-005（BUG-099 は close）。

---

## TQ-1: ws131-p013・p014 の 3 OS の回帰

ws131-p012 の試験（T2-027、2026-10-04 PASS）と同じ組。source は agent/p2 40b242a を自分の directory に出して、そこで build する（main の checkout を切り替えない）:

```sh
mkdir -p build/tq-1/src && git archive 40b242a | tar -x -C build/tq-1/src
cd build/tq-1/src   # 以下の command はこの tree の中で流す（共有の toolchain は main の build/ を読み取り専用で使う。T2-027 と同じ）
```

### A. zedBSD（QEMU）

```sh
OUT=build/tq-1; mkdir -p $OUT
# 1. image（それぞれ 1〜4 分）
plan/tools/guest/test-image.sh plan/ws079/tests/config-amd64-demo.mk      $OUT/demo
plan/tools/guest/test-image.sh plan/ws090/tests/config-amd64-textinput.mk $OUT/ti
plan/tools/files/build-files-image.sh $OUT/files
# 2. 試験（1 つずつ。QEMU は同時に 1 つ）
plan/tools/boot-test.sh $OUT/demo/hdd-image.img                        # login の PNG をユーザーに見せる
plan/ws090/tests/textinput-p013.sh $OUT/ti/hdd-image.img $OUT/textinput-p013
sh plan/ws079/tests/run-pdf-render.sh && sh plan/ws079/tests/run-pdfviewer-host.sh   # viewers-p008 の前提（host、password.pdf を作る）
plan/ws090/tests/viewers-p008.sh $OUT/demo/hdd-image.img $OUT/viewers-p008 $OUT/demo
plan/ws079/tests/demo-s8-s9.sh $OUT/demo/hdd-image.img $OUT/demo-s8-s9
plan/tools/files/files-regress.sh $OUT/files-regress                   # 約 10 分、14 本
# 3. 旧 library が無いこと
ls $OUT/*/rootfs/lib | grep -c libkeiui    # 0 であること
```

合格: 全て PASS、`libkeiui` 0。各 script の先頭の注記に image・BIN の既定があるので、引数の形が違えば注記に従う。

### B. Linux（Debian 13 の QEMU+KVM guest）

手順の詳細は [keiland-linux/README.md](tools/keiland-linux/README.md)。

```sh
make keiland-linux 2>&1 | tee build/tq-1/linux-build.log      # exit 0、"warning:" が 0
sh plan/tools/keiland-linux/elf-check.sh                        # PASS
timeout 600 sh plan/tools/keiland-linux/build-guest.sh '' gdm   # 既存の image を使う。--force は使わない
timeout 200 sh plan/tools/keiland-linux/guest.sh start
timeout 180 sh plan/tools/keiland-linux/install-guest.sh build/keiland-linux/stage
```

guest で gdm を止め、compositor を `KEILAND_SEAT=direct` で起動し（README の「操作」）、textedit・imageview・pdfviewer・notes・terminal・files・settings・kuidemo・monitor を順に起動して、それぞれ `guest.sh screenshot` で窓の PNG を撮る。`--socket` には絶対 path を渡す（T2-027 で相対 path にして compositor が止まった）。最後に `guest.sh stop`。

合格: build の warning 0、elf-check PASS、`ZWL READY` が出て `ZWL ERROR` が 0、9 つの app の窓が PNG に写る、`/opt/keiland/lib` に `libkeiui` が無い。

### C. FreeBSD（QEMU+KVM guest）

手順の詳細は [keiland-freebsd/README.md](tools/keiland-freebsd/README.md)。

```sh
sh plan/tools/keiland-freebsd/backend-test.sh build/tq-1/freebsd     # guest の起動・停止も script が行う
cat build/tq-1/freebsd/summary.txt
```

合格: summary の 9 行が全て PASS（build・install・audit・host-*・sync-rejected・dmabuf-export-rejected）。guest は止めると build と stage が消えるので、audit だけの再実行はできない（やり直すときは backend-test を丸ごと）。

---

## TQ-2: WS127 Files の files-open

```sh
plan/tools/files/build-files-image.sh build/tq-2/files
GUEST_RUNTIME=$PWD/build/tq-2/run BIN=build/tq-2/files plan/tools/files/files-open.sh build/tq-2/out mouse
GUEST_RUNTIME=$PWD/build/tq-2/run BIN=build/tq-2/files plan/tools/files/files-open.sh build/tq-2/out always
GUEST_RUNTIME=$PWD/build/tq-2/run BIN=build/tq-2/files plan/tools/files/files-open.sh build/tq-2/out info
```

（script の先頭の注記では guest が起動済みであることを前提にしている。起動の仕方は `files-regress.sh` の冒頭と同じ。注記に従う。）
合格: 3 つとも PASS、各 app の窓の PNG。

## TQ-3: demo-s8-s9

TQ-1 の A を流していれば不要。単独なら `plan/ws079/tests/demo-s8-s9.sh` を引数なしで（注記の既定の image）。

## TQ-4: WS005 の venus-session-check

元 T1-007: `build-settings-image.sh` に `config-venus.mk` を渡した image では `ZEDBSD_GRAPHICAL_BOOT` が無く、画面が「Display output is not active.」で判定できなかった。WS005 を再開するとき、WS005 の担当が image の作り方を直してから流す（このキューの人は流さず、依頼が来るのを待つ）。

## TQ-5: BUG-053 の swaphog 1300 MiB

負荷試験。**ユーザーの確認を取ってから夜間に。** 元 T1-049 の手順: `plan/tools/guest/guest.sh start IMAGE --memory 512` の後、guest で `swaphog stat`、`swaphog 450`、`swaphog 900`、`timeout 1800 swaphog 1300 30`、`ps -ef`。合格は全て `bad=0` で、guest が応答し続けること（2026-10-04 は 10 分で打ち切り、write 288885/332800 まで進んだ）。
