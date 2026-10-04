<!-- awesome-plan project=zedbsd record=ws045-p010 -->

# ws045-p010: dirname の複数の operand（GNU）と WS045 の受け入れ

Status: in-progress（2026-10-05 P1 generation17 / q712-i01。host の差分試験まで。amd64 の image の boot と guest の GNU の case は T1 に依頼。結果の判定まで cleared にしない）
Disposition: normal
Parent: [WS045](../ws.md)
Queue: q712 / q712-i01

## 範囲

- 2026-10-05 ユーザーの決定「GNU に合わせる」: `dirname` は複数の operand をとり、それぞれの結果を行ごと（`-z` なら NUL ごと）に出す。WS001 の `plan/ws001/tests/dirname-test.sh` も合わせる。
- WS045 の受け入れ: GNU の差分試験と WS043 の POSIX の差分試験の全件、amd64 の build と boot（i386 などは免除、Q1）。

## 結果（2026-10-05）

- source を読むと、`userland/base/dirname/main.c` は既に複数の operand と `-z`・`--zero` を GNU の通りに扱い、`plan/ws001/tests/dirname-test.sh` も複数の operand を成功として確かめる形になっていた（以前の main の取り込みで入っていた）。source の変更は要らなかった。
- GNU の差分の case に dirname を足した（`plan/ws045/tests/cases/misc.sh`: 複数の operand、`-z` と複数、`--zero` と `--`）。

| 確認 | 結果 |
| --- | --- |
| `sh plan/ws001/tests/dirname-test.sh` | WS001 dirname: PASS |
| `python3 plan/tools/utils/util-diff.py --bin build/ws045-host --cases plan/ws045/tests/cases --gnu`（host の build は `plan/tools/utils/build-host-utils.sh build/ws045-host`、zedBSD の TRE） | 518/518（awk 58、grep 98、misc 164、sed 174、sort 24） |
| `python3 plan/tools/utils/util-diff.py --bin build/ws045-host --cases plan/tools/utils/cases`（POSIX、POSIXLY_CORRECT あり） | 1080/1080（dirname.sh の複数の operand を含む） |
| `make BUILD=build/q713 build/q713/bin/dirname`（amd64） | 成功、warning 0 |
| style-check（dirname） | 指摘 0 |
| amd64 の image の boot、guest の GNU の case | **未実施**。T1 に依頼（下） |

## T1 への依頼

- image: `make ZEDBSD_CONFIG=plan/ws045/tests/config-amd64-base.mk BUILD=<dir> disk-image`（ws045-p008 と同じ、main の最新）。
- `plan/tools/boot-test.sh <dir>/hdd-image.img`（login の PNG）。
- 余裕があれば ws045-p008 の手順で guest の GNU の case（`plan/ws045/tests/build-guest-utils.sh` の後 `plan/ws045/tests/guest-batches.sh`）。
- 合格: boot PASS（PNG）、guest の case は host と同じ（p008 では image の古い /bin/sh の builtin の env の 4 件が違った。今の image の sh なら同じはず）。

## 残り

- T1 の結果の判定。PASS なら WS045 は受け入れを満たす（ws.md の完了と Phase の片付けは Q1）。
