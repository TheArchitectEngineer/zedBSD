<!-- awesome-plan project=zedbsd record=ws045p009 -->

# ws045-p009: 全文の規約確認

Phase ID: `ws045-p009`
Parent: [WS045](../ws.md)
Status: planned
Queue: なし（サブエージェント）
依存: ws045-p002〜p008

## 目的

WS045 で変えた全 source を `plan/coding-style.md` の全文と照らし、機械的な検査（`plan/tools/style-check.py`）と人の目の確認を記録する。

## 受け入れ

- 変えた file の style-check が 0（legacy の file は着手前より増えない）。全文の review の結果と例外を記録。build warning 0、差分試験が全件。
