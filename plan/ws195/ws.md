<!-- awesome-plan project=zedbsd record=ws195 -->

# WS195: zedBSD でも userland/desktop を /opt/keiland/ に入れる（account-admin を base から Keiland へ）

<!-- awesome-plan-current:start -->
Status: canceled（2026-10-13 ユーザー「WS195はキャンセルします。」。旧: planning、Target はベータ3）
Primary Milestone: MG006
Related Milestones: MG007
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
<!-- awesome-plan-current:end -->

## 目的（2026-10-09 ユーザー）

「userland/desktop/のインストール先を、zedBSDでもLinux/FreeBSDに合わせて、/opt/keiland/にします。account-adminはKeilandの必須バイナリとして、/opt/keiland以下に移します。baseから移動してください。」zedBSD の image で userland/desktop を /opt/keiland/ の下に入れ、account-admin を base から Keiland へ移す。

## 結果（2026-10-13 cancel）

- p001（P1、2026-10-09 夜）: 今の配置と参照の調べ、/opt/keiland の配置と移行の設計の案、判断 D1〜D7 と p002〜p006 の案を書いた。ユーザーの判断と design-reviewer の前に cancel。source は変えていない。
- zedBSD の配置と account-admin の置き場（`userland/base/account-admin/`）は今のまま。

## 制限・残り

- WS196（useradd・usermod・userdel）の p001 は、この WS の p001 の D7（account-admin の source の置き場）に依存していた。cancel で account-admin は base に残るので、WS196 の依存と E4 の判断の前提が変わる（WS196 の側で確かめる）。
- 再び /opt/keiland へ揃えるなら新しい WS の候補（勝手に移していない）。

## Phase

| Phase | 内容 | 最終の状態 |
| --- | --- | --- |
| ws195-p001 | 今の配置と参照の調べ、/opt/keiland の配置と移行の設計、account-admin の移動 | canceled（2026-10-13、ユーザーの指示で WS を cancel。旧: planning、D1〜D7 のユーザーの判断待ち） |

Phase の directory は完了の規則（AGENTS.md）で削除する（git の履歴に残る、削除は Q1）。
