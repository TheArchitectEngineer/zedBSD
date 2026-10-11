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

拍の model（master は HDA または timer、外の endpoint ごとに ASRC、初めの充填と XRUN の後の詰め直し）、遅れの予算の向きごとの表、権限（ROUTE_SET は root・`_bluetooth`・seat の持ち主（/dev/gpu0 の owner）、枠ごとの class を audiod が強制、「Phone・Settings だけ」は約束（設計 1.1 の R6''））、endpoint の寿命と「一時停止中」、A2DP の Suspend との関係、route の要求で bluetoothd が SCO を張る流れ、audiod の protocol の版 2（message の最大）、3 OS の backend（Linux・FreeBSD は少なくとも ENOTSUP）と checker、Phone の card、host 試験、規約の全文の Phase。

## Phase

| Phase | 内容 | Status |
| --- | --- | --- |
| [p001](phase001/phase.md) | 再設計と review（第 1 版 → review-1 → 設計 1.1（P1 1acb8d9cc、未統合）、E・R・P の再 review 中。判断 G1・G2 は推しどおり、G3・G4 は確認中） | in-progress |
| [p002](phase002/phase.md) | audiod の endpoint・route・権限・protocol 版 2 | in-progress（host 試験・T1-546 PASS、main に統合 2026-10-12） |
| [p003](phase003/phase.md) | ASRC と拍・外の endpoint・call の脚 | in-progress（host 試験・T1-546 PASS、main に統合、speaker の call と mic は QEMU・5330 で未確認） |
| p004 | desktop の口（libkeiland・compositor・3 OS の backend・Settings・Phone、M10 を先に、WS191 の後） | planned（5〜6 LW） |
| p005 | QEMU の確かめ | planned（1 LW） |
| p006 | 規約の全文 | planned（1 LW） |

## 2026-10-11 ユーザー「推し通りです。」

G1: 通話の時 headset が繋がっていれば最初から headset で話す。G2: A2DP の headset が繋がったら media を自動で headset へ移し、切れたら speaker に戻す。

## 2026-10-11 ユーザーの決定: G3・G4

ユーザー「許可します。」「Q1の推しにします」→ G3: 通話の音がスマホにある間は PC の media を止めない（Q1 と P1 の推しを「止めない」に揃えた後の答え）。G4: call の間だけ buffer を詰めて（SCO の水位 20〜25 ms、通話の間の audiod の先行 1 fragment）片道 150 ms を目指し、届かなければ約 170 ms を受け入れる。

## 2026-10-11 ユーザーの決定: G5

ユーザー「G5は推しにします。」→ 利用者の操作によらず media の出力（headset など）が消えた時は再生を止める（`MEDIA_HELD reason=OUTPUT_LOST` で待たせ、compositor が ROUTE_SET を出すまで鳴らさない。speaker へ自動で戻して鳴らさない）。review-2 の N7。
