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
- 2026-10-25 18:05 P2 → Q1（設計の注、Q1 が 18:10 頃に了承）: G1 MEM +0x10 WINDOW_CTRL（[0] ENABLE、power-on/FLR で 1、CTRL.RESET は変えない、0 で BAR2 の書きは捨て・読みは 0・bus は普通に完了）、G2 §11.1、G3 §13 の起動の frame buffer の引き継ぎの規則。MEM の 5 word は 0x1C0 に入らない（0x1D0 は PROT）ので Amaterrace の MEM を 0x1E0 へ（EXTCAP で見つける）。R1 atg_cp_regs の flop（board の rst_n）→ win_en → atg_top。R2 汎用 rtl/common/am_axi_gate。R3 KC705 の board top で pcie m2 と mig s2 の間（user_clk、am_sync）。D1〜D5 driver。e2e の model（link に host memory [0, HOST_LIMIT)、応答に要求の ID、window は guard の配列）は P3 の D6 が main に入った後（Q1 の指示）。

## 結果の途中（2026-10-25 18:31 P2）

### amaterrace-gpu p2-p040 47fc3ce（親 312176e = 575dcf6 + main 86ca58a の merge、衝突なし）

- GHCI（docs/ghci.md）: §11.1 に「窓は local memory の見え方、driver が自分の scanout にした後は普通の local memory、reset の後は開いている（GOP・起動の console 用）、使わない driver は閉じる、閉じている間は host の書きは捨て・読みは 0、bus は普通に完了」。§11.3 の MEM の表に +0x10 WINDOW_CTRL、表の Framebuffer の行。§13 に「起動の frame buffer の引き継ぎ」（attach で CTRL.ENABLE なら FB_BASE・FB_STRIDE・FB_SIZE・FORMAT から読み、その範囲を割り当てず窓を開けたまま、自分の最初の flip が取られたら範囲を返し窓を閉じる。ENABLE が 0 なら attach で閉じる）。Appendix A の 5 に 1 行。include/ghci_regs.h・.vh に GHCI_MEM_WINDOW_CTRL・ENABLE。
- RTL: atg_cp_regs の MEM を 0x1E0〜0x1F0 に、WINDOW_CTRL の flop（board の rst_n、窓の有る時だけ書ける、無ければ 0）と出力 win_en、atg_cp・atg_top に port win_en。新 rtl/common/am_axi_gate.v（open は素通し、closed は書きを OKAY で捨て読みは 0 の OKAY、方向ごとの mode は何も outstanding でない時だけ en に従う、VALID は READY まで保つ、W が AW より先の時の lead の counter、counter は am_add）と spec/common/am_axi_gate.md。KC705 の atg_kc705_raster.v: pcie の m2 と mig の s2 の間に am_axi_gate（user_clk、en = am_sync(win_en)、reset 値 1）。spec: atg_cp_regs・atg_cp・atg_top・atg_kc705_raster。
- 確認（P2、自分で実行）: sim/run.sh atg_cp_regs PASS（新しい .window: reset の後 1、0 を書くと 0 と win_en 0、byte 0 の無い書きは変えない、1 で戻る、reset で開く、MEM の無い bench は 0）、am_axi_gate PASS（seed 1・5、2 つの形、各 1500 の書きと読み、en を 20〜400 cycle ごとに切り替え）、mutation MUT_GATE_NOWAIT は FAIL（slave の write data の ERROR）。最初の版は en が落ちた時に m_awvalid を READY の前に下げる bug があり、TB（slave の「AW changed before ready」）が見つけて直した。atg_kc705_pcie PASS（新しい .window: 閉じると BAR2 の書きは OKAY で DDR3 は変わらず、読みは 0 で OKAY、burst も、開け直すと元の値）、board top の en を 1 に固定する mutation は 5 つの FAIL。atg_kc705_raster PASS（73 s）、atg_top PASS、atg_cp_prot PASS、atg_top_prot PASS。tools/lint.sh rc 0（155 clean）。tools/arith-lint.py: am_axi_gate 0、atg_cp_regs 2（前からの counter と perf の加算、p037 の CP の分）。
- 未実施（T へ）: make sim・sim-quick・Verilator（fw-run・e2e）。axi_id_slave は W を AW より先に取らないので、open の W 先行の経路（lead）は「出す」までで「取られる」は試していない。

### zedBSD agent/p2-p040（driver、未 commit → この記録と同じ commit）

- src/drivers/gpu/ghci/: hcr.h に GHCI_MEM_HOST_LIMIT・WINDOW_CTRL・ENABLE。ghci.h: memory の説明を §11.3 どおりに、kind を GHCI_MEMORY_WINDOWED（窓の範囲の local）・UNWINDOWED（その先の local）・HOST に、field host_limit・mem_registers、allocator を windowed・unwindowed に、ghci_memory_window_set。memory.c: aperture = [最初の line、max(local の終わり, HOST_LIMIT)) を 2^32 − line で切る、kind_for = DEVICE は discrete なら local（窓の先が先、満ちたら窓の範囲）、他（ring・fence・bounce・vk の heap/control/staging・PROT の table・MAPPABLE）は host、local には CPU の view を作らない、host は local の外かつ HOST_LIMIT の下（kern_pmem_alloc_limited の上限と host_usable）、ghci_memory_window_set（WINDOW_CTRL）。ghci.c: MEM の HOST_LIMIT と registers を読む、map の DEVICE の属性を外す（local は map しない）、登録が済んだら窓を閉じ、detach の最後に開け直す（driver は attach で DC を暗くするので起動の frame buffer は見えなくなる: 引き継ぎそのものは範囲の外）。display.c: 2 枚の frame を GHCI_USE_DEVICE（local）に、cursor の surface と staging は host のまま（CPU が変換する）。vk/vk-device.c: discrete は heap 2 つ（0 = local の全部 DEVICE_LOCAL、1 = host、flags 0）、type 0 DEVICE_LOCAL → heap 0、type 1 HOST_VISIBLE|HOST_COHERENT → heap 1。unified は前のまま（1 heap、type 1 も DEVICE_LOCAL）。
- upload・readback: CPU の read/write（gpu_resource_read/write）は local では host の bounce と BLT の COPY（blit.c、前から有る）、Vulkan は vkCmdCopy* の firmware の copy（GPU address だけを見る、fw/cp に local の address の決め打ちは無いことを grep で確かめた）。
- host 試験: plan/ws288/tests/ghci-host-test.sh → ghci-host-test 134 passed 0 failed、ghci-driver-host-test 180 passed 0 failed、ghci-vk-host-test 166 passed 0 failed、PASS（自分で数えた）。model に HOST_LIMIT 0x8000_0000 と WINDOW_CTRL、host RAM を 32 MiB（vk は 64 MiB）に、driver の window の mapping を model の local memory でない guard の配列にした（driver が窓を触ったら分かる）。新しい check: aperture、窓を閉じる・開け直す、ring・fence・bounce が host で HOST_LIMIT の下、storage が host で RAM として map、blob が窓の先の local、display の frame が local で中身は copy、guard が不変、vk の type と heap。
- build: make -j16 ZEDBSD_CONFIG=plan/ws288/tests/config-amd64-ghci.mk BUILD=build/p040 build/p040/vmunix rc 0、warning 0、kernel include check PASS、amd64 vmunix check PASS。
- **注意**: e2e（ghci-e2e.sh）は今の link に host memory が無い（kern_pmem_alloc_limited が ENOMEM）ので、この driver では ring が取れず start で止まる。e2e の link と kernel の変更は P3 の D6 の後（Q1 の指示）。それまで e2e は FAIL の見込み（未実行）。

