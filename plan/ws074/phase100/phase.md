<!-- awesome-plan project=zedbsd record=ws074-p100 -->

# ws074-p100: Acid3 100/100・pixel完全一致

Status: planned（2026-10-13 q1011: Acid3得点100/100・pixel差0/480000。Acid2 exact・target 等の whole 条件は未達）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p099](../phase099/phase.md) cleared、[p172](../phase172/phase.md) whole-Phase clearedと実際に統合・検証された出力。branch側の同じ論理IDの記録はp172で意味的に照合する。

## 目的と範囲

固定した歴史的Acid3を現行libbrowser/browserで動かし、画面上の100/100と固定参照画像とのpixel完全一致を両方満たす。2026-09-30に計画された100/100・Uncaught/crash/timeout 0に、2026-10-02ユーザーがpixel完全一致とfail 0を追加した。

実行Queueの選定前に、p172が取り込むbranchのp100成果/証拠を照合し、Acid3資産のcommit、runner、viewport、font、参照画像、色空間、比較方式、反復回数と時間の上限を固定する。既存のAcid2とWS107独立componentの回帰を保つ。一般的なCSS/layout/DOM/描画修正を失敗の原因ごとに有限のQueueへ分ける。参照画像や試験を出力に合わせて改変しない。

## Clearance

- 固定したAcid3が表示する得点100/100、参照との画素差0、試験のfail/Uncaught/crash/timeout 0。試験環境・入力・出力画像・比較結果を保存する。
- 該当するbrowser/libbrowserとWS107の回帰、build、sourceの全文規約を確認する。未実施/skipをPASSとして数えない。
- p172のwhole-Phase clearance前に実装を始めない。後続の[p173](../phase173/phase.md)は p188 の main 統合済み Acid3 出力のみを前提にし、p100 whole の Acid2 exact/target gate は要求しない。p101 の whole-Phase 前提は維持する。

Event ws074-p173-interop-resume-20261013: p173 が 2026-10-13 のユーザー指示で WPT Interop 2025 を先行するため、依存出力を明確化した。p188 の main 再計測で Acid3 得点100/100・RGB差0が確認済み。p100 whole の Acid2 exact/target gate は本Phase内に残り、p173 はその結果を要しない。p100 status/clearanceと p101 の前提は変更しない。GitHub publication保留。

## 制約・再開

[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)を適用。WS107の内容不変移動style例外は流用しない。着手時の設計/試験条件をphaseへ確定し、exact-scope Queue承認を得る。現時点は計画のみ、source変更/試験/Queue実行なし。

Event ws074-dedicated-interop2025-20261002: ユーザーがAcid3のpixel単位100%・fail 0を指定。WS074のp100を強化。WS/branch側p100との照合はp172へ接続。GitHub publication保留。

2026-10-03 JST / p172-prerequisite: [p172 whole clearance](../phase172/import/checkpoint98/README.md)と実際の最終browser shell出力を確認。p100はplannedのまま、固定Acid3の入力・runner・参照/viewport/font・比較方式・時間枠を定めた新Queueの選定/承認が必要。Acid3の実行やpixel達成は未実施。共有Queue ID衝突はAgent Aの投影待ち。

2026-10-03 JST / next-queue-proposal: [90分のread-only Acid3 baseline案](browser3/next-queue-proposal.md)を準備。WPT固定commit/必要font/host browserの存在をread-onlyで確認。Queue IDはAgent Aの衝突解消・承認待ちで、実行なし。

2026-10-03 04:45 UTC / p100-baseline-approval: userが[90分scope](browser3/next-queue-proposal.md)を「承認し、A に ID 割当を依頼する」と回答。[承認記録](browser3/approval-20261003.md)、AへのID依頼（branch の `plan/agents/browser3/next-id-request.md`、main に無い）。ID/Queue activation待ち、p100 planned・未着手、Acid3未実行。

2026-10-05 Q1 records reconciliation: ユーザーが WS074 を Codex から Q1 に戻し、レンダリングの改善（Acid3 の pixel を含む）をユーザーの指示まで止めた。このPhaseは planned のまま（main の p100 を実行した Queue は無い）。
- 2026-10-03 の [90 分の baseline の承認](browser3/approval-20261003.md)は Queue ID が付かず（Agent A への ID の依頼は main に記録が無い）、実行されなかった。2026-10-05 の停止の指示の下ではこの承認を使わない。再開はユーザーの指示の後、新しい main の Queue ID と承認で行う。
- [案](browser3/next-queue-proposal.md)の `build/browser3-p172/plain/browser` は 2026-10-03 のリポジトリの作り直しで消えた build を指す。再開の時は今の main の source から host の build を作り直し、2026-10-04 の試験の image の方針（AGENTS.md の「試験の image の作り方」）に合わせる。
- Acid3 の得点 100/100・pixel 37.04%（302208/480000 の画素が違う）は取込みの途中の [checkpoint02](../phase172/import/checkpoint02/README.md)（q579、host の plain）の計測で、p172 の最終の source では再計測していない。branch 側の p100 は「得点だけ」の旧い条件で cleared と報告され、[semantic-index](../phase172/import/semantic-index.json) で historical evidence として保存した。今の p100 の条件（pixel の完全一致・fail 0）は未達。

## 2026-10-13 再開指示と有限の進め方

ユーザー「Acid3のすべてのテストでピクセル単位の100%一致になることをゴールに、作業してください。」。2026-10-05 の描画改善停止を Acid3 について解除し、本 Phase の目標を再確認した。Acid3 の個別100項目は DOM の得点で、pixel 判定は 800×600 の test 最終画面と固定 reference 最終画面の RGB 480000画素について差 0 を意味する。100項目それぞれに独立の reference 画像があるとは扱わない。比較入力や判定を出力に合わせて変更しない。

最初の有限単位は [p178](../phase178/phase.md) の試行。現行 source と固定 WPT の取得・build・基準計測、差の領域分け、原因を絞った1領域の修正と回帰を対象にする。その後は実測した原因ごとに対象 file・期待する画素差・回帰・時間枠を定めた Phase/Queue を Q1 が選定する。各修正後に得点100/100、Acid2 exact、全体の差の非増加を確認し、最後に現行 source の全画面差0・例外/fail 0・必要な build/全文規約を検証する。p178 の1領域が一致しても p100 は cleared にしない。

2026-10-13 の checkout には固定 WPT の `build/ws074-suites/wpt` と B1 worktree が無く、共有 Queue に p100/p178 の項目が無い。Q1 が Queue と担当を記録する前に implementation attempt は始めない。担当の削除禁止と現行 runner の `unlink()`・suite fetch の `rm -rf` の不整合、および Mahora への font 変更は [p178 の実行前の整合](../phase178/phase.md)で扱う。`master.md` はこのセッションで変更しない。

2026-10-13 follow-up: ユーザーがこのセッションによる Queue 管理を指示し、[q1000](../queue.md) を p178 の有限の試行として実施・終了した。固定 WPT と host build の準備後の現行計測は得点100/100、pixel一致37.0717%、差302056/480000、Acid2一致98.4458%。参照側の `reference.png` の箱が描かれず差が画面全体に広がったので、p178はuncleared。p100の条件は未達。詳細と画像は[p178](../phase178/phase.md)。上段の「WPT が無い」「Queue が無い」は開始前の状態を記す。

2026-10-13 q1001 follow-up: ユーザーが [p179](../phase179/phase.md) の範囲を承認し、参照画像が描かれない一般的な原因を修復した。同じ固定条件で Acid3 得点100/100・例外0、差56647/480000（88.1985%一致）、Acid2差1865/120000。p179 は cleared、p178 の旧 q1000 attempt は uncleared のまま。全画面差0と Acid2 の現行回帰は未達であり、本 Phase は planned。次の局所試行には新しい Queue の exact scope と承認が必要。[q1001の証拠](../phase179/evidence/20261013-q1001/report.json)。

2026-10-13 q1002 follow-up: ユーザー承認の [p178得点位置試行](../phase178/next-queue-proposal.md) は部分 scope cleared。参照得点の `right:0`/auto幅の位置を修復し、得点ROI差9572→800、全体差56647→43486、score100/100・例外0、Acid2差1865で不変。[q1002の証拠](../phase178/evidence/20261013-q1002/report.json)。得点領域と全画面の pixel 差0は未達で、この Phase は planned のまま。次の有限試行には別の exact scope/Queue が必要。

2026-10-13 q1003 follow-up: ユーザー承認の [p180 iframe子描画修正](../phase180/phase.md) は cleared。DOM/frame load を保って iframe の子を親文書の layout/paint から除き、Acid3差43486→42921、score100/100・例外0、Acid2差1865で不変。[証拠](../phase180/evidence/20261013-q1003/report.json)。本文 y373.50 / reference y350 の差、全画面差0、Acid2現行exactは未達。この Phase は planned のままで、次の有限試行には別の exact scope/Queue が必要。

2026-10-13 q1004 follow-up: ユーザー承認の [p181 HTML type selector修正](../phase181/phase.md) は cleared。`type=HIDDEN` の input が UA `input[type=hidden]` と一致し、form line24→0、本文 y373.50→349.50。Acid3差42921→18596、score100/100・例外0、Acid2差1865で不変。[証拠](../phase181/evidence/20261013-q1004/report.json)。全画面差0、本文の残り0.5差、Acid2現行exactは未達。この Phase は planned のままで、次の有限試行には別の exact scope/Queue が必要。

2026-10-13 q1011 follow-up: [p188](../phase188/phase.md) の修正で固定 Acid3 は得点100/100、inventory100/100、例外0、RGB 画素差 **0/480000**（[report](../phase188/evidence/20261013-q1011/report.json)）。ユーザーが指定した Acid3 個別項目と最終画面 pixel 一致の目標は満たした。p100 whole の既存条件には過去 exact の Acid2 回帰と target/WS107 確認、source 全文規約が含まれる。現行 Acid2 は差1865/120000で q1010 から非増加だが exact ではなく、QEMU/別OSと共有 main 最新 HEAD の全体確認は未実施。このため Phase 状態は planned のままで whole clearance は宣言しない。Acid3 の目標達成を理由に追加 Queue を自動選定しない。

2026-10-13 main merge follow-up: WS074 の145 file を `main` commit `1f9cc35ca` に取り込み、main host build warning/error0 と固定 Acid3 得点100/100・inventory100/100・例外0・差 **0/480000** を再確認（[report](../phase188/evidence/20261013-merge-main/report.json)）。Acid2差1865/120000は不変で exact に達せず、target/別OS確認も未実施。Phase whole は planned のまま。
