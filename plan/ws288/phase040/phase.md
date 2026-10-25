<!-- awesome-plan project=zedbsd record=ws288-p040 -->

# ws288-p040 GHCI の memory の置き場所の設計どおりの実装（host の ring・fence、BAR2 は frame buffer だけ、窓を閉じる bit）

- 親: [WS288](../ws.md)。関係: [p039](../phase039/phase.md)（GPU → host の経路、HOST_LIMIT）、[p006](../phase006/phase.md)（driver）、[p013](../phase013/phase.md)。
- 状態: in-progress（2026-10-25 17:57 Q1、q1581 P2）
- 依存: p039（575dcf6、T1-801 で試験中。main に入る前は p1-p039 の上で作り、後で main に合わせる）。

## 決定（2026-10-25 17:57 ユーザー）

Q1 の提案（GHCI の MEM の capability に window の enable の bit を足し driver が自分の scanout に切り替えた後に閉じる、console と panic の画面は BAR2 に頼らず GPU の driver の scanout の経路（host の memory の buffer）を使う、driver の置き場所を GHCI §11.3 どおりに）の後、「では、この設計でGHCI仕様をアップデートして、zedBSDのドライバを仕様に合わせて変更し、RTLも必要に合わせて修正しましょう。」

先の決定: ring・fence は常に host memory、BAR2 は frame buffer 専用・32 bit か 64 bit（amaterrace main 8533ca6）。GOP の frame buffer を引き継がない場合は窓を閉じてよい。console は graphical session の後は使わない、使うなら /dev/gpu の scanout と Vulkan の display の上。

## 範囲

1. **GHCI**（amaterrace docs/ghci.md）: MEM の capability に window の enable の bit（host が書ける、reset で 1 = GOP などがそのまま使える、0 で BAR2 への host の access は捨て・読みは 0）。§11.1 に「窓は local memory の見え方で専用の memory ではない、driver が引き継いだ後は普通の local memory、閉じてもよい」。GOP などの起動の frame buffer の引き継ぎの規則（attach で DC の register から今の scanout を読める、最初の flip の TAKEN の後に範囲を返す）を §11 か §13 に短く。
2. **RTL**: MEM の capability の register（atg_cp_regs）と、KC705 の BAR2 の経路（atg_kc705_pcie と atg_kc705_mig の BAR2 の口）で enable を見て、0 なら書きを捨て読みは 0 を返す（AXI の応答は OKAY のまま）。TB。算術は am_ の module。
3. **zedBSD の driver**（src/drivers/gpu/ghci/）: memory.c の kind_for を GHCI §11.3 どおりに: device-local は local（窓の外を優先）、host-visible（staging・host が書く uniform・readback）と ring・command buffer・fence は host memory、host の buffer は local の外かつ HOST_LIMIT（MEM +0xC）の下、aperture は local と host の両方を覆う。scanout の buffer は local。BAR2 は driver が自分の scanout に切り替えた後（または窓を使わない時）に enable を 0 に。Vulkan の heap は discrete で 2 つ（device-local と host-visible・coherent）。vk/ と firmware の upload・readback が copy の command で通ることを確かめ、無ければ足す。host 試験（ghci-host-test.sh）と e2e（ghci-e2e.sh: Verilator の model は KC705 の形でないので、LOCAL と HOST の両方を持つ形にするか、どの形で確かめるかを設計の節で決める）。
4. 範囲の外: GOP の frame buffer の実際の引き継ぎ（KC705 に option ROM が無い）、panic の画面の経路（後の Phase）、起動の frame buffer の console の driver からの取り上げの GPU の core の仕組み（後）。

## 受け入れ

GHCI の文、RTL の TB（窓を閉じると BAR2 の書きが捨てられ読みが 0）、driver の host 試験と vmunix の warning 0、e2e が新しい置き場所（ring・fence が host）で PASS・oracle mismatch 0。make sim・Verilator は T。

## 記録

- 2026-10-25 17:57 Q1: 新設、P2 に（amaterrace の worktree p2-p040 は p1-p039 575dcf6 から、zedBSD の worktree は新しく）。
- 2026-10-25 P2 → Q1 設計（Q1 が受け入れ）:
  - GHCI: G1 MEM +0x10 WINDOW_CTRL（RW）[0] ENABLE、電源・function の reset で 1、CTRL.RESET では変えない。0 = 窓への host の書きは捨て・読みは 0・bus は普通に完了。MEM の version 1 のまま。G2 §11.1 窓は local memory の見え方、driver が自分の frame buffer を scanout したら普通の local、窓を使わない driver は閉じる。G3 §13 起動の frame buffer の引き継ぎ: attach で DC が ENABLE なら FB_BASE などが起動の frame buffer、driver の最初の flip が取られるまでその範囲と窓を保ち、その後に返して窓を閉じてよい。MEM の capability は 5 word で 0x1C0 に入らない → 0x1E0 へ（EXTCAP で見つける、PROT は 0x1D0 のまま）。
  - RTL: R1 atg_cp_regs の WINDOW_CTRL（board の rst_n）→ atg_cp → atg_top の win_en。R2 新 rtl/common/am_axi_gate.v（閉じると AW・W を受けて B OKAY、AR に len+1 beat の 0、方向ごとに outstanding が無い時だけ切り替え）、TB・spec。R3 KC705 の atg_kc705_pcie m2 と atg_kc705_mig s2 の間に gate（user_clk、win_en を am_sync）、atg_kc705_pcie_tb に group。
  - driver: D1 HOST_LIMIT を読み aperture を [最初の page, max(local の終わり, HOST_LIMIT))。D2 kind_for: DRIVER（ring・fence・bounce・vk の engine・staging・PROT の表）と MAPPABLE は host memory（HOST_LIMIT の下・local の外）、DEVICE は local、unified は host。local に CPU の view を持たない（CPU からの転送は host の bounce と BLT の copy）。D3 scanout の 2 frame は local、cursor の staging は host で CPU が変換し BLT で local へ。D4 窓を閉じる: DC が点いていなければ attach で、点いていれば最初の自分の flip が取られた後。D5 Vulkan の heap: discrete で heap 0 device-local・heap 1 host（HOST_VISIBLE|COHERENT）。
  - e2e: link に host memory の model [0, 0x8000_0000)（2 GiB の mmap NORESERVE）と local [0x8000_0000, +1 GiB)、応答に要求の AXI ID、ID と行き先の食い違いを stray に数える。e2e の kernel の kern_pmem_alloc_limited は host の model から。device->window は guard の配列（driver が窓を使えば分かる）。判定: case PASS・oracle mismatch 0・stray 0・guard 無傷・最後に WINDOW_CTRL が 0。
  - Q1: e2e と link の変更は P3 の D6（同じ file）が main に入った後に（P2 に伝えた）。
- 2026-10-25 18:20 P2 → Q1（申告）: amaterrace p2-p040 47fc3ce（親 312176e）。GHCI: §11.1（窓は local memory の見え方、使わなければ閉じる、WINDOW_CTRL）、MEM +0x10 WINDOW_CTRL [0] ENABLE（電源・FLR で 1、CTRL.RESET では変えない）、§11.3 の表の frame buffer の行、§13 の起動の frame buffer の引き継ぎ、Appendix A の手順 5。ghci_regs.h/.vh。RTL: atg_cp_regs の MEM を 0x1C0 → 0x1E0、WINDOW_CTRL の flop（board の rst_n）→ win_en → atg_top。新 rtl/common/am_axi_gate.v（開 = 素通し、閉 = 書きは捨てて OKAY・読みは 0 で OKAY、方向ごとに idle の時だけ切り替え、VALID を保つ）と spec と TB。KC705 の board top で pcie m2 と mig s2 の間に gate（user_clk、en = am_sync(win_en)、reset で 1）。PASS: atg_cp_regs（window の group）、am_axi_gate（seed 2 つ、mutation FAIL、最初の版の VALID の保持の誤りを TB が見つけた）、atg_kc705_pcie（window の group、en=1 の mutation で 5 FAIL）、kc705_raster、top、cp_prot、top_prot、lint 155 clean、arith-lint の新 code 0。make sim・Verilator は T。次は zedBSD の driver。
- 2026-10-25 18:32 P2 → Q1（申告）: zedBSD agent/p2-p040 6d25a84f7（親 b7966d020）。driver: MEM の HOST_LIMIT を読む、aperture は [最初の line, max(local の終わり, HOST_LIMIT))、DEVICE は local（窓の外を先に）、他（ring・fence・bounce・vk の heap・control・staging・PROT の表・MAPPABLE）は host（HOST_LIMIT の下・local の外）、local に CPU の view を持たない（CPU からの転送は host の bounce と BLT の copy）、窓は登録の後に閉じ detach で開く（driver は attach で DC を消すので起動の frame buffer は残らない、本当の引き継ぎは範囲の外）、display の frame は local・cursor は host、Vulkan の discrete は heap 0 = local DEVICE_LOCAL・heap 1 = host（HOST_VISIBLE|COHERENT）。kind の名前 VISIBLE/HIDDEN → WINDOWED/UNWINDOWED。変えた file: hcr.h・ghci.h・memory.c・ghci.c・display.c（1 行）・vk/vk-device.c、host 試験 2 つ。ghci-host-test 134/0・driver 180/0・vk 166/0 PASS（model に HOST_LIMIT・WINDOW_CTRL・窓の guard）、vmunix warning 0。**ghci-e2e.sh はこの commit では link の変更までは落ちる**（e2e の kernel の stub に host memory が無い）。link と e2e は P3 の p035 が main に入った後。
- 2026-10-25 19:12 P2 → Q1（申告）: amaterrace p2-p040 d0d3d92（親 47fc3ce・ea37240 = p039 と main の統合）。p040 の部分だけを解いた: atg_cp_regs（MEM 0x1E0 と WINDOW_CTRL、main の DISP2 の xcap 11 = 0x200）、atg_cp.v、board top（1 vector の MSI と gate）、pcie_tb（MEM_WINDOW_CTRL 0x1F0）、GHCI §13 は main で 13.1 DISP・13.2 DISP2 に分かれたので引き継ぎの節は §13.3。lint clean、atg_cp_regs・am_axi_gate・cp_disp・top・top_prot・top_fw・kc705_pcie（window を含む）・kc705_raster・dc PASS。link（host memory [0, 0x8000_0000) を ID 1、応答に要求の ID、stray = 範囲外か ID の誤り、atg_link_host・atg_link_window_open、link-build に HOST_LIMIT）と e2e の kernel（host の pmem、窓は guard、窓の閉じ・開け直し・guard 無傷・host_beats > 0・stray 0 を確かめる）を作業中、e2e を実行中（25〜40 分）。
- 2026-10-25 19:21 P2 → Q1（申告）: 実装済み。amaterrace p2-p040 8a0571c（312176e → 47fc3ce → d0d3d92 → 8a0571c の link）、zedBSD agent/p2-p040 db57828e9（6d25a84f7 driver → 0a6f26c7f main の merge → db57828e9 e2e）。link: host memory [0, 0x8000_0000)、応答に要求の AXI ID、local は ID 0・host は ID 1 だけ（他は stray）、local_beats/host_beats、atg_link_host・atg_link_window_open、link-build に HOST_LIMIT。e2e の kernel: host の pmem、窓は guard、終わりに窓の閉じ・開け直し・guard 無傷・host_beats > 0・stray 0 を確かめる。display の case: shared の present は local の blob（GPU_RESOURCE_WRITE で書く）に（§11.3 で mappable の blob は host memory、present_shared は host を断る）。P2 の確認: e2e 9 PASS・mismatch 0・stray 0（8 の measure 全部）・placement failures 0・guard 0・host beats 19,476・local beats 84,799、display の copy の FB_BASE 0x8400_0000（窓の外の local）・shared 0x8409_6000。ghci-host-test 134/180/166、vmunix warning 0。T1-803。
