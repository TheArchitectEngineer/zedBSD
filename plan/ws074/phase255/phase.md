<!-- awesome-plan project=zedbsd record=ws074-p255 -->

# ws074-p255: PerformanceObserverのnative mark登録・queue・task配信

Status: in-progress（q26 native mark consumer host partial）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: [ws074-q20261018-26-i01](../queue.md)
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

Event ws074-p255-primary-version-correction-20261018: 一次文書の再読で、上記の「2026 Performance Timeline draft」という日付の記述を訂正する。現在のTRが示す固定版は[2025-05-21 CRD](https://www.w3.org/TR/2025/CRD-performance-timeline-20250521/)で、この版にもentryTypesと他memberの併用TypeErrorがある。相違は単純な2025対2026の世代差とは断定せず、固定tentative WPT/Chrome153の実測挙動とpublished algorithmの差として保持。将来の選定scopeでfixed-target互換profileを明記しURL別例外にはしない。observer modeの変更は一次algorithmではInvalidModificationError、未対応typeだけのobserveは既存registrationを破壊せずabortする。現時点のp255実装は未選定、q25 native producer gate待ち。

Event ws074-q25-producer-prerequisite-verified-20261018: [p254 q25 verified output](../phase254/evidence/20261018-q25/README.md)は通常mark記録/取得/clear/managed lifecycleとWindow/GCの実producerを提供し、全fixed corpus旧PASSを維持。whole p254 clearanceを前提にせず、このnamed host outputを本Phaseのconsumer入力として使用可能。原held caseは実entryを持つが通知0のため保持。observer-designの技術調査は証拠へ保存、まだ本Phaseのsource Queueは未選定。

## q26 exact design / acceptance

Event ws074-p255-q26-selected-20261018: [Queue](../queue.md)へ18:49 JSTまでのnative mark consumer host partialを保存。p254 whole clearanceではなくq25の実producer/取得/clear/Window/GCのnamed verified outputを前提にする。timelineはactive observer wrappersとone queued-task callback/flagをmanaged trace、observerはcallback/timeline/実record vectorをtrace、EntryListは切離したmark referencesを自己所有する。既存Preludeはnative globalがあればno-op fallbackを実行しないためsource変更不要。

Native timer-task backendによりnotify登録順のsnapshotと各pending buffer切離しを行い、callback前にqueued flagを解除して再入markを次taskへ送る。例外はbind_report_exceptionで通常報告し他observerを続ける。takeRecordsはraw queue順を保持してdrain、EntryList queriesはstartTime順snapshotを返す。observeは辞書のlexical member順/iterable protocol/throwを実装し、登録modeはdisconnect後も維持。frozen ['mark']だけを正しく公開し未実装typeを宣伝しない。

Target profileはexact Queueの通り、固定tentative WPT/ChromeのentryTypes+buffered ignoreを一般APIとして適用し、published 2025 CRDとの相違/partial scopeを明記する。callback droppedEntriesCountはmark-onlyで実drop0、observe後初回に0/以後は未設定のoptionとして扱う。既存producer snapshot helperを必要な範囲で共用しても通常API/Window/GC契約を維持し、p254 native7と38条件を再検証する。

独立Chrome/nativeでfuture通知、buffered replay、過去entryTypes抑止、takeRecords/disconnect/reobserve/mode/error/readonly supported types、複数observer/再入/例外/GC/child退役を確認。全fixed corpusとhost/target/style/Acidのgate、scope/禁止事項/未達の扱いはQueueの通り。held PASSはreal positive/原WPTがそろうまで加算しない。
