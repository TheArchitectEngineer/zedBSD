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
- 2026-10-26 12:50 P3（q1592-i01、worktree `~/zedBSD-worktrees/p3-p043` agent/p3-p043、amaterrace `~/amaterrace-worktrees/p3-p044` p3-p044）:
  - **範囲 1（sim・host）**: amaterrace c62fddb（親 c7af33f）: docs/ghci.md §7.2 に class 2 の INIT 0x10109・DIAG_ECHO 0x1010A・DIAG_WRITE 0x1010B・DIAG_SUM 0x1010C（16 word の返事）、fw/cp/diag.c、FW_VERSION 0x00020001、fwtest.diag、image 作り直し（IMEM 66884 B）。class 2 にした理由: firmware が decode するので RTL を変えずに CPFW の reload で板に入り、1 回の INIT が ring の読み・引数の DMA・返事の書き・fence を全部通る。zedBSD 38380a435・be750c43d・2d73b0521: src/drivers/gpu/ghci/firmware.c（attach と reset・reload の後に INIT。返事なし・token 違いは attach の失敗。古い firmware の FEATURE_NOT_PRESENT は CPFW_VERSION < 0x00020001 の時だけ別の行で受ける＝Q1 承認）、root だけの GPU_OP_GHCI_DIAG（diag・paint）と GPU_OP_GHCI_FIRMWARE（uapi/gpu-op.h）、`ghcictl diag [N]`・`ghcictl paint`・`ghcictl fwload [-l] imem dmem`。
  - 試験: fw/cp/tests/run.sh 全 PASS、sim/verilator/fw-run.sh PASS（fw.diag 211 word・0 differ）、sim/run.sh atg_top_fw PASS、ghci-host-test.sh 134/237/169・0 失敗（test_firmware・test_idle_frame・vk の stop 契約を追加）、ghci-e2e.sh（AMATERRACE_GPU=p3-p044）9 行 PASS（INIT が RTL の firmware で通る、idle frame の scanout も）。build: VF2 vmunix 478f6e24…・amd64 vmunix（check PASS）・riscv64 ghcictl・ghci-blit、warning 0。
  - **範囲 2（実機、serial・SSH）**: evidence/boot1-uncached-serial-ghci.txt・boot2-cached-fwstart-serial-ghci.txt・step2-kernel-ghci.txt。
    - CONFIG_GHCI_HOST_UNCACHED=y の kernel: 最初の INIT が timeout。FENCE_LAST 1・fence page（uncached の別名で読む）0・RING_RPTR=WPTR=9（CP は ring を読めている）→ **VF2 の PCIe の DMA は L2 を通る（coherent）ので、uncached の別名は GPU の書きを見ず、GPU は別名で書いた値の代わりに L2 の古い行を読む**。p043 の「fill が 0」・RING の fault・no Vulkan executor (42) の原因と見る。cached の直の map（HOST_UNCACHED=n、VF2_DMA_DEFAULT_COHERENT の既定）で INIT が通り vulkan 1 に。試験の config を n に変えた。
    - 古い firmware（0x00020000）: 「predates INIT; answered FEATURE_NOT_PRESENT」で attach。`ghcictl fwload`（host memory から CPFW LOAD、IMEM 16721・DMEM 912 word）で firmware 2.1 が INIT に正しく答えた（protocol 1.0、features 7）。bitstream の作り直しなし。KC705 の BRAM に入ったまま（再構成まで）。
    - `ghcictl diag 1000`（23 秒）: register・dbg-host-read・dbg-host-write・dbg-host-write-other・dbg-local（VRAM）・fw-echo・fw-host-write（firmware の 16 KiB を CPU が読む）・fw-host-write-other（別名）・fw-host-sum（CPU の 16 KiB を firmware が足す）・fw-local（VRAM 16 KiB 書き＋和）が各 1000/1000。fw-host-sum-other（CPU が別名で書く）だけ 0/1000 ＝上の L2 の観察（失敗に数えない形にした）。100 回でも同じ。
    - 板で見つけた別の不具合 2 つを直した: (a) vk の node に stop_begin/stop_poll が無く、GPU の core が session の close の度に device 全体の fault にして、2 回目以降の open が ENODEV → vk-node.c に stop の契約。(b) VF2 の電源断の後の起動で時々「firmware did not start (CPFW_STATUS 9)」→ ghci_firmware_start が 1 回だけ reset と RUN をやり直す（log 付き。その後の起動では再発せず、やり直しの効き目は未確認）。
  - 範囲 3 の準備: display に driver の idle frame（VRAM、最大の mode）、接続があり lease が無い時に 1280x720 で 1 接続に 1 回 scanout（lease が取った接続では再点灯しない）。範囲 4 の `ghcictl paint`: blitter で上 2/3 に 8 本の色帯（白・黄・シアン・緑・マゼンタ・赤・青・黒）、下 1/3 に 16 段の灰、firmware の DIAG_SUM で読み戻して比べる。HDMI に出す前に Q1 に聞いた（返事待ち）。新しい kernel は板の /boot/boot0/vmunix に置いたが、まだ起動していない。
