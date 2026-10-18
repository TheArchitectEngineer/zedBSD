<!-- awesome-plan project=zedbsd record=ws074-p255 -->

# ws074-p255: PerformanceObserverのnative mark登録・queue・task配信

Status: planning
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: 未選定（p254 producer後の候補）
Origin: [p189 q23 holdback](../phase189/evidence/20261018-q23/native-prerequisite-holdback.json)
Prerequisite: [p254](../phase254/phase.md)の実mark記録/取得とmanaged lifecycleのverified output。

## Own goal / current evidence

prelude-source.incのPerformanceObserverはobserve/disconnectが何もせずtakeRecordsが空、supportedEntryTypesも空のcompat placeholder。q23 negative WPTは過去entryが作られずobserverも機能しない状態でPASSとなった。original raw resultは保存し、確認済みgoal PASSへの加算はholdする。markとobserver両方のpositiveを提供して過去entry抑止が本当に機能することを検証する。

Native constructor/callback brand、markの実登録/filter/buffer・takeRecords/disconnect/reobserve、Window-owned task source/batching/再入・例外/GC・retired contextの停止、PerformanceObserverEntryList snapshotと正しいsupportedEntryTypesを実装する。p254のdefault markだけでなく、将来のtype拡張を名前だけ先に宣伝しない。試験用JS stubやtest URL分岐で結果を補正しない。

## Design / verification candidate

既存native interface/managed cell/root/Window timer-task backendを使い、observer/callback/queueをGC traceする。callback実行前にqueueを切り離し、callback中のmark/disconnect/GCが次回のbatchと現在のsnapshotを壊さないことを確認。active Windowだけでdeliveryし保存entry/listはretire後も安全に読める。既存Preludeのno-op fallbackを通常native implementationへ置き換える手順とchild realmでの初期化順を実コードから確定する。

独立Chrome/nativeでfuture markの実通知、type-specific buffered replay、entryTypesで過去を拾わない条件、複数observer・takeRecords・disconnect/reobserve・callback exceptions/GC・child退役を実測。q23前提probeとheld ID、full current corpus/DOM/host/target/style/Acidを新finite Queueに選定してから実行する。public ABI/Wayland/WPT/expectations/共有toolchain不変、Guardrail/C全文/browser componentを適用。p100/WS whole acceptanceは別。

## Target compatibility context

固定2025 WPTのtentative caseはentryTypes＋bufferedを無視する過去entry抑止を要求し、Chrome153も警告後に無視してfutureのみ通知する。一方[2026 Performance Timeline draft](https://www.w3.org/TR/performance-timeline/#dom-performanceobserver-observe)はentryTypesと他member併用をTypeErrorとする。実装選定時には、既にユーザーが指定したfixed2025 target/profile、対応するprimary snapshotと実際のChrome比較、最新draftとの差を明示して一般APIとして一貫させる。テストだけに特例を入れたり、2026完全適合を先取り主張したりしない。

Event ws074-p255-added-from-q23-20261018: p189のknown input/observer no-opを受け、p254 producer→本Phase real delivery→p189 holdback再評価を定義。origin/new producer/本Phase/WSへ構造更新を配送。まだ未選定、p254 outputを実際に検証してからconsumerを実行する。shared projection pending。
