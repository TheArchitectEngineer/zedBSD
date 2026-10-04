<!-- awesome-plan project=zedbsd record=ws132 -->

# WS132: /dev/system の電源管理と PnP の通知、自動 mount、Files の eject

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: MG003、MG004
Objectives: O2, O3
Parent: [Master](../master.md)
Queue: q707 / q707-i01（P1 generation17、2026-10-05）
Resume point: p001（設計）を書いた（[phase001](phase001/phase.md)、D1〜D3 は人間の判断待ち）。p002（kernel の事象の核・UAPI・送り手・`KERN_SYSTEM_GET_POWER`）を実装し build と host の試験まで済んだ（[phase002](phase002/phase.md)）。QEMU の試験を T1 に依頼中。次は D1〜D3 の後に p003・p004。**ベータ1 に入れる、範囲は全部**（2026-10-04 ユーザーの決定「全部をベータ1 に」: 電源管理・PnP の通知・自動 mount・eject。機能の締切は 10/13。間に合わない所はその時にユーザーと削る）。
<!-- awesome-plan-current:end -->

## 目標（2026-10-03 ユーザー）

- 「ejectは挿入の通知もほしいですが、まだそういう機能がカーネルにないですよね。udev的なものです。」
- 「/dev/systemに電源管理とPnP通知、両方入れるのがいいと思います。何を通知してほしいかは、subscriberが指定すればいいと思います。」

- 2026-10-04 ユーザー（受け取り手）:「/dev/systemをopenしてreadするとき、あらかじめほしいイベント通知の種類を登録するのがいいかもしれないです。また、グラフィカルセッション（Waylandコンポジタ）が起動したとき、Keilandが/dev/systemのイベントを受け取ると思います。…コンソールセッションのとき、/sbin/initがそれを受け取るのがいいのかなあとも思いました。でも、このOSはクライアントOSだから、/sbin/initはその通知、いらないかなあとも思います。うん、いらないですね。Keilandが受け取ることにします。」→ 決定: (a) subscriber は open の後、read の前に受け取りたい事象の種類を登録する（登録しない種類は届かない）。(b) 受け取り手は Keiland（compositor、libkeiland-backend-zedbsd の中）。/sbin/init は受け取らない（console の session では電源ボタン等の事象を扱う者は居ない）。(c) ACPI の電源ボタン・蓋・AC の事象（WS049 p007 の kernel の handler、今は log だけ、`src/drivers/acpi/acpi-kern.c` の power_button）を `/dev/system` の事象として配る（Q1 が範囲に明記、2026-10-04 のユーザーの問い「電源ボタンイベントは/dev/systemのsubscriberに配信されるんですよね？」による）。

## 範囲（p001 で設計）

1. kernel: device の追加・削除（USB storage・network・display・input ほか）と電源の事象を、`/dev/system` を open した subscriber に `read()`・`poll()` で渡す。subscriber は受け取りたい事象の種類を指定する（filter）。形式（例: 1 行の key=value）、あふれた時の扱い、複数の読み手、権限（誰がどの事象を読めるか）。電源管理（WS052 の `/dev/system` での制御）と同じ node に載せる。
2. userland: 自動 mount の小さな daemon（devd に近い）が storage の事象を受けて mount の方針を決め、利用者の unmount の権限を扱う。desktop（libkeiland の抽象化、Files）へ通知する。
3. Files の eject（[ws127-p002](../ws127/phase002/phase.md) で止めた項目）と、挿入の通知での Locations の更新。
4. Linux・FreeBSD の Keiland では libkeiland の OS の module（udev・devd）で同じ抽象化を提供する。

## Phase（p001 の案、2026-10-05）

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [ws132-p001](phase001/phase.md) | 設計 | in-progress（2026-10-05 q707-i01 P1: 設計を書いた、D1〜D3 は人間の判断待ち） | — |
| [ws132-p002](phase002/phase.md) | 事象の核（`/dev/system` の購読・read・poll）、UAPI、送り手（ACPI の電源ボタン・蓋・AC・電池、disk、input、USB、network）、`KERN_SYSTEM_GET_POWER` | in-progress（2026-10-05 q707-i01 P1: 実装・build warning 0・host の試験 527＋137 checks。QEMU は T1 に依頼） | p001 |
| ws132-p003 | Keiland の backend の事象、compositor（input の探し直し、電池の表示、電源ボタン・蓋） | planning | p002、D1・D2 |
| ws132-p004 | volumed（自動 mount・抜去・eject の口） | planning | p002、D3 |
| ws132-p005 | libkeiland の volume の口、Files の eject・Locations | planning | p004 |
| ws132-p006 | QEMU の試験と全文の規約 | planning | p002〜p005 |
| ws132-p007 | 実機の UAT | planning | p006 |
