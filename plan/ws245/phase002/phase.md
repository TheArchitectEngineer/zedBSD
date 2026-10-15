<!-- awesome-plan project=zedbsd record=ws245-p002 -->

# ws245-p002: 骨組み（design.md の P01）

Status: cleared（2026-10-16、main 00d8f32a7）
Parent: [WS245](../ws.md)
Approval: 2026-10-16 ユーザー「D1〜D6全部推奨で。」（D5: P01〜P05 から始める）

## 範囲

[design.md](../design.md) の P01 の行: `display/adl/` の directory、path・plan・resource の型、executor の殻、boot の語 `i915.display.new`、gen12 の入口からの振り分け。新の道は no-op（旧の code に渡す）。

## 受け入れ

build warning 0、固定の snapshot に対する planner の host 試験、`new=0` の起動が今と同じ（QEMU・5330 の確かめは点 A でまとめて）。

## 結果

- **q992 P2（提出 a5eba4c2c・1ff73b3ab）**: display/adl/ に adl.h（adl_config・adl_path_request・adl_snapshot・adl_plan・adl_step_kind（power → clock → link → timing → planes → panel））、adl-plan.c（書かない planner: 範囲・重複・pipe/transcoder/DDI の衝突・snapshot の使用中の検査、eDP の DDI A は専用の transcoder）、adl-execute.c（CHECKED の plan だけを受け、P01 では全 step が no-op の表）、adl-config.c（boot の語 `i915.display.new`、既定 0、0・1 以外は通知して 0）。display/display.c の i915_display_attach の Gen12 の入口で adl_display_select を 1 回。i915.mk に 3 file。
- 確かめ（Q1、main）: build rc 0・warning 0、plan/ws245/tests/adl-plan-host-test.sh 78/0。QEMU・5330 は点 A。
- 逸脱（担当の選択）: file の名前 adl-*.c、snapshot の型を adl.h に（hardware なしで試験）、executor は step の表。
