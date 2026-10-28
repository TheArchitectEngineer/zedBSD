<!-- awesome-plan project=zedbsd record=ws033 -->

# WS033: networking サービスと有線インタフェースの管理

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」: 有線 LAN の後挿し・抜去・networking.wait は p001 で QEMU で通り、code は main。p002 は実機の立会いだけ、p003 の規約は WS の外の一括の見直しへ）
Completed: 2026-10-27（q1608 の精査）
Primary Milestone: MG005
Related Milestones: MG003
Objectives: O1, O2, O3
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

有線インタフェースを networkd が常駐して管理し（ケーブルの抜き差しに追従して DHCP・static を構成する）、起動時の `networking` サービスが「どれか一つのインタフェースが到達可能なアドレスを持つ」まで待てるようにする。対象の Latitude 5330・5320 は RJ45 を持たず、有線は USB の LAN（RTL8156 の CDC NCM、ECM）だけ。

## ユーザーの決定

- 2026-10-02: ネットワークが最上位（fg019、ベータ1）。
- 2026-10-08: 「規約の整形（各 WS の全文規約の Phase）はベータ3」（p003 は WS の外の一括の見直しへ）。
- 2026-10-13: 「これ以上ベータ2に機能追加しないつもりなので、実装を終わらせましょう」（p001 の q958）。
- 2026-10-27（q1608）: 「…masterとWSを修正して、completedやcanceledにしてください。」「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」

## 結果

この WS は初め計画の記録を持たないまま実行された。経緯と決定は [notes.md](notes.md)。

- 判断と実行を分けた `managed-lan`（IDLE・PENDING・CONFIGURED・UNCONFIGURED）。装置を開かず次にすべきことを答えるだけなので host で試せる（`make managed-lan-host-test`、31 項目）。
- carrier の変化は既存の route socket（`RTM_IFINFO`）で受ける。新しい kernel interface は要らなかった。
- `net lan enable`・`net lan disable`・`net startup`、`/etc/service.d/networking`（oneshot）。`net startup` が `rc.conf` の `networking.wait` を読んで待つ。
- DHCP が取れなければ MAC から 169.254.x.y を導く（有効なアドレスとは扱わない）。
- p001（2026-10-14 Q1 の判定）: QEMU で USB の LAN の後挿しの DHCP と fetch（L1）・抜き差し・`networking.wait`（L3）が T1-480（T1-513・T1-517 でも）PASS。BUG-168（後挿しで up しない）を直した。

## 制限・移管

- 実機の確認は未実施: L2（carrier の down/up、QEMU の usb-net の set_link は実機の p002 へ回した）と L4（5330・5320 の RTL8156 の抜き差し）。
- dp8390 はリンクを検出しないので抜線を検出できない。
- 規約の全文の見直し（p003）は WS の外の一括の見直しへ。
- 関連の Bug: BUG-169・BUG-174・BUG-212・BUG-213・BUG-222（UAT 2026-10-04・10-06 の指摘）は各 ticket で扱う。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws033-p001 | USB の LAN の後挿し・抜去・carrier の変化と `networking.wait` を QEMU で通す（L1〜L3） | cleared（2026-10-14 Q1、T1-480・T1-513・T1-517、L2 は実機へ） |
| ws033-p002 | 実機の確認 L4 | planning のまま WS を閉じた（実機の確認は未実施。2026-10-27 ユーザー「実機確認だけが…」） |
| ws033-p003 | WS033 の source の全文規約の確認 | planned のまま WS を閉じた（規約の全文は WS の外の一括の見直しへ） |

## 記録の所在

Phase の計画・結果は削除する。git の commit `fb1f6afee` の `plan/ws033/` にある。試験は `plan/tools/` へ移す（Q1 の OK の後）。Queue の履歴は [plan/history](../history/index.md)。
