<!-- awesome-plan project=zedbsd record=q1148 -->

# q1148 / WS074 bounded vertical-lr sideways paint

Status: finished（2026-10-14 11:28–11:34 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p220有限部分scope](../ws074/phase220/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1148-i01 | [p220](../ws074/phase220/phase.md) partial | 非折返しvertical-lr/LTR Latin text-onlyのCPU/GPU時計回りsideways paint、独立F pixel・clip、固定旧WPT/host/build/Acid回帰 | cleared。p220 whole in-progress |

Outcome: F字形の回転画素完全一致、CPU/GPU通常・clip差0、旧WPT10/10 PASS。shader検証、warning0 build、host CSS51/JS14/Grid48 PASS、Acid3 score100/inventory100/差0、Acid2差1865維持。[証拠](../ws074/phase220/evidence/20261014-q1148/README.md)。WPT/runner/公開ABI/Master無変更。

Resume: p220 upright/CJK・他writing-mode・折返し・複雑inline・装飾、p225 auto tracks、p189 runner coverage。p173全3,805件100%未達。共有Queue/Master/GitHub publication pending、pushなし。
