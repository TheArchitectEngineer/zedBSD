<!-- awesome-plan project=zedbsd record=ws074-p254 -->

# ws074-p254: User Timing markの実記録・取得・native entry lifecycle

Status: planning
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: 未選定（q23後の候補）
Origin: [p189 q23 native prerequisite holdback](../phase189/evidence/20261018-q23/native-prerequisite-holdback.json)

## Objective / evidence

現在のperformance.markはenvironment_performance_nothingへ接続し、getEntries*は常に空。p077で明示的に受け入れた最小readonly/compat範囲を越える実記録はまだ無い。q23の追加17中1 IDはWPT上PASSでも、事前のmarkが作成されないため観測抑止の試験前提が無い。[native/Chrome probe](../phase189/evidence/20261018-q23/performance-prereq/probe.html)はnative prior/future各0、Chrome各1とfuture-only通知を示す。p077の既存受け入れ/履歴をinvalidateせず、新しいproducerとして実データを提供する。

Own goal: native PerformanceEntry/PerformanceMarkの実状態・prototype/brand/readonly fields、performance.markの実記録、getEntries/ByName/ByTypeの実buffer snapshot/順序・clearMarks、Window独立・saved entry/GC/retired frameのlifetime。name/type/startTime/duration/detailのうち最初の有限scopeはdefault optionsとprimitive detail/明示startTime等を正確に選定し、複雑detailのstructured clone/measure/resource/event/worker等は未達を明示する。native Observerの登録/通知はp255が所有する。

## Design / prerequisites

既存bind/environment.c・window.c・internal.hのnative interface registry/managed cell/native_operations brand/既存clockを使う。native timelineはmanaged stateがvm_value entriesをtraceし、entryは独立managed immutable stateで名前・detailを保持。prototype lookalikeを拒否し、Object/Arrayのconstructor名だけを偽装しない。構築中のname/options/entry/wrapperをallocation/callback前にrootし、buffer publicationを成功後に行う。getterのToString/辞書Get/再入GC・clear during conversionに対して現在のbufferを整合させる。特に保存したentryがWindowやDocumentのC pointerを借用したままretire後に触れない設計とする。

新source module追加を前提にせず既存engine source内で設計し、public browser ABI2/Wayland/shell/共有toolchain/sysroot/WPT/runnerを変えない。Guardrail/C全文/browser component全文を適用、new/refactored functionsを全文生成/format19/style0で検査し既存legacy残差を別記。p100 full WS conformance/target link/runtimeは別の関門。

## Completion / verification candidate

Native APIの通常pathでactual mark objectが返り、bufferの存在/内容・filter/clear・readonly/brand・独立Window/GC/lifetimeを実測。default timestampsは既存performance.nowと整合、明示optionsの対応scope/未対応を誤魔化さず記録する。Chrome独立probe、native直接host fixtureとJS、full current1508 harness/1561 static/413 scripted/48 strictを同一final pairでbefore/after照合し旧whole/callbackの後退を診断、host/target exact source warning0/Acid非悪化を確認。対象source/checks/timeboxを新Queueへ保存するまで実装しない。

p255はこのPhaseのwhole clearanceではなく、実際にverifiedになったmark creation/retrieval/clear/managed lifecycleのscoped outputを前提にできる。q23のheld PASSは本Phaseへの転記だけでclearしない。real observer future deliveryとpast suppressionをp255で検証してからp189で再評価する。

## Standards / version context

[User Timing mark algorithm](https://www.w3.org/TR/user-timing/#mark-method)はmark objectの作成・queue・buffer追加・返却を要求する。2026-03-11 draftと固定2025 WPT/Chrome2026の共通なproducer条件を使用する。observer mixed entryTypes/bufferedの世代差はp255へ記録し、このproducer設計とは分離する。

Event ws074-p254-added-from-q23-20261018: p189で確認した既存no-opによる入力欠落をownerとして受け取る。mark/entryのNative producerを定義、p255 consumer→p189 holdback再評価の依存を追加。まだQueue未選定でありsource permission/clearedを先取りしない。p077 readonly受け入れは維持、shared projection pending。


Event ws074-q23-clearance-invalidated-idl-20261018: [origin p189](../phase189/evidence/20261018-q23-followup/missing-idl-proof.json)で2 IDの入力欠落が判明。Native mark/observer no-op diagnosisと本Phase goalは不変。source生成の前にp189のmissing pinned IDL providerと正しい全1508 baselineのverified outputを待つ。q24-preflightのChrome mark入力は調査資料でありQueue/source実装ではない。shared projection pending。

Event ws074-q24-provider-prerequisite-verified-20261018: [p189 corrected baseline](../phase189/evidence/20261018-q24/README.md)でIDL原資源/全1508/旧1491同値を実検証し、前回input不足の再開条件を満たした。mark producer goal/基準は不変、Native source実装は次のexact finite Queue選定後。
