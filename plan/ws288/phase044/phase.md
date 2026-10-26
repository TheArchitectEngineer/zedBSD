<!-- awesome-plan project=zedbsd record=ws288-p044 -->

# ws288-p044 CP の初期化・診断の command と、段を追った実機の確かめ

- 親: [WS288](../ws.md)。関係: [p043](../phase043/phase.md)（VF2 の M.2 の KC705、attach まで）、[p024](../phase024/phase.md)（KC705 の板、MIG）。
- 状態: in-progress（2026-10-26 11:10 Q1 新設）
- 由来（2026-10-26 11:10 ユーザー）: 「はい、ドライバの初期化時に、CPと通信していないのが、非常に設計としてまずいです。初期化コマンドを実装してください。・初期化コマンドと診断コマンドを実装 ・初期化と診断のコマンドが通るのを確認 ＝コマンドを置いたホストメモリへのアクセス経路と、CPファームウェアの正常動作を確認 ・HDMI接続の通知を受けて、scanoutを開始。 ＝AXIやDDR3アクセスの正常性を確認。DC回路の正常性を確認。 ・VRAMへのblitをscanout目視でチェック ・ホストメモリへのblitをCPUリードでチェック」
- 前の段の問題（Q1 の判断不足）: driver の attach（ghci_cp_start）は ring と fence の page を置いて CP の状態 RUNNING を待つだけで、CP と command の往復をしていなかった。p043 の確かめは blitter（host の memory）から始めてしまい、変数が多すぎた。

## 範囲

1. **初期化の command と診断の command**: GHCI の class 2（firmware の gpu-op）か µop に、driver が attach の時に必ず送る初期化の command（firmware の版・能力の返事、host の memory の道の確かめ）と、診断の command（例: 指定の値を host の memory の指定の address に AXI で書く・host の memory の pattern を読んで checksum を返す・VRAM の読み書き・echo）を足す。amaterrace の docs/ghci.md（仕様）・fw/cp（firmware）・sim の TB と、zedBSD の GHCI の driver（attach で初期化を送り、返事が来なければ attach を失敗にする）。
2. **初期化と診断が通る事を実機で**（VF2 + KC705）: command を置いた host の memory への道（ring の読み）と CP の firmware の正常な動作。CP が host の memory に書いた値を driver が cache の invalidate の後に読んで確かめる・host が書いた pattern を CP が読む。
3. **HDMI の接続の通知（HOTPLUG）を受けて scanout を始める**（内容は問わず、まず信号を出す）: AXI と DDR3 の読み、DC の回路の正常さ。ユーザーが monitor を見る。
4. **VRAM への blit を scanout の目視で確かめる。**
5. **host の memory への blit を CPU の読みで確かめる**（ghci-blit）。その後に Vulkan。

## 受け入れ

1〜5 が実機（VF2 の serial・SSH、HDMI の目視はユーザー）で通る。各段の証拠を evidence/ に。sim（make sim の関係する TB・ghci-e2e）が PASS のまま（T）。

## 記録

- 2026-10-26 11:10 Q1: 新設。担当は P3（新しい起動）。
