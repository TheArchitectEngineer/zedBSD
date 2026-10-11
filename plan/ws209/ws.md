<!-- awesome-plan project=zedbsd record=ws209 -->

# WS209: audiod の音の endpoint と route（通話・Bluetooth・将来の modem の音の経路）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-11 Q1 作成、P1）
Primary Milestone: MG006
Related Milestones: —
Parent: [Master](../master.md)
Queue: [P1 の lane](../agents/P1/queue.md)
Resume point: p001 再設計（ws197-p007 の設計 2 の R 節と review-2・review-3 の R の指摘から）。
<!-- awesome-plan-current:end -->

## 由来

2026-10-11 ユーザー:「今後モデムを実装するときも、音声はPCM取得ではなく、デバイスに直接出力なので、そういう出力先指定のモデルがあったほうがいいと思いました。」、Q12〜Q14「推しでOKです」→ Q12: R は新しい WS。Q13: 通話中の media は出力が speaker の時は止める、headset の時だけ −12 dB。Q14:「スマホで話す」の切替は Phone の通話の card だけ。

## 目標

audiod に音の endpoint（HDA の speaker・mic、Bluetooth の headset の A2DP・SCO、phone の SCO、将来の modem）と route（`media`・`call`）を持たせ、app は PCM に触らず route を選ぶだけにする。ws197-p007b1（HFP の音）・WS205 の A1・H3 の土台。

## 範囲（review-2 の指摘を含む、見積もり 15〜20 LW）

拍の model（master は HDA または timer、外の endpoint ごとに ASRC、初めの充填と XRUN の後の詰め直し）、遅れの予算の向きごとの表、権限（ROUTE_SET は root・`_bluetooth`・登録された compositor の uid）、endpoint の寿命と「一時停止中」、A2DP の Suspend との関係、route の要求で bluetoothd が SCO を張る流れ、audiod の protocol の版 2（message の最大）、3 OS の backend（Linux・FreeBSD は少なくとも ENOTSUP）と checker、Phone の card、host 試験、規約の全文の Phase。

## Phase

| Phase | 内容 | Status |
| --- | --- | --- |
| [p001](phase001/phase.md) | 再設計と review（第 1 版 2026-10-11 P1、review 中。判断 G1・G2） | in-progress |

## 2026-10-11 ユーザー「推し通りです。」

G1: 通話の時 headset が繋がっていれば最初から headset で話す。G2: A2DP の headset が繋がったら media を自動で headset へ移し、切れたら speaker に戻す。
