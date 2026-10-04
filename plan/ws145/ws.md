<!-- awesome-plan project=zedbsd record=ws145 -->

# WS145: 印刷（printer の daemon・IPP/LPD で PDF を送る、libkeiland の印刷の口、Settings の Printers の頁）

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG006
Related Milestones: MG005
Parent: [Master](../master.md)
Queue: なし（担当と時期は未定）
Resume point: p001（設計）から。
<!-- awesome-plan-current:end -->

## 単一目標

app が libkeiland に PDF の場所を渡して印刷を依頼すると、network の printer（IPP か LPD）で印刷される。printer の一覧と既定の printer を libkeiland から compositor 経由で取れ、Settings の Printers の頁で printer の IP address・port・protocol を設定できる。

## ユーザーの指示（2026-10-04 夜、原文）

「SettingsのPrintersは、プリンタのIPアドレス、ポート、プロトコルだけ設定できる画面を追加してほしいです。libkeilandでプリンタ一覧やデフォルトプリンタをコンポジタ経由で取得可能にしましょう。実際のプリントは、libkeilandにPDFの場所を書いて印刷依頼を投げると、libkeiland-backendがプリンタデーモンが未起動なら起動し、デーモンがIPPやLPDのプロトコルでPDFを投げる、という形にしたいです。PDFが最初のフォーマットでOKで、そのあとPostScriptやベンダー形式への変換フィルタなども実装して広げていけばいいと思います。」

## 範囲（p001 で設計して確定）

1. **Settings の Printers の頁**（今は stub）: printer を足す・消す・既定にする。設定項目は IP address・port・protocol（IPP か LPD）だけ（名前は自動か入力は設計で決める）。
2. **libkeiland の口**: printer の一覧と既定の printer の取得（compositor の拡張 kl_system_manager_v1 を経て libkeiland-backend）、印刷の依頼（PDF の path を渡す、job の状態の通知・取り消しは設計で決める）。app は OS の口を持たない（Guardrail の「配置」「app と設定」）。
3. **libkeiland-backend**: 印刷の依頼を受けて、printer の daemon が起動していなければ起動し（zedBSD の service の仕組み、WS002）、job を渡す。printer の設定の保存の場所（system の設定か利用者の設定か、書き込みの権限）は設計で決める。
4. **printer の daemon**（新しい userland の daemon）: job の queue（spool）、IPP（RFC 8011 の Print-Job、HTTP の上）と LPD（RFC 1179）で PDF を送る。最初の形式は PDF だけ（printer が PDF を受ける前提。受けない printer は失敗を返す）。
5. **後の段**（別の Phase）: PostScript・vendor の形式（PCL・raster など）への変換の filter を足して対応する printer を広げる。printer の発見（mDNS/DNS-SD）は後の候補。
6. Linux・FreeBSD の Keiland: backend で CUPS などの既存の仕組みを包む（設計で決める）。
7. 外部の実装を取り込む場合の license の監査（Guardrail・設計方針）。

## Phase（案）

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| ws145-p001 | 調査と設計（daemon の構成・spool・IPP/LPD の最小の実装、設定の保存と権限、libkeiland・拡張・backend の口、Settings の頁、試験の方法: host の IPP/LPD の模擬の server と QEMU、実機の printer） | planning | — |
| ws145-p002 | printer の daemon（spool、IPP と LPD で PDF） | planning | p001 |
| ws145-p003 | libkeiland-backend・compositor の拡張・libkeiland の口（一覧・既定・印刷の依頼・daemon の起動） | planning | p001、p002 |
| ws145-p004 | Settings の Printers の頁（IP address・port・protocol） | planning | p003 |
| ws145-p005 | 変換の filter（PostScript・vendor の形式） | planning（後） | p002 |
| ws145-p006 | 全文の規約の確認と回帰 | planning | p002〜p004 |
