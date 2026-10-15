<!-- awesome-plan project=zedbsd record=ws242 -->

# WS242: nvrtx の RTX 20/30/40 の全モデルの対応

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-15 Q1 作成、ユーザー「20/30/40対応も独立したWSにして、今から作業します。」）
Primary Milestone: MG006
Parent: [Master](../master.md)
Related: [WS037](../ws037/ws.md)（nvrtx の本体、RTX 2070 で desktop）、[WS240](../ws240/ws.md)（RTX 50、まだ開始しない）
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-15）
「nvrtxドライバは、RTX 20/30/40/50までのSMで対応できるようにしたいです。」「20/30/40は、30/40の動作確認なしで、とりあえず対応しておいてください。firmwareも追加しておきます。あと、20/30/40の全モデルで認識できるようにしてください。50は別WSを作り、あとでnvrtxに追加します。」「この作業はいったん記録してから開始してください。50はまだ開始しないでください。」「20/30/40対応も独立したWSにして、今から作業します。」

## ユーザーの決定（2026-10-15）

- 「GTX 16・RTX 30・40 はデフォルトで有効にします。記録だけしてWS247を続けます。」→ 今は `nvrtx.gsp=1` の時だけ起動する GTX 16・RTX 30・40 を既定で有効にする。実装は WS242 の次の round（担当未定、WS247 の後）。

## 単一目標
nvrtx が RTX 20（Turing、GTX 16xx を含む）・30（Ampere）・40（Ada）の全モデルを認識し、30・40 でも GSP の起動・display・Vulkan の描画（SM86・SM89 の shader）が動く形にする。30/40 は実機の確かめなし（host 試験と build まで）。RTX 2070 の今の動きを壊さない。

## Phase
| ID | 内容 | 担当 |
| --- | --- | --- |
| ws242-p001 | driver: 全 chip の PCI id・chip id の認識、chip の世代の表、firmware の package（GA10x・AD10x）、起動・RM・class・QMD の差 | P2 |
| ws242-p002 | compiler: SM86・SM89 の encode・latency・規則（SM75 の出力は不変） | P4 |
| ws242-p003 | 規約の全文 | 後 |
