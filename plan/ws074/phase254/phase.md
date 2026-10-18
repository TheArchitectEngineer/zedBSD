<!-- awesome-plan project=zedbsd record=ws074-p254 -->

# ws074-p254: User Timing markの実記録・取得・native entry lifecycle

Status: in-progress（q25 native producer host partial）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: [ws074-q20261018-25-i01](../queue.md)
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

## q25 selected design / bounds

Event ws074-p254-q25-selected-20261018: exact scope/criteria/timeboxは[Queue](../queue.md)へ保存。private timelineはmanaged vm_value vectorをtrace/finalizeし、markは名前/detail/timestampを自己所有しWindow pointerを保持しない。receiverのnative_operationsとmanaged cell typeでbrand検証。name/detail/prototype/新state/新wrapperをallocation/辞書Getより前に必要なrootへ登録、成功後だけbufferへpublish。snapshot/clearは文字列変換再入後の最新bufferを参照。mark constructorは既存new_target prototype protocolを使いbufferに入れない。entry readonly IDL getterは関連prototypeへenumerableで追加、entry toJSONは共通4fieldをordinary own dataとして公開。

今回detailはclone不要なprimitiveだけに限り、複雑object/function/symbolはDataCloneErrorとして未対応を明示。structuredClone globalやJS補完に依存せず、複雑detailのfull-standard algorithmはwhole残差として保持。p255の実observer後までheld caseは解除しない。原固定2025 WPT/一次User Timing/Chrome独立21条件を参照し、q24入力不具合訂正後baselineを使用。検証gate/正確な生成source/禁止事項はQueueの通り。

Event ws074-p254-q25-owner-refinement-20261018: 独立Chrome/nativeでborrowed parent mark.call(child.performance)の生成prototypeが相違と判明し、timelineが実owner globalとmark prototypeをmanaged traceする内部設計へ精密化。mark生成はreceiverの保持するrelevant global/clock/prototypeを使い、entry自体はraw Window pointerを持たない。root-only GC/retired childとborrowed methodを同じ38条件/host fixtureで再検証する。Phase外のscope/interface/順序は不変、p255はverified producer待ちを維持。
