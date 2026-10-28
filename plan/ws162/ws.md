<!-- awesome-plan project=zedbsd record=ws162 -->

# WS162: FIDO2 の login

<!-- awesome-plan-current:start -->
Status: completed（WS172・WS199 で達成）（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」。根拠: 2026-10-05 夜 ユーザーの決定で WS172 に吸収、FIDO2 の login は 2026-10-10 ユーザー「FIDO2のキー登録とログインができました」（BUG-279 resolved、WS199））
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

greeter と lock の画面で、登録した security key（WS161）で login・unlock する。

## ユーザーの決定

- 2026-10-05「FIDO2ログイン」（ベータ2）
- 2026-10-05 夕「sessiondに制御を入れず、libfido2をコンポジタのgreeterが直接叩くモックアップを作ってください。設定は~/.configの中でOKです。」
- 2026-10-05 夜（Q1 経由）: WS172 に吸収。FIDO2 の login は WS172（`/sbin/passkey` と `/usr/libexec/passkey-fido2`、sessiond の外部の認証）の p003 が持つ。上の mock は作らない。
- 2026-10-10「FIDO2のキー登録とログインができました」（BUG-279 の解決）

## 結果

この WS では code を作っていない。FIDO2 の login は [WS172](../ws172/ws.md) p003 と [WS199](../ws199/ws.md) で main に入り、2026-10-10 にユーザーが実機で登録と login を確かめた。

## 制限・移管

- p001 の設計（第 2 版 §9、判断 K1〜K3）は WS172 p001 の設計（`/etc/passkey`、root の検証、機器の子の sandbox）に置き換わった。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws162-p001 | 要件と設計 | planning のまま取り下げ（WS172 p001 の設計に置き換え） |
| ws162-p002 | 保存と lock の画面の「Use security key」 | 取り下げ → WS172 p003 |
| ws162-p003 | Settings の登録・削除、greeter | 取り下げ → WS172 p003 |
| ws162-p004 | 実機の UAT、規約の全文 | 取り下げ → WS172 p003（UAT）・p006（規約） |

## 記録の所在

Phase の記録は削除する（Q1）。git の commit `fb1f6afee` の `plan/ws162/` にある。Queue の履歴は [plan/history](../history/index.md)。
