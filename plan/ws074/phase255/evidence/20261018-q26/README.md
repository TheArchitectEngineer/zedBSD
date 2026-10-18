# q26 native mark observer / uncleared error-report fidelity

native PerformanceObserver/EntryListを追加し、real mark登録/queued records/private uncancellable Window task、buffered mark replay、entryTypes過去抑止、sticky mode/sequence/dictionary変換、takeRecords/disconnect/reobserve、actual frozen SameObject supportedEntryTypes=[mark]、callback identity/順序・microtask・例外後の継続とGCを実装。Chrome30条件が完全一致、非保守的stack GC/iterator/callback/再入/例外継続/公開timer cancel耐性/retired child list保持とDocument/realm回収のnative10条件、producer7条件を通過。独立probeはactual prior/future各1・futureだけ通知。 最終source12324ff1e・frozen final pairで全1508 harnessはq25比whole/callback/completionが完全同一、494 raw PASS。static1561は844 PASS/717 FAILで全pixel metrics不変、scripted413は105 PASS/283 FAIL/5 unsupported/20 missingで805 captures全RGB不変、strict48は39 PASS/9 missing不変。DOM38、CSSOM12+12、host/target exact2 source warning0、producer Chrome38/native7、Acid3得点/inventory100・pixel差0/Acid2既存差873維持。 ただしcallbackのassert_unreachedを意図的にthrowする同じJS＋公式harnessの失敗対照がnative raw PASS/completion0、Chrome harness_error/completion1となり、既存Window reporterがconsoleだけでErrorEventをdispatchしないことを実証。q26 callback exception/harness fidelity基準は未達なのでattempt uncleared、held1は加算せず確認済1481/3805（38.922470%）維持（raw1482）。旧raw PASS494にUncaught行は0で既存確認済みを根拠なく減算しない。 責務を新p256 Window ErrorEventへ分離し、既存Event/Window/timer→p256のstandalone error output→p255再評価の依存を追加（cycle無し）。次finite Queueで通常productionの例外通知を実装し、official harness negativeが真に非PASS/positiveがPASSとなることを確認後にheld caseとp255を再評価する。runnerでconsole文字列から結果を補正せずWPT/期待値/分母を保持。p254旧fixture split-call規約違反はq26でtoken/string literal不変formatとnative7/全文規約で訂正、q25 historical unclearedは保存。whole p255/p189/p100/target full/WS100%は未達、shared projection/GitHub pending、push無し。

## Evidence and commands

Approval0749a534f、source12324ff1e。summary/4 suite comparison/source-proof/native-runs/versions/target-commandsが索引。harness-all-rawは全1508 report/checkpoint/generated原JS/IDL/stdout binary、scripted-all-native-capturesは805全capture/PNG/ready/headers、crash-all-rawは48全raw、acid-all-rawは全画像/DOM/console等を保存。全tar memberを順次byte read-back済み。runner/WPT/manifest/固定期待値は不変、WPT tracked clean。archive初回出力folderをproducer側に誤指定したが、データを捨てずconsumer evidenceへrenameし、hashを保持。retain helperの旧design.txtコピー参照は外して再実行。

## Oracle failure / resume

oracle-gap-proof/native-result/Chrome-result/Chrome原console/HTML、oracle-failure-controls（source.js/official testharness/report/wrapper/stdio binary等）を保持。同じintentional assert_unreachedがnativeではUncaught consoleのみでwhole PASS、ChromeではWindow error listenerからcompletion.status1になる。resources/testharness.jsのerror listenerとbind/window.cのconsole-only reporterを照合済み。callback self/real data/future-only deliveryのpositiveは成立するが、この失敗判別の前提欠落を無視してheld1を解除しない。

通常callback ErrorEventをp256が所有し、p255はそのverified outputで再開する。p256はstandalone timer failureで前提を独立に検証し、p255 wholeへ依存させない。既存active/archived bug/Phaseの検索に同一tracking recordは無かった。shared Bug Board/Issue公開はQ1所有のためpending。known failureなのでnon-blocking bug transferによるclearanceはしない。

## Standards / inputs / limitations

C全文/Guardrail/browser componentを直接適用、project format19と全文のmultiline定義/3-clause条件/split-call規則を優先。generated/fixtureの機械的指摘は65のany-gotoだけで、全て単一shared cleanup labelへのforward jumpを全件構造/manual照合、他0/未解消conformance0。checker raw0とは主張しない。旧source全体のlegacy findingsはp100残差。targetはq02 exact window/environment compileの-oのみ変更し2source warning0、full target link/runtimeは未実施。公開ABI2/Wayland/source module/SDK/sysrootは不変。

最初のbuildはout-parameterのGCC maybe-uninitialized診断を受けcaller localを明示初期化。最初のChrome/native30比較は29一致、残1はsequence要素変換errorでnativeがIteratorCloseする差であり、WebIDL一次algorithmとChromeに合わせてcloseせずerrorを伝播するよう訂正し、期待結果を変えず30完全一致。Chromeはcallback間microtask first→micro→second、nativeも通常bind_checkpointで一致。entryTypes+bufferedは固定WPT/Chrome互換として一般にbufferedを無視するがpublished 2025-05-21 CRDの併用TypeErrorとの差を明示しfull最新仕様適合は主張しない。

private task id0は既存public clearTimerが拒否する範囲であり、JS-visible timer idを消費せずcancel不能。実Window task queue/tracer/retirementを使いmicrotaskで配信を代用しない。frozen supported arrayはreal native immutable index/length/nonextensibleのmark-only。他entry type/worker/complex detail/id/navigationIdは未実装を保持。

producer-fixture-format-proofはq25試験Cのsplit-call§8違反のtoken/全string literal不変の訂正と原hashを保存。元q25 conformance判定はdated follow-upでunclearedへ訂正し原結果を保持、このq26のnative7/full conformanceで修正版を再検証。

staticは全1561のreport/pixel metricsを保持するが原runnerの制限で全画像を保存せずfail2と最終出力だけ。今回は全metric不変、p190 graph/fuzzy full oracleの未達を保持。scriptedは805全RGB/readyが以前と一致、crash39/48とAcid2既存873差・target full/p100/shared publication pendingを隠さない。
