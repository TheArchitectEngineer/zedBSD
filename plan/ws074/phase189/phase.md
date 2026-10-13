<!-- awesome-plan project=zedbsd record=ws074-p189 -->

# ws074-p189: Interop 2025 testharness 結果取得

Status: planned（2026-10-13 追加、Queueなし）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p173 q1012 manifest](../phase173/evidence/20261013-q1012/manifest.json) の固定選定出力。p173 whole clearance は要しない。

## 目的と受け入れ

Interop 2025 固定 manifest の testharness 1,783件について、WPT のテスト本体と期待を変更せずに実行し、test/subtest の PASS・FAIL・ERROR・TIMEOUT・未実行を区別して保存できる runner を作る。HTTP/HTTPS、origin、worker、variant などの実行環境差は明示して段階的に対応し、未実行を PASS に算入しない。最終的に全1,783件の結果と、WPT実行環境の未対応が0であることを確かめた時に whole clearance を判断する。ブラウザ実装の不足による実際の FAIL は p173 の後続互換性 Phase に渡し、本Phaseの runner 完成と混同しない。

## 最初の有限 attempt 案（未承認）

最大90分。既存 `browser --run` と WPT の testharness callback を調べ、単一 HTTP origin の選定済み testharness HTML 1件で subtest と completion を機械可読に取得する。runner の最小実装、成功/失敗/timeout/無出力の区別、固定 manifest に載るIDだけを受け付ける検査と再現可能な fixture を含む。testharness.js 自体の改変、テスト固有の PASS 強制、browser engine/公開ABIの変更は含めない。もし WPT の JS harness が現行 engine で構文/機能エラーになれば、検証結果と最小原因を記録して attempt を uncleared とし、browser の別Phaseを設計する。p173 q1012 で `--run` に選定ページを1件渡した際は exit0・console出力0で、結果判定はできなかった。

適用: [Guardrail](../../guardrail.md)、runner の近傍規則、browser sourceに進む場合は [C全文](../../coding-style.md) と [browser component全文](../../standards/browser-component.md)。影響候補は `plan/ws074/tests/` と `plan/ws074/phase189/`、ignored build。新しい code と実行は exact-scope Queue 承認後のみ。`master.md` は変更しない。

Event ws074-p173-q1012-split-20261013: [p173](../phase173/phase.md) の3,805 ID のうち1,783件が testharness で、現行 static reftest runner は結果取得できないため設計。p173 の目標・分母は縮小せず、本Phaseの出力を p173 全件 baseline の前提にする。GitHub publication保留。
