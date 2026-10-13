<!-- awesome-plan project=zedbsd record=ws196 -->

# WS196: useradd・usermod・userdel（root の端末の利用者の管理）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG002
Related Milestones: —
Objectives: O1
Parent: [Master](../master.md)
Queue: q969（p002、P2）、q972（p003、P2）
Target: **ベータ2**（2026-10-13 ユーザー「WS196、WS172、WS009はベータ2に入れます。」。旧: ベータ3 以降（2026-10-09 Q1 の提案））
注（2026-10-13 Q1 の依頼の作業）: p001 の依存の WS195 は 2026-10-13 にユーザーの指示で cancel（account-admin は base に残る）。p001 の依存（WS195 p001 の D7）と E4 の前提を確かめ直す
Resume point: p002・p003 は実装済み、p004（T1）は T1-597 の後の再試験待ち（依頼の文は [phase004](phase004/phase.md) の「T1 への再試験の依頼」）。p003 の merge の時に Q1 が `config/ci/*.mk` に `useradd usermod userdel` を足す。T1 の後に p005（規約の全文）。2026-10-14 の 7 つの決定は [p001](phase001/phase.md) の末尾。
<!-- awesome-plan-current:end -->

## 由来（2026-10-09 ユーザー）

「account-admin コマンドは、useradd/mod/delと同等のものでしょうか？useradd/usermod/userdelの方がユーザは使いやすいと思うのですが」「useradd/usermod/userdelは別途、実装が必要な認識ですが」

## 目標

- root が端末で使う useradd・usermod・userdel を base に足す。POSIX には利用者の管理の utility は無い（passwd も POSIX に無い）ので、Linux の shadow-utils の option の慣習に合わせる（範囲は p001 で決める）。
- /etc/passwd・/etc/shadow・/etc/group の編集は account-admin だけが行い、useradd などは account-admin を呼ぶ root の前段にする（2026-10-14 ユーザー「useraddなどがaccount-adminを呼び出す構成はどうでしょうか。」）。
- account-admin の既存の欠陥（最後の管理者の数え方・uid の使い回し・lock の横取り・途中の失敗）もベータ2 で直す（2026-10-14 ユーザー）。
- account-admin の安全の規則（root・system の account の保護、最後の管理者）との関係を決める。

## Phase

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 範囲と構成の設計、review-1、ユーザーの決定 | cleared（2026-10-14） | — |
| [p002](phase002/phase.md) | account-admin の欠陥の直し（S1・S3・S4・S5・S6・M7・M2 の autologin）と root の前段の request の口（usermod の操作を含む） | in-progress — merge 済み（7f2c850a7）、T1（p004）待ち（q969） | p001 |
| [p003](phase003/phase.md) | useradd・usermod・userdel の前段（F1・S2・S8、§2 の option から `-d`・`-m` を除く）、man（`docs/reference/user-administration.md`）、host 試験 | in-progress — 実装と host 試験済み、test-wait（p004）（q972） | p002 |
| [p004](phase004/phase.md) | T1: guest で 3 つの命令・Settings の Users の頁・login・autologin、`plan/ws089/tests/admin-p026-guest.sh`（p002 と p003 の依頼を 1 つに） | uncleared — T1-597 で 3・4 PASS、2 の 3 つは試験の期待の誤り、1 は起動の直後の SSH の切断。試験を直した（842c58a82）、再試験の依頼は phase.md | p003 |
| [p005](phase005/phase.md) | 規約の全文の見直し（WS が変えた C を全文と照らし、書き方だけを直した） | 見直しと host 試験済み（2026-10-14 P2）、Q1 の判定待ち | p004 |
