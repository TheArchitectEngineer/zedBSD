<!-- awesome-plan project=zedbsd record=ws245-p002 -->

# ws245-p002: 骨組み（design.md の P01）

Status: in-progress（q992、P2、2026-10-16）
Parent: [WS245](../ws.md)
Approval: 2026-10-16 ユーザー「D1〜D6全部推奨で。」（D5: P01〜P05 から始める）

## 範囲

[design.md](../design.md) の P01 の行: `display/adl/` の directory、path・plan・resource の型、executor の殻、boot の語 `i915.display.new`、gen12 の入口からの振り分け。新の道は no-op（旧の code に渡す）。

## 受け入れ

build warning 0、固定の snapshot に対する planner の host 試験、`new=0` の起動が今と同じ（QEMU・5330 の確かめは点 A でまとめて）。

## 結果

（Q1 が書く）
