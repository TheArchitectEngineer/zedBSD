<!-- awesome-plan project=zedbsd record=ws162 -->
# WS162: FIDO2 の login

Status: planning（2026-10-05 追加、**ベータ2**（2026-10-05 ユーザー）、見積もり 2 LW。2026-10-05 夕のユーザーの決定で mock の第 2 版（p001 §9）、q771）
Master: [master](../master.md)
Primary Milestone: MG006

## 由来

ユーザー（2026-10-05）「FIDO2ログイン」

## 範囲（案、p001 の設計で確定する）

greeter と lock の画面で、登録した security key（WS161）で login・unlock する。登録は Settings の Users の頁。password との併用の規則は設計で決める。

## mock への変更（2026-10-05 夕のユーザー）

「sessiondに制御を入れず、libfido2をコンポジタのgreeterが直接叩くモックアップを作ってください。設定は~/.configの中でOKです。」と libpasskey の決定で、
sessiond の口と `/etc/keiland/fido-keys` をやめ、compositor が libpasskey で鍵と話し、保存は `~/.config/keiland/passkeys`。lock の画面は compositor だけで済む。
greeter は WS163 の G1 と同じ壁（`_greeter` は利用者の home を読めず、login は password の AUTH だけ）があり、hmac-secret で password を包む案を含む判断 K1 待ち（p001 §9.4）。

## Phase

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [ws162-p001](phase001/phase.md) | 要件と設計 | planning（第 1 版、第 2 版 §9 q771。K1〜K3 待ち） | WS161 |
| ws162-p002 | 保存（`~/.config/keiland/passkeys`）と lock の画面の「Use security key」、host 試験 | planned | WS161 p004、K2 |
| ws162-p003 | Settings の登録・削除、greeter（K1）、T1 | planned | p002、K1・K3 |
| ws162-p004 | 実機の UAT、全文規約 | planned | p003、WS161 p006 |
