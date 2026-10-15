<!-- awesome-plan project=zedbsd record=ws240 -->

# WS240: nvrtx に RTX 50（Blackwell、SM120、GB20x）を足す

<!-- awesome-plan-current:start -->
Status: planning（2026-10-16 Q1 作成、ユーザーの指示。「50はまだ開始しないでください。」）
Primary Milestone: MG006
Parent: [Master](../master.md)
Related: [WS037](../ws037/ws.md)（nvrtx、RTX 20/30/40）
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-16）
「nvrtxドライバは、RTX 20/30/40/50までのSMで対応できるようにしたいです。」「20/30/40は、30/40の動作確認なしで、とりあえず対応しておいてください。firmwareも追加しておきます。あと、20/30/40の全モデルで認識できるようにしてください。50は別WSを作り、あとでnvrtxに追加します。」

## 単一目標
WS037 の nvrtx（RTX 20/30/40）に、RTX 50（Blackwell、GB20x、SM120）を足す。

## 見込みの差（Q1 の知識、2026-10-16、確かめていない）
- GSP の起動: Blackwell は FSP を使う新しい信頼の連鎖で、Turing〜Ada の booter・FWSEC・WPR2 の道と変わる（P0〜P2 の作り直しに近い）。RM の API は新しい版（r570 以降）が前提の見込み。
- shader の compiler: SM120 は命令の encode・制御の bit・latency に Ada までより大きい差（Mesa NAK の Blackwell の対応が出典の候補、MIT）。
- 3D・compute・copy・display の class、compute の QMD の新しい版、MMU の page table の形式（変わるかは要確かめ）。
- 実機の RTX 50 が要る（入手はユーザー）。

## Phase
未定（WS037 の 20/30/40 の対応の後に、調べと設計の Phase から）。
