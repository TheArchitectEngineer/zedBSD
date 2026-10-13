<!-- awesome-plan project=zedbsd record=ws074-p173 -->

# ws074-p173: Interop 2025 WPTの対象固定とbaseline

Status: uncleared（2026-10-13 q1012-i01 partial cleared、whole baseline未達）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p172](../phase172/phase.md) whole-Phase cleared/実統合出力、[p188](../phase188/phase.md) の main 統合済み Acid3 score100/100・RGB差0の検証出力。p100 whole clearance は必要としない（Acid2 exact/target gate は独立）。

## 目的

ブラウザをWPT Interop 2025の公式focus area対象テストで検証し、全対象PASS 100%を目指す。その前段として公式の選定範囲、固定WPT commit、対象manifest、実行環境、runner能力、baselineを確定し、後続の小さな改善Phaseへ分ける。ユーザーの2026-10-02指示は目標と専任枠の指定であり、本Phaseの実行Queue承認ではない。

参照: [Interop 2025公式説明](https://github.com/web-platform-tests/interop/blob/main/2025/README.md)（focus areasとinvestigation effortsを別に記載）、[WPT](https://github.com/web-platform-tests/wpt)。focus areaに含まれるtestharness/reftest等を対象とし、調査項目は得点対象と混同しない。対象の選定規則と分母は実行時に公式manifest/labelsと固定commitから再現可能にする。

## 最初の有限な作業とclearance

1. p172で取り込まれたrunner/branch側WS074記録を確認し、公式Interop 2025 focus areaのtest selectionを固定commitで列挙する。各testの種別・期待・重複・環境要件をmanifestへ記録する。
2. 現行browser/libbrowserで走る範囲を測る。未対応のrunner機能、未実装web機能、実際のFAIL、crash、timeout、skipを分ける。実行不能をPASSとして扱わない。
3. 分母・PASS数・各focus areaの率と失敗clusterを再現可能なreportへまとめ、依存/コストを考慮して改善Phaseを設計する。対象を都合で除外しない。WPT資産はWS074の既存方針どおり固定取得し、ライセンスを確認する。

本Phaseのclearanceは**対象固定・runner/baseline・後続Phase設計**の完了を意味する。Interop 2025全件PASSの達成を意味しない。最終目標は固定した全対象でPASS 100%、fail/error/crash/timeout/未説明skip 0とし、後続の改善と最終回帰の証拠がそろった時にWSレベルで判定する。対象の変更や実行不能の扱いに製品/範囲判断が必要なら証拠と選択肢を記録し、勝手に基準を緩めない。2026-10-13 のユーザー指示で本Phaseを次の対象にした。p174 File System Access/p175 OPFS は未開始の独立候補として残す。

## 制約・再開

[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)を適用。公開ABIとWaylandなしのlibbrowserを保つ。最初のattemptはmanifest/runner/baselineに限定し、時間上限と検証コマンドはQueue選定時に確定する。p188の実出力は main 統合後の [report](../phase188/evidence/20261013-merge-main/report.json) で確認済み。p100 whole は Acid2 exact/target gate が未達だが、Interop対象固定の技術的前提ではない。現時点は計画のみ、source変更/試験/Queue実行なし。

2026-10-13 planning inventory: [selection snapshot](selection-snapshot.md)。公式19 focus area、Interop repository `2d28de9c1e86182c38a7311854ca59cefde7ac04`、WPT metadata `018c06b2c0fae50400c71585c73b7f238a2d029a`、既存 WPT `2d66b9b7998bb58c336138c178323ddee857b586` を確認。metadata上の重複除去後3,805 pathは**候補**で、WPT revision/variants/metadataのずれとrunner coverageの照合前なので公式分母・通過率とはしない。q1012-i01 はこの snapshot の範囲でユーザー承認済み。

Queue: [q1012-i01](../queue.md) partial / 2026-10-13 23:49 JST ユーザー承認・開始。最大90分。ブラウザ本体の修正なし。

## q1012-i01 結果（2026-10-13 23:58 JST）

**partial scope cleared、p173 whole は uncleared。** 公式 [Interop 2025](https://github.com/web-platform-tests/interop/blob/main/2025/README.md) の19 focus areaから固定 metadata の label を選び、WPT `MANIFEST.json` に全件照合した。3,811領域所属（重複6）/ **unique 3,805 ID**、未解決0。WPTの3-Clause BSDライセンスを確認し、suite は source tree に複製せず外部 `/tmp` checkout と ignored build に置いた。metadata は外部入力で source tree に vendoring していない。生成手順、SHA-256、分母と種別、sample全結果は [証拠](evidence/20261013-q1012/README.md)。

種別は testharness1,783、reftest1,974、crashtest48。現 runner で静的reftest候補1,561。testharness1,783、crashtest48、scripted test/reference407、variant6はrunner未対応であり PASS として数えない。選定済み静的reftestの35件 sample は **5 PASS / 30 FAIL / error0**。anchor positioning0/10、Flexbox0/10、Grid2/5、backdrop-filter3/5、writing modes0/5。これを全体通過率へ外挿しない。testharness選定ページ1件を `browser --run` に渡しただけでは exit0・console0で判定不能だった。

Runnerの `WPT` symlink解決と公式selection manifestフィルタを修正し、固定 manifest generatorとcoverage分類器を追加。browser engine/公開ABI/テスト本体は変更しない。Python構文check、選定sample35件のID照合、`git diff --check` を確認。既存 CSS2 全件、全1,561静的候補、testharness/crashtestの正式実行、別OS、Acid3再計測は今回未実施（browser source無変更）。選定全件のPASS数・各領域の正式率、runner coverage0未達のため whole clearance はしない。

失敗clusterと次の設計: testharness結果取得1,783件を [p189](../phase189/phase.md)、scripted reftest/variant/crashtest及びstatic shardを [p190](../phase190/phase.md) に分離。sample の描画差は anchor/Flexbox/Grid/backdrop/writing modes 別に調べる。p189/p190 の出力と全件の再現可能な baseline がそろったら本Phaseの whole 判定を再開する。p173の3,805 ID分母やユーザーの全件PASS 100%目標は変更しない。次のQueueは未選定・未承認。

Event ws074-p173-q1012-outcome-20261013: q1012-i01 partial scope cleared、p173 whole uncleared。上記証拠と runner 未対応2,244件、sample FAIL30件を記録。resume は p189/p190 の別Queue承認と出力、full baseline。GitHub publication保留。

Event ws074-p173-q1012-split-20261013: 公式分母が確定して現 runner 能力不足が分かったため p189/p190 を追加。p189はtestharness、p190はreftest/crashtestを担い、両者は p173の固定manifest出力を前提に独立。p173全件baselineは両出力を必要とする。p100/p101/p174〜p176の範囲・依存は変更しない。GitHub publication保留。

Event ws074-p173-interop-resume-20261013: ユーザーの「WPTに進みます。2025を100%」を、既存WSのInterop 2025公式focus area目標の再開・優先化として受けた。p100 wholeの未達を隠さず、p173の前提を p188 の統合済みブラウザ実出力に限定した。p100、p101、p174〜p176の受け入れは変更しない。Phase comment/WS projection はローカルに記録、GitHub publication保留。

Event ws074-dedicated-interop2025-20261002: ユーザーが専任ブラウザ担当によるInterop 2025クリアを目標に追加。p173を計画し、p172/p100を前提にした。WSのp101 CSS2全件目標は保持。GitHub publication保留。

Event ws074-browser-next-goals-20261002: ユーザーがInterop 2025 100%を明示し、File System Access/OPFSを先に積むよう指定。全対象PASS 100%/未説明skip 0を数値目標とし、p174/p175を専任laneの先の候補にした。p172/p100の技術的前提と本Phaseのbaseline clearance条件は維持。GitHub publication保留。

2026-10-05 Q1 records reconciliation: WS074 は Codex から Q1 に戻り、レンダリング・互換性の改善はユーザーの指示まで止める（[WS074](../ws.md) の「Q1 向けの整理」）。前提の p172 は 2026-10-03 に whole cleared。このPhaseの Status は変えず、選ばない。専任 P10 の枠の記述は履歴（今の担当は Q1）。
