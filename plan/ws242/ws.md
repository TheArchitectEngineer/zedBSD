<!-- awesome-plan project=zedbsd record=ws242 -->

# WS242: nvrtx の RTX 20/30/40 の全モデルの対応

<!-- awesome-plan-current:start -->
Status: incomplete（すぐ取り組む物。2026-10-27 ユーザー「すぐ取り組むものの一つです。」）。旧: incomplete・保留（止まったまま）（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: p001 は 2026-10-15 に P2 の途中の WIP、p002 は host・build まで。実機（RTX 30/4…
Primary Milestone: MG006
Parent: [Master](../master.md)
Related: [WS037](../ws037/ws.md)（nvrtx の本体、RTX 2070 で desktop）、[WS240](../ws240/ws.md)（RTX 50、まだ開始しない）
Resume point（2026-10-29 P2、q1678-i01）: 設計の第 2 版（[design.md](design.md)、review [design-review.md](design-review.md) に答えた版）の Phase を p003〜p009 の番号に振り直して phase.md を作った（設計の表の p005a → p005、p005b → p006、p005c → p007、p006 → p008、p007 → p009）。実装は p003 から順に、試験なし（host 試験と build まで）。旧: p001 の再開点・p002 は下の表。
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-15）
「nvrtxドライバは、RTX 20/30/40/50までのSMで対応できるようにしたいです。」「20/30/40は、30/40の動作確認なしで、とりあえず対応しておいてください。firmwareも追加しておきます。あと、20/30/40の全モデルで認識できるようにしてください。50は別WSを作り、あとでnvrtxに追加します。」「この作業はいったん記録してから開始してください。50はまだ開始しないでください。」「20/30/40対応も独立したWSにして、今から作業します。」

## ユーザーの決定（2026-10-15）

- 「GTX 16・RTX 30・40 はデフォルトで有効にします。記録だけしてWS247を続けます。」→ 今は `nvrtx.gsp=1` の時だけ起動する GTX 16・RTX 30・40 を既定で有効にする。実装は WS242 の次の round（担当未定、WS247 の後）。

- 2026-10-29 ユーザーの回答（クリック、Q1 の中継、[design.md](design.md) §8）: Q242-1「既定で起動 (推し)」、Q242-2（Q250-2 とまとめた問い）「WS305 の VFIO (推し)」。
  → 解釈: GTX 16・RTX 30・40 は既定で GSP を起動し、`nvrtx.gsp=0` で逃げられることを文書に書く。RTX 2070 の回帰（p006）は WS305 の Linux＋VFIO の passthrough（10.0.10.3）で RTX を使える時に T1。
- 順（2026-10-29 Q1 の判断、Q250-3 の推し）: WS242 p003〜p005 → WS250 p002・p003 → WS291 の NVIDIA の段。

- 2026-10-29 聞き直し（設計の §3 の表の誤り（諦めの道がある、と書いた）と WS305 が止めたままの事に気づき、前提を正して聞き直した。上の Q242-1・Q242-2 の答えはこれで置き換わる）: ユーザーの回答（クリック）Q242-3「諦めの道を先に (推し)」、Q242-4「RDNA と差し替え (推し)」。
  → 解釈: 段ごとに諦めて firmware の画面に戻す道（design.md 第 2 版の改訂の p005b）を作ってから既定で起動（p005c）。RTX 2070 の回帰は RDNA と差し替えた素の起動（SwitchBot で電源、判定は ssh と boot-test.sh）。plan/tools/nvrtx/ に戻す試験の tools.md の登録は試験が入った時に Q1。

## 単一目標
nvrtx が RTX 20（Turing、GTX 16xx を含む）・30（Ampere）・40（Ada）の全モデルを認識し、30・40 でも GSP の起動・display・Vulkan の描画（SM86・SM89 の shader）が動く形にする。30/40 は実機の確かめなし（host 試験と build まで）。RTX 2070 の今の動きを壊さない。

## Phase
| ID | 内容 | 状態 | 依存 | 担当 |
| --- | --- | --- | --- | --- |
| ws242-p001 | driver: 全 chip の PCI id・chip id の認識、chip の世代の表、firmware の package（GA10x・AD10x）、起動・RM・class・QMD の差 | in-progress（残り＝再開点 1〜3 は p003〜p005 へ移した。uncleared で閉じる案は Q1） | — | P2 |
| ws242-p002 | compiler: SM86・SM89 の encode・latency・規則（SM75 の出力は不変） | in-progress（実装・host 試験・build まで） | — | P4 |
| ws242-p003 | [host 試験の復元と追従、identify・profile・製品・起動の判断](phase003/phase.md) | in-progress（実装済み・試験未実施） | — | P2（q1678-i01） |
| ws242-p004 | [SM を compiler に、QMD 3.0・class・PCAS2_B、HBLANK_DELAY](phase004/phase.md) | in-progress（実装済み・試験未実施。HBLANK_DELAY は入れない判断） | p003 | P2（q1678-i01） |
| ws242-p005 | [scrub 済みの末尾 256 MiB の検査、firmware の package の確かめ](phase005/phase.md) | in-progress（実装済み・試験未実施） | p003 | P2（q1678-i01） |
| ws242-p006 | [段ごとに諦めて firmware の画面に戻す道](phase006/phase.md) | planned | p004 | P2（q1678-i01） |
| ws242-p007 | [GTX 16・RTX 30・40 を既定で起動](phase007/phase.md) | planned | p004・p006 | P2（q1678-i01） |
| ws242-p008 | [RTX 2070 の実機の回帰](phase008/phase.md)（RDNA と差し替え、Q242-4） | planned | p004〜p007 | 未定（T1） |
| ws242-p009 | [規約の全文と出典の監査](phase009/phase.md) | planned | p003〜p008 | 未定 |
