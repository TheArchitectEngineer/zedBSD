<!-- awesome-plan project=zedbsd record=queue -->

# q1012 / WS074 Interop 2025 lane

Status: finished（2026-10-13 23:49〜23:58 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: 2026-10-13 23:49 JST、この chat のユーザー回答「q1012 の範囲を承認する」。[exact scope snapshot](phase173/selection-snapshot.md) の q1012-i01 を承認。以前の Acid3 自動反復指示はこの Queue の根拠にしない。
Timebox: 2026-10-13 23:49 JST 開始、最大90分（翌01:19 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1012-i01 | [ws074-p173](phase173/phase.md) partial | 公式Interop 2025 19領域の固定 manifest と WPT 資産取得、runner能力照合、実行可能な代表試験の baseline、failure/未実行分離、証拠・後続Phase案。ブラウザ本体の修正なし。最大90分 | cleared（partial、p173 whole は uncleared） | p172 whole cleared、p188 main統合出力 verified |

Dependency graph: p172 → q1012-i01、p188 verified output → q1012-i01、q1012-i01 → p173 whole baseline判断。p100 whole は Acid2 exact/target gate の別課題。

Upcoming Work Outlook: [p189](phase189/phase.md) testharness結果取得、[p190](phase190/phase.md) reftest/crashtest runner coverage、p173全件baselineを候補とする。sample描画FAILは領域別に調べる。次の Queue は別の選定・承認を要する。

Coordination: [q1011](../history/queue-q1011.md) までの履歴を保持。共有 `plan/queue.md` / Master / GitHub publication は別の Q1 セッションとの競合を避け pending projection。WS074 lane の状態はこの file が所有。

Previous outcome: q1011-i01/p188 cleared。実 HTTP visited-link fixture6/6、frame load52/52、CSS rule44/44、host build warning0。固定 Acid3 score100/100・inventory100/100・例外0・差 **0/480000**。[証拠](phase188/evidence/20261013-q1011/report.json)。

Outcome: q1012-i01 partial cleared、p173 whole uncleared。[report](phase173/evidence/20261013-q1012/README.md) に公式19 focus areaの固定3,805 unique test ID（未解決0）、runner静的候補1,561/未対応2,244、選定sample35件5 PASS/30 FAIL/0 error、再現コマンド・SHA・制約を保存。p189/p190を新Phaseとして設計した。browser本体無変更、`master.md` 無変更、Python構文と差分check PASS。全件正式PASS率は未測定、ユーザーの100%目標は未達。共有Queue/Master/Project/GitHub投影は pending。
