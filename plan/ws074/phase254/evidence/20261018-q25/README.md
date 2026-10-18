# q25 native mark producer host partial

native PerformanceEntry/PerformanceMarkをprivate registryへ追加し、markの実記録/返値、constructor非buffering、readonly enumerable common fields/primitive detail/common4 toJSON、finite非負timestamp、required引数/予約名/例外、stable timestamp順のgetEntries/ByName/ByTypeとclearMarksを実装。native brandとmanaged payloadでprototype偽装を拒否し、receiverのactual relevant global/clock/prototypeを使用。38条件が独立Chromeと完全一致、buffer-only/saved entry/辞書Get・filter再入GC/retired child/cross-realm/allocation pressureのnative7条件通過。global公開前の既存helperのnewborn rootも補強。 最終source2d5db3785・frozen verified pairで全1508 harnessはq24比全whole/callback/completion/returncode完全同一、494 raw PASS維持。static1561は844 PASS/717 FAIL、全pixel metrics不変。scripted413は105 PASS/283 FAIL/5 unsupported/20 missing、805 captures全RGB不変。strict48は39 PASS/9 missing不変。DOM37/37・CSSOM12+12、host/target exact2 source warning0、Acid3得点/全inventory100・pixel差0、Acid2既存差873不変。 確認済1481/3805（38.922470%）維持、raw fixed1482。独立probeはprior/future entry各1に改善したがObserverはまだno-opで通知0、known held PASS1は解除しない。複雑detail clone/id/navigationId/measure/resource/event/workerは未実装、p254 wholeはuncleared、q25 host producer scoped itemだけcleared。p255はverified producer outputを前提に次のfinite Queueへ選定可能。p189未attempt275/p100/target full link/runtime/WS全件100%は未達、shared projection/GitHub pending、push無し。

## Sources / evidence

Implementation WIP250893946、publication root WIP2d5db3785、approval242f46b03。summary/各comparison/source-proof/versions/target-root-commands/style-reviewが索引。harness-all-rawは全1508 report/checkpoint/generated原本・IDL・stdio、scripted-all-native-capturesは805の原capture/PNG/ready/headers/provenance、crash-all-rawは48の全raw、acid-all-rawは全画像/DOM/console/layout/style/scoreを保持。全tar member byte read-back済み。native-controlsは独立Chrome原console/HTML、初期未対応helper名によるcompile errorと訂正、tag/cross-realmの初回差分、pressure前後/frozen pair/source hash/全commands/raw、formatter生成関数と各logを保持。WPT tracked clean、期待値/manifest/runnerは変更無し。

Scope: engineはbind/environment.c、window.cのregistry array追加、internal.hの末尾enum/externだけ。private ID既存値/公開ABI2/Wayland境界を維持。新engine module/SDK/sysroot/共有toolchainは変更無し。targetはq02 exact single-source commandの-oのみ変更し2source warning0、full link/runtimeは未実施。hostはheader依存を全再build、測定pairを別folderへfreeze。native helper wpt-ready-renderはq21の同一bytesをコピーし新libと組合わせ、recorded engine_library_sha256を実照合。

## Full C review / limitations

C全文/Guardrail/browser component全文を使用、簡約版無し。clang-format19はproject configを継承しfull標準のmultiline definition/3-clause条件を優先。新規/変更関数とhost fixtureの他の機械的指摘0、raw goto33は全文§14のshared cleanupへのforward-onlyで全件照合、未解消の生成scope違反0。checker自体がany gotoを報告する限界を隠さない。未変更のlegacy helpers/他API/全WS sourceはp100 full conformanceの残差であり、このpartialだけでWS適合を認定しない。

最初のcore buildは既存辞書helper名を誤記し、bind_get_optionの実契約に訂正。Chrome37比較でnative tagと追加borrowed-method probeのrelevant realm差を確認し、実owner global/prototypeをtraceする設計へ修正、最終38で完全一致。GC監査でglobal公開前のnewborn root不足を補い、7つ目のpressure caseを追加したためsuperseded pairでの途中検証を保持し最終pairで全gateを再測定した。archive read-backの初回はmember順が異なりgzip backward seekを繰返したため自分の処理をTERMで停止し、同じ全byte照合をarchive member順に読む実装へ訂正。測定/ブラウザ/判定基準は変更していない。

staticは全report/pixel metricsを保存するが、元runnerの制限で全1561の原画像は保持せず最初のfail2と最終出力だけ。今回は全metric不変、new pixel gainは無い。p190全graph/fuzzy一次oracle適合の残差は維持。scriptedは805全RGB/readyを保持し旧結果同一、既存pinned graph/fuzzy providerを変更無し。

## Known native holdback and remaining APIs

native-observer-prereq.outは実mark返値とprior/future各1を示す一方、通知は0のまま。q23元raw/判断を保存し、producerの達成だけでheld PASSを加算しない。p255 real future delivery/buffered/takeRecords/disconnect/GC/退役停止を実装・検証してから元WPTを再評価する。複雑detail cloning、PerformanceEntry id/navigationId（pinned IDLにも存在）、measure/resource/event/workerは未実装でありfull IDL適合とは主張しない。Acid2の既存873差とtarget full/p100/共有投影pendingも保持。
