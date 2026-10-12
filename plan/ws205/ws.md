<!-- awesome-plan project=zedbsd record=ws205 -->

# WS205: Bluetooth audio（A2DP の SBC・AAC と、headset での HFP 通話）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-11 Q1 作成、P1 の予約の Queue。p001 の設計を P1 が書いた）
Primary Milestone: MG006
Related Milestones: —
Parent: [Master](../master.md)
Queue: [P1 の lane](../agents/P1/queue.md)
Resume point: p001（調査と設計）から。P1 が Phase を書き足す。
<!-- awesome-plan-current:end -->

## 由来

2026-10-11 ユーザー（このセッション）: 「Bluetoothオーディオの実装、libmediaにAACエンコードの実装、SBCエンコードの実装、SBCとAACでBlueoothオーディオを使えるようにする。目標はPCに接続されたBluetoothヘッドセットで、スマホ経由のHFP通話できること。」

## 目標

bluetoothd に A2DP の source（SBC と AAC の encode）と、headset（HFP の AG 側の音の中継）を足し、PC に接続した Bluetooth の headset で、スマホ経由の HFP 通話ができるようにする。libmedia に AAC と SBC の encoder を実装する。

## 完了の条件（仮、p001 で確定）

Bluetooth の headset で音楽が SBC と AAC で鳴る（実機）。スマホ経由の HFP 通話の音が PC に接続した headset で双方向に通る（実機、ユーザーの確認）。libmedia の SBC・AAC の encoder は host の試験で decode の一致を確かめる。

## Phase

| Phase | 内容 | Status |
| --- | --- | --- |
| [p001](phase001/phase.md) | 調査と設計（既存の code・hardware・規格、判断が要る点をユーザーへ） | in-progress（設計済み、D1〜D6 待ち） |
| [p002](phase002/phase.md) | libmedia の SBC の encoder・decoder（A2DP と mSBC）、host の試験 | in-progress（実装・host 試験済み） |
| [p003](phase003/phase.md) | libmedia の AAC-LC の encoder、host の試験 | in-progress（実装・host 試験済み） |
| p004 | A2DP の sink の endpoint と route `media`（audiod の route の model の上） | planned |
| p005 | bluetoothd の A2DP の source（SBC）、headset の接続 | planned |
| p006 | A2DP の AAC | planned |
| p007 | AVRCP の Absolute Volume | planned |
| p008 | HFP の AG と通話の制御の橋渡し | planned |
| p009 | headset の SCO の endpoint と route `call`（ws197-p007 の後） | planned |
| p010 | 実機（T1）と通話の UAT | planned |
| p011 | 規約の全文の見直し | planned |

## 2026-10-11 ユーザー「全部承認です。」（Q1 が示した推しを全て承認）

D2〜D6 は推しどおり: D2 audiod に Bluetooth の出力の口（全部の音を headset へ、app ごとの行き先は後）D3 AAC-LC だけ・TNS/PNS 無し・256 kbps まで・44.1/48 kHz D4 AVRCP は Absolute Volume だけ D5 headset が AAC なら AAC、無ければ SBC bitpool 53 D6 codec と A2DP を先、headset の通話は ws197-p007 の後。D1（試験の headset の機種と AAC・mSBC の対応）は推しの無い問いなので未回答 → headset が分かるまで AAC・mSBC は host の試験だけで確かめる。音の route の model（ws197 の 2026-10-11 の決定）に乗せる。
