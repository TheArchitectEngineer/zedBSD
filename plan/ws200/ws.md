<!-- awesome-plan project=zedbsd record=ws200 -->

# WS200: Settings の Users の頁のパスワード変更のウィザードと認証方式の選択

<!-- awesome-plan-current:start -->
Status: completed（2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」。全 Phase が cleared。旧: incomplete、p002 は 5330 の UAT 待ち）
Primary Milestone: MG006
Related Milestones: —
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
<!-- awesome-plan-current:end -->

## 目的

ユーザー（2026-10-10 の UAT）「SettingsのUsersのページには、パスワード変更ボタンをつけて、ウィザード式にする。」「SettingsのUsersのページには、認証方式のボタンをつけて、Password, PIN, Security Keyにチェックをつけて変更できる。それ以外のログイン方法は、greeter/lock画面では使えなくなる。コンソールのログインではパスワードだけ使える。」

## 結果（2026-10-13 完了）

- Users の頁の「Change Password」のウィザード（今の password → 新しい password を 2 回 → 完了）と「Sign-in Methods」（Password・PIN・Security Key の checkbox、1 つは残す、登録の無い方式は選べない）。WS199 の popup の部品（`userland/desktop/settings/dialog.c`）と /etc/passkey の options の行の `methods` を使い、保存は特権の側（sessiond・/sbin/passkey）。
- 確認: host 試験、QEMU の T1-525（Change Password・Sign-in Methods の全手順）・T1-528（手順 8）。5330 の UAT（p002）は 2026-10-13 ユーザーの指示（上の原文）で確認済みの扱い。

## 制限・移管

- console の login・su・sudo・SSH は今どおり password だけ（docs/architecture/security.md の規則）。
- 規約の全文の見直しの Phase は無い（規約の整形はベータ3、2026-10-08 ユーザー）。

## Phase

| Phase | 内容 | 最終の状態 |
| --- | --- | --- |
| ws200-p001 | Change Password のウィザードと Sign-in Methods、host 試験 | cleared（2026-10-10 Q1、T1-525・T1-528） |
| ws200-p002 | 5330 の UAT | cleared（2026-10-13 ユーザー、上の原文） |

Phase の directory とこの WS だけの試験は、完了の規則（AGENTS.md）で削除する（git の履歴に残る、削除は Q1）。
