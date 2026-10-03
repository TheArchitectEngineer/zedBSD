<!-- awesome-plan project=zedbsd record=ws049p012 -->

# ws049-p012: 壊れた table への堅牢性（fuzz）

Phase ID: `ws049-p012`
Parent: [WS049](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーのサブエージェント指示。branch の上で実行）

## 目的と受け入れ

kernel の中で firmware の AML を解釈するので、壊れた table で kernel を壊さないことを確かめる。受け入れ: 数千の壊れた table を
ASan・UBSan・LeakSanitizer の下で読み込み、全 method を走らせても、sanitizer の報告・crash・hang が 0（interpreter が誤りを返すのはよい）。

## 試験

`plan/ws049/tests/fuzz.py [--iterations N] [--seed S] TABLE...`: 毎回ひとつの table の AML の 1〜8 byte を、乱数か AML に多い byte（opcode、
PkgLength の上位 bit、名前の文字）に変え、`aml-host --quiet --reg --init --methods` で読み込み・初期化・引数の無い全 method の評価をする。
sanitizer の終了 code（ASan 99、UBSan 98）、signal、60 秒の hang を失敗とし、table を `build/ws049/fuzz/` に残す。

## 結果（2026-09-27）

| 実行 | 結果 |
| --- | --- |
| seed 1、300 回（q35・pc の DSDT、ASL の試験 4 つ） | 失敗 0 |
| seed 2、3000 回（q35・pc の DSDT、PRIMERGY の SSDT 2 つ、ASL の試験 8 つ） | 失敗 0 |
| seed 3、100 回（q35 の DSDT と data.aml）の終了 code | 0 が 41、1（interpreter が誤りを返した）が 59。壊れた table が誤りの経路を通っていることの確認 |

## 制限

- 変異は byte の置き換えだけ（長さを変える挿入・削除はしない）。PRIMERGY の DSDT（272 KiB）は 1 回が長いので入れていない。
- host の harness の上の確認で、kernel の上では未実施。
