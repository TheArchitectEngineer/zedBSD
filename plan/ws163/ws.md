<!-- awesome-plan project=zedbsd record=ws163 -->
# WS163: 数字 6 桁の login

Status: planning（2026-10-05 追加、**ベータ2**（2026-10-05 ユーザー）、見積もり 1 LW。2026-10-05 ユーザーの決定で mock に改めた（p001 §9）、q769）
Master: [master](../master.md)
Primary Milestone: MG006

## 由来

ユーザー（2026-10-05）「数字6桁のログイン」

## 範囲（案、p001 の設計で確定する）

greeter と lock の画面で数字 6 桁の PIN で login・unlock する。PIN の保存（hash）、試行の制限、password との関係、sudo・SSH では使わないかを設計で決める。

## mock への変更（2026-10-05 ユーザー）

ユーザー「~/.configの中にPINを保存してOKです。sessiondに難しい制御をさせたくないです。移植ができなくなるからです。これはまずモックアップとしての実装で、あとで鍵管理やPAMのような仕組みをきちんと考えます。」
→ PIN の hash は利用者の `~/.config/keiland/pin`、確かめは compositor（lock の画面）、sessiond に口を足さない。p002〜p003 の中身を改めた（実行前）。
greeter の PIN の login は sessiond の口無しでは安全に作れないので、判断 G1（p001 §9.6）待ち。

## Phase

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [ws163-p001](phase001/phase.md) | 要件と設計 | planning（第 1 版 q733、mock の設計 §9 q769。G1 待ち） | — |
| [ws163-p002](phase002/phase.md) | PIN の保存（`~/.config/keiland/pin`、compositor の pin-store）と lock の画面の PIN の unlock、host 試験 | in-progress（実装・host 試験済み d9ab018d、T1 待ち） | p001 §9 |
| [ws163-p003](phase003/phase.md) | Settings の Users の PIN（`kl_system_account_v1` の set_pin、password は sessiond の今の UNLOCK で確かめる）、greeter（G1 の答えによる）、T1 | in-progress（Settings は実装・host 試験済み d9ab018d。greeter は G1 待ち） | p002、G1 |
| ws163-p004 | 全文規約の見直し | planned | p003 |
