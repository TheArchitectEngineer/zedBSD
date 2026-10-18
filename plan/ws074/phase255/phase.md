<!-- awesome-plan project=zedbsd record=ws074-p255 -->

# ws074-p255: PerformanceObserverのnative mark登録・queue・task配信

Status: uncleared（q26 native consumer positive verified、Window ErrorEvent/harness negative未達）
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

Event ws074-p255-q26-primary-controls-refinement-20261018: 初回native/Chrome30条件は29一致、Symbol要素のsequence変換でnativeだけIteratorCloseを呼ぶ差を検出。一次[WebIDL sequence conversion](https://webidl.spec.whatwg.org/#create-sequence-from-iterable)も変換errorをそのまま伝播しcloseしないため、nativeを訂正し原期待[true,0]を保持。ChromeのObserver間microtask順first→micro→secondも確認しcallbackごとの通常bind_checkpointを使用。Window task backendはprivate id0（public clearTimerが拒否する範囲）でqueueし、公開timer IDを消費せずJSからcancel不能にする。scope/他Phaseの契約は不変、full gatesは修正後pairで行う。

Event ws074-q25-conformance-followup-20261018: q26全文監査でq25追加host-performance-marks.cのsplit callが最後のsource引数とanswer引数を同じ行に置く§8違反を検出。元q25のmanual conformance未解消0という判定を訂正し、i01のstandards clearanceをinvalidate/unclearedへ追記する。原API実装・独立Chrome38/native7・全fixed corpusの実測結果/原Outcome/commit/hashは保持、名前付きproducer機能出力は引き続きverifiedなのでq26のconsumer前提は満たす。q26が既に許可するhost fixture追従範囲でformatを修正し、C tokens/全string literal不変を実照合、native7と全q26最終gateで再確認する。whole p254は元からunclearedであり状態不変、goal1481も不変。shared projection pending。

Event ws074-p256-added-from-q26-oracle-gap-20261018: 新[p256](../phase256/phase.md)へWindow ErrorEventの既存欠落を渡す。意図的Observer callback failureはnative raw PASSに対し同じJS/official harnessのChromeはharness_error（completion.status1）、nativeはUncaught consoleのみでstatus0。p255が呼ぶ既存window_report_exceptionはWindow error listenerをdispatchしない。consumer positive30/GC10/producer7はverifiedだがq26 exception/harness fidelity条件は未達、held1は解除しない。p256 standalone timer/global ErrorEvent output→p255再評価を前提とし、p255 whole→p256の循環は作らない。旧raw PASS494にUncaught行は0、現確認済1481を保持。新sourceの実装は次Queue選定後。

Event ws074-q26-ended-uncleared-20261018: [history](../history/ws074-q20261018-26.md)・[evidence](evidence/20261018-q26/README.md)。native PerformanceObserver/EntryListを追加し、real mark登録/queued records/private uncancellable Window task、buffered mark replay、entryTypes過去抑止、sticky mode/sequence/dictionary変換、takeRecords/disconnect/reobserve、actual frozen SameObject supportedEntryTypes=[mark]、callback identity/順序・microtask・例外後の継続とGCを実装。Chrome30条件が完全一致、非保守的stack GC/iterator/callback/再入/例外継続/公開timer cancel耐性/retired child list保持とDocument/realm回収のnative10条件、producer7条件を通過。独立probeはactual prior/future各1・futureだけ通知。 最終source12324ff1e・frozen final pairで全1508 harnessはq25比whole/callback/completionが完全同一、494 raw PASS。static1561は844 PASS/717 FAILで全pixel metrics不変、scripted413は105 PASS/283 FAIL/5 unsupported/20 missingで805 captures全RGB不変、strict48は39 PASS/9 missing不変。DOM38、CSSOM12+12、host/target exact2 source warning0、producer Chrome38/native7、Acid3得点/inventory100・pixel差0/Acid2既存差873維持。 ただしcallbackのassert_unreachedを意図的にthrowする同じJS＋公式harnessの失敗対照がnative raw PASS/completion0、Chrome harness_error/completion1となり、既存Window reporterがconsoleだけでErrorEventをdispatchしないことを実証。q26 callback exception/harness fidelity基準は未達なのでattempt uncleared、held1は加算せず確認済1481/3805（38.922470%）維持（raw1482）。旧raw PASS494にUncaught行は0で既存確認済みを根拠なく減算しない。 責務を新p256 Window ErrorEventへ分離し、既存Event/Window/timer→p256のstandalone error output→p255再評価の依存を追加（cycle無し）。次finite Queueで通常productionの例外通知を実装し、official harness negativeが真に非PASS/positiveがPASSとなることを確認後にheld caseとp255を再評価する。runnerでconsole文字列から結果を補正せずWPT/期待値/分母を保持。p254旧fixture split-call規約違反はq26でtoken/string literal不変formatとnative7/全文規約で訂正、q25 historical unclearedは保存。whole p255/p189/p100/target full/WS100%は未達、shared projection/GitHub pending、push無し。

Event ws074-q26-declaration-conformance-followup-20261018: q27全文監査でq26導入のenvironment_entries_snapshotのforward宣言がconstant tables前にあり、initializer参照が無いことを確認。C全文§2の限定例外を満たさず、q26の「変更scope未解消conformance0」はこの点でも無効。元functional/corpus/GC/target出力は保持、q26 attemptは既にoracle未達でunclearedでありその結果を維持する。q27生成範囲にenvironment.cは含めないためsourceは変更せず、p100と次の該当source Queueへ規約整理を残す。q27 Event側は同じ問題を14 helper宣言の正規block移動で是正し、宣言multiset/残りtoken/host runtime object不変を証明。baseline producer/consumerの機能出力への依存とwhole/conformance受入を区別する。
