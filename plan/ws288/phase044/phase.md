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
- 2026-10-26 11:45 P3 範囲 1（sim・host）: amaterrace p3-p044 c62fddb（親 c7af33f）: docs/ghci.md §7.2 に class 2 の gpu-op INIT 0x10109・DIAG_ECHO 0x1010A・DIAG_WRITE 0x1010B・DIAG_SUM 0x1010C（各 16 word の返事）、fw/cp/diag.c、FW_VERSION 0x00020001、fwtest.diag、image を作り直し（IMEM 66884 B）。zedBSD agent/p3-p043 38380a435（親 44e5d6931）: src/drivers/gpu/ghci/firmware.c（attach と reset の後に毎回 INIT、token の誤りか返事なしで attach を失敗にして log）、root だけの GPU_OP_GHCI_DIAG・GPU_OP_GHCI_FIRMWARE（uapi/gpu-op.h）、ghcictl diag [N]・ghcictl fwload [-l]（CPFW LOAD で host の memory から、-l は DBG の port で VRAM に置いた写しから、bitstream の作り直し不要）。class 2 を選んだ理由: firmware が decode するので RTL を変えずに reload で板に入る、1 回の INIT で ring の読み・引数の DMA の読み・返事の書き・fence を通る。diag の段: SCRATCH の register、DBG の port（host の word の読み書き・VRAM の word）、firmware の echo 14 word、firmware の host への pattern の書きを CPU が読む、CPU の pattern を firmware が足す（単純・重み付き）、firmware の VRAM の書きと和。VF2 では host の段を uncached の別名と cache された直の map の両方で（食い違いで PCIe の DMA が L2 に入るかが分かる）。互換: bitstream の firmware（0x00020000）は INIT を知らず FEATURE_NOT_PRESENT を返す → CPFW_VERSION < 0x00020001 の時だけ「INIT の前の firmware」として受け、fwload で新しい firmware を入れられるように attach を続ける（Q1 が承認: 古い firmware の返事も CP との往復、別の log の行にする）。試験: fw/cp/tests/run.sh 全 PASS、fw-run.sh PASS（fw.diag 211 word・0 differ）、atg_top_fw PASS、ghci-host-test.sh 134/225/166・0 失敗、build の warning 0（VF2 の vmunix e9cc9a7d…、riscv64 の ghcictl・ghci-blit、amd64）。次: 板の段 2。
