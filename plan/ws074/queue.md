<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-07 / Grid used track CSSOM

Status: active（2026-10-18 11:42 JST）
Owner: このCodex chat単独。
Approval: [ユーザーの自走指示](autonomy.md)を次の有限範囲へ適用。
Timebox: 最大90分（13:12 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-07-i01 | [p243](phase243/phase.md) primary horizontal partial | 既存horizontal grid/inline-grid solverの最終used trackをarena snapshotに保持し、private host queryからcaller-owned vectorへcopy、template columns/rowsのresolved CSSOMへ渡す。固定/%/fr/auto/implicit/repeat/collapse/emptyとdynamic更新を独立Chromium比較。公式Grid502をq03とwhole/subtest比較し旧PASS後退0、host/target compile warning0、変更関数C全文review/format、Acid3差0/Acid2非増加。検証runnerのTemporaryDirectoryをretainへ直す。child/vertical/line-name/subgrid等は残しwhole未達、solverの別欠陥・public ABI/WPT/Masterは変更しない | in-progress | p221 existing solver、q03 Grid raw、q06全静的census/後退0、2e3680e6b |

Dependency graph: q03/q06 verified baseline + p221 solver → p243 partial。
Upcoming Work Outlook: child/verticalのowned copy、line names/subgrid、実track sizing、replaced ratio、runner待機/variant/worker coverage。

---

<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-06 / 固定reftest全静的候補census

Status: finished（2026-10-18、census/診断部分cleared）
Owner: このCodex chat単独。
Approval: [ユーザーの自走指示](autonomy.md)を本有限範囲へ適用。
Timebox: 最大90分（13:03 JSTまで）。runner全体を5,300秒・個別render20秒で制限する。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-06-i01 | [p190](phase190/phase.md) static census partial | 固定reftest1,974 ID中、既存runnerの静的候補全1,561 IDを800×600・固定font・参照連鎖/fuzzyの既存判定で測定。未対応413 IDとcrash48を未達として保持。全ID/whole/画素差/checkpointを保存し、比較可能なq1062全ID snapshotとq1202既知4後退を照合。browser/runner/WPT/ABI/Masterは変更せず、次の一般修正候補を示す | cleared | p173固定manifest、d328827f5の固定browser/libbrowser、q1034 reference-chain runner |

Dependency graph: 固定manifest + p242 q05 output → p190 static census partial。p189/p242 whole clearanceは前提ではない。
Upcoming Work Outlook: Grid/Flexの共通原因、scripted402/reference5/variant6、testharness未対応328、crash48。

Outcome: 静的1,561件836 PASS/725 FAIL、error/timeout0。全FAIL725を開始前binaryでも照合し今回sessionの後退0。未対応413保持、crash48は未更新。[証拠](phase190/evidence/20261018-q06/README.md)。p190 whole uncleared。

---

<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-05 / replaceChildren observer通知

Status: finished（2026-10-18、observer partial cleared）
Owner: このCodex chat単独。
Approval: [ユーザーの自走指示](autonomy.md)を適用。
Timebox: 最大90分（12:58 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-05-i01 | [p242](phase242/phase.md) observer partial | replaceChildrenでsource parentからの移動を先に通知し、targetの交換はadded/removed全nodeを1recordへ集約する。private sequence API・GC root snapshot・sibling fieldを追加。普通のinsert/append/prependの契約は維持。独立observer例をChromium比較、既存DOM/GC、公式13件、build warning0/full C review。公開ABI/WPT/Masterは不変 | cleared | 5008906bd基本交換、q04 observer残課題 |

Dependency graph: p242 q04基本交換 → p242 q05 observer partial。
Upcoming Work Outlook: createHTMLDocument/HTMLCollection iterable依存、静的reftest census、runner未対応328。

Outcome: observer24項目Chromium一致、DOM25/25、補助16→18 callback PASS、全31 callback取得。公式whole PASS増分0。p242 whole uncleared。[証拠](phase242/evidence/20261018-q05/README.md)。

---

<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-04 / ParentNode.replaceChildren 基本交換

Status: finished（2026-10-18、basic partial cleared）
Owner: このCodex chat単独。
Approval: ユーザーの[自走指示](autonomy.md)を以下の有限範囲へ適用。
Timebox: 最大90分（12:52 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-04-i01 | [p242](phase242/phase.md) basic partial | Element/DocumentFragment/DocumentのreplaceChildrenを登録。全引数のnode/string変換とGC保持、fragment化と順序/重複、replace-all用validation、native削除/挿入通知を実装。独立例・補助WPT・公式cleanup群を前後比較、host warning0・C全文review・Acid差非増加。既存observerが単node通知ならbatchingは後続に残しwhole未達とする。公開ABI/通常append/prepend/WPT/Master変更なし | cleared | q03全ID/rawと9b94ebe38固定binary、既存DOM insert/remove/adopt |

Dependency graph: q03診断出力 → p242 basic partial。p189 whole clearanceは前提ではない。
Upcoming Work Outlook: observer batching、公式cleanup群のCSS本体、静的reftest census、runner328未対応。

Outcome: DOM39項目・全24/24、公式cleanup13の12件がFAILまで進み1件no_completion、whole PASS増分0。補助16 callback PASS、observer等は未達。[証拠](phase242/evidence/20261018-q04/README.md)。

---

<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-03 / Interop2025 testharness全候補の最新census

Status: finished（2026-10-18、測定部分cleared）
Owner: このCodex chat単独。
Approval: ユーザーの[自走の指示](autonomy.md)を本有限範囲へ適用。
Timebox: 最大90分。期限内のcheckpointを保存し、残りを未実行として残す。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-03-i01 | [p189](phase189/phase.md) partial | 固定manifestの1,783 testharness IDを再分類し、既存runner適格1,455 IDを最大128件のshardで全件測定。固定revision/font/5秒settle/20秒process、raw callback/gzip/checkpointを保存。ID・whole/subtest結果と未対応328IDを照合し、共通失敗群と次の有限修正候補を示す。browser/runner/WPT/ABI/Masterは変更しない | cleared | p173の固定manifest、9b94ebe38の固定browser/libbrowser |

Dependency graph: p173 manifest + p241 frozen output → p189 census partial。
Upcoming Work Outlook: 静的reftestの全体census、上位の共通原因、scripted/variant/origin/workerのrunner coverage。

Outcome: 全適格1,455 IDを測定、485 PASS・旧481 PASSから後退0。未対応328保持。[証拠](phase189/evidence/20261018-q03/README.md)。p189 whole uncleared。共有投影pending。

---

# ws074-q20261018-02 / flex ratio の stretch cross size

Status: finished（2026-10-18、host部分cleared）
Owner: このCodex chat単独。
Approval: ユーザー「では、当分の間、100%を目指して自走をお願いします。」を[自走の契約](autonomy.md)と次の有限範囲へ適用。
Timebox: 最大90分。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-02-i01 | [p241](phase241/phase.md) host partial | 非置換の横書きrow/column、nowrap・definite cross size・stretch・auto cross size・非auto cross marginのratio base/minimum。明示px/%も共通化。frame/margin/minmax、wrap/auto margin非適用の独立例、固定WPT前後、build warning0、全文review、Acid差非増加 | cleared（host40例・WPT149→152・後退0、[証拠](phase241/evidence/20261018/README.md)） | p234 ratio・p240自動最小サイズの統合済み出力 |

Dependency graph: p234 ratio → p240 minimum → p241 partial。
Upcoming Work Outlook: flex-basis:content区別、replaced ratio、固定Interopの最新censusとrunner coverage。全3,805 IDの達成は未確認。

Outcome: p241 host部分cleared、production link/target確認はpending。Acid3差0、Acid2差873。次は公式2025の最新census。
History: [確定したscopeと結果](history/ws074-q20261018-02.md)。

---

# ws074-q20261018-01 / flex item の aspect-ratio（Codex）

Status: finished（2026-10-18、host の有限部分 cleared、p240 whole uncleared）
Owner: この Codex chat の単独 executor。既存の B1 は p239 後に停止済みとして引き継いだ。
Approval: 現ユーザー「では、WS074を続けてください。」と、既存の後続有限 Queue 事前承認・p234/p235 に記録された layout 改善の残りを適用。
Timebox: 最大90分。
ID: main の q1203 以降との衝突を避け、この lane 内では `ws074-q20261018-01` とする。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-01-i01 | [p240](phase240/phase.md) partial | 横書き flex item の preferred aspect-ratio に関する自動最小サイズと flex basis の共通原因。row/column・cross size・basis 0・box sizing を最小例と固定 WPT で比較し、warning 0 build、短い host 検証、Acid3 差0/Acid2 非増加、変更関数の全文規約を確認。QEMU/実機は今回の部分範囲外 | cleared（[証拠](phase240/evidence/20261018/README.md)、WPT142→149・後退0） | p172 の統合済み engine、p234 の ratio 実装 |

Dependency graph: p172 engine → p234 ratio → p240 partial。
Upcoming Work Outlook: fixed009/045/046のstretch cross size、021/022のflex-basis:content、replaced ratio、targetの確認。inner scroll・transform・runner coverageも残る。次の候補であり実装済みとはしない。

Outcome: host の29例・WPT257 ID・amd64 buildを検証し、部分cleared。p240 wholeとWS074は未達。仕様とChromiumの1例の差、未実施の関門、再開候補は[Phase](phase240/phase.md)に記録。Master/共有Queue/GitHubの投影はpending。
History: [このcycleの確定した範囲と結果](history/ws074-q20261018-01.md)。

---

# q1200・q1201 / WS074 Grid の track と縦書き子、Flex absolute の static position（B1）

Status: finished（2026-10-14 17:25–18:10 JST、B1。承認と timebox は q1199 と同じ）

| Attempt | Phase | Exact scope | State |
| --- | --- | --- | --- |
| q1200-i01 | [ws074-p226](phase226/phase.md) partial | auto-fill/auto-fit、水平 grid の縦書き in-flow 子、置換要素の stretch、fr<1、computed の align 名。旧 PASS 後退 0、Acid3 差 0、Acid2 非増加、warning 0 | cleared（whole 379→427） |
| q1201-i01 | [ws074-p227](phase227/phase.md) partial | flex の absolute 子の static position と alignment keyword の構文。同上の確認 | cleared（whole 427→458） |
| q1202-i01 | [ws074-p227](phase227/phase.md) partial | flex align-content・wrap-reverse、inline-flex、blockify、overflow 2値、sideways parse、safe。reftest 682→815、PASS→FAIL 4 | cleared（whole 458→481。reftest の4件は未解決として記録） |


# q1199 / WS074 Grid content distribution and absolute-child containing block (B1)

Status: finished（2026-10-14 17:05–17:50 JST、B1。Codex から引き継ぎ。承認: ユーザー「WS074を引き継いでWPTの続きをできますか？最大３時間のサブエージェントでお願いしたいです。」と既存の事前承認「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」の延長。Timebox 90分）

| Attempt | Phase | Exact scope | State |
| --- | --- | --- | --- |
| q1199-i01 | [ws074-p226](phase226/phase.md) partial | 水平 grid の content distribution・auto track stretch・absolute 子の grid line containing block・百分率 gap。旧 PASS 後退 0、Acid3 差 0、Acid2 差非増加、warning 0 | cleared（[証拠](phase226/evidence/20261014-q1199/run.md)） |


# q1198 / WS074 framed horizontal Grid second vertical auto-inset child

Status: finished（2026-10-14 14:13 JST 開始、14:15 ごろ Codex の契約の使用量の上限で中断。2026-10-14 Q1 が未 commit の差分を main に commit）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（15:43 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1198-i01 | [ws074-p217](phase217/phase.md) partial | positioned horizontal-tb LTR/RTL固定2×2 inline-grid、px tracks/gap0、親非対称px margin/border/padding、直接vertical-rl auto-size ASCII absolute子row1/col2明示span1・全auto inset・justify-self:center・子複合frameで、縦inline測定とGrid area中央static Xを最小修正。独立LTR/RTL最小ページChromium矩形一致、固定non-static012水平2 callback先頭2子全検査通過、縦2 callback初回失敗非悪化、旧007–011 whole PASS、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加。他子/auto line、WPT/runner/ABI/Master変更なし。012 wholeは基準外 | cleared（2026-10-14 B1 が evidence を判定: [README](phase217/evidence/20261014-q1198/README.md)。Chromium 一致、012 水平2 callback 先頭2子通過、Acid3 差0/Acid2 1865、007–011 PASS） | q1197先頭子 |

Dependency graph: q1197先頭子 → q1198-i01。p217/p189/p173 wholeは部分受け入れに含めない。

Upcoming Work Outlook: non-static008–012 frame variants、auto lines、stretch005–016、RTL字形視覚、RTL行軸017、baseline、runner未対応。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
