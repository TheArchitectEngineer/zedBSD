<!-- awesome-plan project=zedbsd record=ws074-p256 -->

# ws074-p256: Window ErrorEventによる未処理例外通知・harness failure fidelity

Status: in-progress
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: ws074-q20261018-27-i01（native event prerequisite partial）
Origin: [p255 q26 callback failure control](../phase255/phase.md)
Prerequisites: 既存native EventTarget/Event/window timer/VM例外の実出力（p077/p083/p086/p209等）、Guardrail/C全文/browser component全文。p255 wholeまたはconsumer source出力を実装前提にしない。

## Expected / observed / impact

callbackでassert_unreachedをthrowした同じ原JS＋公式testharness/reportについて、q26 nativeはconsoleにUncaught Errorを出しながらcompletion.status=0/whole PASS、Chromeはcompletion.status=1/whole harness_errorになる。bind_report_exception→window_report_exceptionがconsoleだけに出し、Window ErrorEventをdispatchしない既存経路が原因。native Observerの実通知・callback例外継続自体は機能したが、公式harnessへ失敗が届かないためp255例外通知条件とheld PASS1再認定の前提が未達。q25 raw PASS494の全stdout/stderrを走査しUncaught行を持つ既存PASSは0、確認済み1481は現時点で変更しない。

[q26 evidence](../phase255/evidence/20261018-q26/README.md)へraw/proof/同一JS/Chrome/native原consoleを保存済み。既存Phase/activeとarchived Bug index/ticketsをErrorEvent/uncaught harness/exception consoleで検索し同一tracking recordは見つからなかった。共有Bug Board/Issue公開はQ1所有のためpending、当面このhandling Phaseへ再現/影響/調査/再開条件を保持する。

## Own objective / design candidate

通常browser production経路で、同一origin Windowのclassic/timer/native callback未処理例外をgenuine ErrorEventとして実Window EventTargetへ通知し、公式testharnessの既存error listenerが失敗を受け取れるようにする。native ErrorEventのconstructor/prototype/readonly message/filename/lineno/colno/errorとbrand、error payload/文字列/元exception/WindowのGC・退役lifetime、reporting recursion/cancel/default consoleを扱う。Window.onerrorのlegacy handlerやPromise unhandledrejection、cross-origin redaction/source metadata等の範囲はコード/一次資料を読んでfinite Queue前に正確に選ぶ。未選定のworker/module/source redaction/Promise semantics等を達成と主張しない。

既存bind/event.c/window.c/internal.hでnative Event payloadとregistryを使い、ErrorEventをJSで補完したりrunnerで例外文字列から結果を書換えない。公開ABI2/Wayland/source module/SDK/sysroot/共有toolchain/WPT/runner/manifest/期待値を変更しない。targetはq02 dry-runの既存event/window（必要な既存sourceのみ）exact single-source commandで-oだけ変更可能。Guardrail/C全文/browser component全文、format19/全文manual/機械検査を適用する。

## Verification / bounds / dependencies

独立Chrome/nativeのactual global error listener、constructor/brand/readonly/元error identity/default prevention/再入・GC・retired child、setTimeout等の通常callback例外、公式testharnessでの意図的失敗が必ず非PASSとなるnegativeと実positiveを検証。Observerを使わないtimer failureでも前提を独立に証明し、本Phase→p255への依存を循環させない。p255の元callback failure controlとheld IDはその後の再評価に再利用可能。

同じfinal pairでfixed全1508/1561static/413scripted/48strictとDOM/native producer/consumer/host/target/style/Acidを検証する候補。新ErrorEventが以前の偽PASSを顕在化させる場合は旧console/source/allow_uncaught_exception設定/一次harness handlerと照合し、本当の後退と区別して正確に集計する。以前のPASSを維持するために失敗通知を抑制しない。実装/検証は新exact finite Queue選定後、各最大90分。未達はunclearedと証拠・resumeを保持する。

Dependency graph: verified native Event/Window/timer/VM → 本Phaseの実Window ErrorEvent出力 → p255 callback failure fidelity/held case再評価。p255の既存consumerは調査contextであって本Phaseのwhole prerequisiteではない。

Primary reference: [HTML report an exception](https://html.spec.whatwg.org/multipage/webappapis.html#report-an-exception)、pinned WPT interfaces/html.idl/resources/testharness.jsのerror listener。実装候補を設計してもsourceの実行許可にはならない。

Event ws074-p256-added-from-q26-oracle-gap-20261018: p255のintentional callback failureがnative PASS/Chrome harness_errorとなる実証を受け、通常Window ErrorEventの責務を本Phaseへ分離。p256→p255のnamed-output依存を追加しcycle無し。全3,805分母/既存score/原reportは保持、次finite Queue選定までsource未実装。shared projection pending。

Event ws074-q26-error-prerequisite-resume-20261018: [origin evidence](../phase255/evidence/20261018-q26/oracle-gap-proof.json)でnative PASS対Chrome harness_errorを実証し、q26 attempt unclearedとして終了。通常Window error eventのnamed outputがp255再開条件。本PhaseはまだQueue未選定、次のfinite source scope前に実code/一次error algorithmを詳細化する。

Event ws074-p256-q27-selected-20261018: VMはthrow line/columnのみ保持しscript source/muted metadataをcallbackへ伝播していないため、まずnative ErrorEventと実Windowのlegacy onerror dispatchを独立partialとして選定。reporter接続を次finite scopeへ残す内部手順の詳細化で、whole目的/受入/依存は維持。q27のexact scope/criteriaは[Queue](../queue.md)先頭を参照。原q26 negative/確認済1481/held1を保持。

Event ws074-p256-q27-primary-chrome-usv-20261018: pinned html.idlとHTML本文はfilenameをUSVStringと規定するが、Chrome153実測はlone surrogateを保存する（DOMString挙動）。本partialの選定済みUSVStringは一次IDL通り維持し、独立28対照中この1条件のChrome差を明示する。未対応Proxy constructorで独立fixture全体が停止する初回native出力を保存し、brand対照はgenuine別Event/偽prototype/plain/nullの実対応receiverで継続する（Proxy対応を達成と主張しない）。原WPT/期待値/score不変。

Event ws074-p256-q27-build-sequencing-20261018: initial host build呼出しの完了をsession statusで確認せず後続host buildを開始し、一時的な重複を発生させた。中間pairは正式測定に使わず、全buildの終了確認後に一度だけhost buildを直列実行してからfinal pairをfreezeする。source/期待値は以後固定し、final pairのsource/object/hashを確認する。

Event ws074-p256-q27-final-format-proof-20261018: final corpus開始直後の全文監査で既存bind_event_create内5-clause条件のformatterによる一行化を見つけ、規約通り改行だけを復元。全非空白token/string不変、同一host flagsによる再compileとdebug除去後のobject全bytes一致をformat-equivalence.jsonへ保存。frozen pairは変更せずruntime同値のfinal sourceを検証対象とし、target exact event/windowを再compile warning0、checker19の許可済みforward cleanup以外0を再確認する。追加の意味変更や期待値緩和は無い。

Correction ws074-p256-q27-final-format-proof-corrected-20261018: 前イベントの「一行化を見つけ復元」は誤った事前記述だった。保存したbefore/after SHA256は共に4774f871d2cafe3b762c40e97e6898a2515d80101bccc042a43ab5dd2e3638f8で全bytes同一。既存5-clause条件は既に正しく改行されており、追加formatter置換はno-op、source変更は発生していない。format-equivalence.jsonを実測通り訂正。final pairはe2111e1b2のsourceそのものに対応し、余分なruntime同値compileも同一である。履歴の誤記をこのdated correctionで明確化する。

Event ws074-p256-q27-declaration-order-20261018: C全文§2に従い、event.cのconstant initializerが必要とする44宣言だけをtables前に残し、14 helpersをtables後の通常blockへ移動。全宣言multisetと他token/stringを保持、host同flags compile warning0・debug除去後object全bytes一致をprototype-order-proof.jsonへ保存。final sourceはruntime同値の宣言順整理を含み、frozen original source-proofも別保存。target exact2source warning0を再確認。q26環境helperの同種残差はorigin p255/history/p100へ明示し、q27源のscoped conformanceと区別。
