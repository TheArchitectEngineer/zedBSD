<!-- awesome-plan project=zedbsd record=ws132 -->

# WS132: /dev/system の電源管理と PnP の通知、自動 mount、Files の eject

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。p001〜p005・p007〜p009 cleared（p007・p008 の実機は 2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」）。電源ボタンは WS182 の電源のメニュー（2026-10-07 ユーザー））
<!-- retained-after-completion --> Phase phase001・phase004 はその場に残す: src/・userland/・platform/・Makefile・config/ の source か build の file が path を名指す（AGENTS.md §10 の source の comment の掃除の後に消す。2026-10-28 Q1 の方針、q1660）
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: MG003、MG004
Objectives: O2, O3
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

2026-10-03 ユーザー「ejectは挿入の通知もほしいですが、まだそういう機能がカーネルにないですよね。udev的なものです。」「/dev/systemに電源管理とPnP通知、両方入れるのがいいと思います。何を通知してほしいかは、subscriberが指定すればいいと思います。」

kernel の device の追加・削除と電源の事象を `/dev/system` の subscriber に渡し、媒体の通知と mount の daemon、Files の Devices と eject、Linux・FreeBSD の Keiland の同じ抽象化を揃える。

## ユーザーの決定

- 2026-10-04（受け取り手）「/dev/systemをopenしてreadするとき、あらかじめほしいイベント通知の種類を登録するのがいいかもしれないです。また、グラフィカルセッション（Waylandコンポジタ）が起動したとき、Keilandが/dev/systemのイベントを受け取ると思います。…コンソールセッションのとき、/sbin/initがそれを受け取るのがいいのかなあとも思いました。でも、このOSはクライアントOSだから、/sbin/initはその通知、いらないかなあとも思います。うん、いらないですね。Keilandが受け取ることにします。」→ subscriber は read の前に受け取る事象の種類を登録する。受け取り手は Keiland。
- 2026-10-04「全部をベータ1 に」（電源管理・PnP の通知・自動 mount・eject）。
- p001 の D2: 蓋を閉じたら画面を消して lock、15 分以内に開けたら password 無しで自動の unlock（S0i3 までの間）。D3: 自動 mount はせず媒体の追加を通知、double click で /media/ の下に mount。
- 2026-10-05 午後 UAT「Filesでのデバイスのマウントは、マウントするかの確認ポップアップがないと、セキュリティ的にあぶない気がしました。それから、起動ディスクのパーティションは表示しなくてもいいかも。」→ p009。
- 2026-10-07: 電源ボタンは WS182 の電源のメニュー（D1 の「短押しで S0i3」を置き換え）。
- 2026-10-08「規約の整形（各 WS の全文規約の Phase）はベータ3」→ p006 は WS の外の一括の見直しへ。
- 2026-10-13「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」→ p007・p008。

## 結果

- kernel: `/dev/system` の事象の核（購読・read・poll）、UAPI、送り手（ACPI の電源ボタン・蓋・AC・電池、disk、input、USB、network）、`KERN_SYSTEM_GET_POWER`（p002）。noexec と FAT の持ち主（p004）。
- Keiland: backend の事象（`events-zedbsd.c`、Linux・FreeBSD は stub）、compositor の input の探し直しと電池・AC の表示（p003）、蓋の動作（p008）、devices と bar の媒体の icon（p004）。
- volumed（媒体の追加の通知、double click で mount、抜去・eject の口、p004）、libkeiland の volume の口と Files の Devices・Today の icon・eject（p005）、mount の前の確認の card と起動 disk の除外（p009）。
- 証拠: QEMU の T1-106・T1-132・T1-150・T1-186 と host の試験（p008 の蓋は host の 29 checks、QEMU は蓋が無く未実施）。実機は 2026-10-13 の一括の cleared。

## 制限・移管

- 蓋の自動の切り替え（S0i3）は WS052（Sleep）と一緒。電源ボタンは WS182。
- QEMU の試験と全文の規約（p006）は WS の外の一括の見直しへ。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws132-p001 | 設計（D1〜D3） | cleared（2026-10-08 Q1 の判定） |
| ws132-p002 | 事象の核・UAPI・送り手・`KERN_SYSTEM_GET_POWER` | cleared（2026-10-05 Q1、T1-132） |
| ws132-p003 | backend の事象、input の探し直し、電池・AC の表示 | cleared（2026-10-05 Q1、T1-132） |
| ws132-p004 | volumed、noexec と FAT の持ち主、媒体の icon | cleared（2026-10-05 Q1、T1-150） |
| ws132-p005 | libkeiland の volume の口、Files の Devices と eject | cleared（2026-10-05 Q1、T1-150） |
| ws132-p006 | QEMU の試験と全文の規約 | WS の外の一括の見直しへ（Phase の directory なし） |
| ws132-p007 | 実機の UAT | cleared（2026-10-13 ユーザー「UATの結果待ちのものは…clearedに…」） |
| ws132-p008 | compositor の電源ボタン・蓋の動作 | cleared（2026-10-13 同上。電源ボタンは WS182） |
| ws132-p009 | Files の Devices の直し（mount の確認、起動 disk の除外） | cleared（2026-10-05 Q1、T1-186） |

## 記録の所在

Phase の計画・結果・試験（`phaseNNN/`・`tests/`）は削除する（Q1 の pipeline）。p001・p004 の設計（source の comment が引く）は移して残す案あり（Q1 の判断）。媒体の icon の案（`proposals/`）はこの directory に残す。git の commit `fb1f6afee` の `plan/ws132/` にある。Queue の履歴は [plan/history](../history/index.md)。
