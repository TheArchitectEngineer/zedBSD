<!-- awesome-plan project=zedbsd record=ws288-p013 -->

# ws288-p013 KC705 の PCIe（GPU の board として host に出す）

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §8 の p013 の行・§9.2（host link A'）。
- 状態: in-progress（2026-10-25 15:49 Q1、q1579 P2: 板の要らない部分 = bridge の RTL と sim を先に。板の部分は p024 と一緒）
- 依存: p012、p024（板の bring-up）、p006（driver の PCIe の attach は p006 の範囲）。
- 承認: 2026-10-25 ユーザー「了解です。SAはfuture workに回します。p009, msaaを優先して実装してください。そのあと、p010,p018,p019,p006,p011を実装します。KC705のDDR3をVRAMにしてPCIeのGPUボードにすることで、QEMUからパススルーで利用し、ゲストのzedBSDからVulkanを動作させることを、最初のゴールにします。必要なphaseは作成してください。」

## 範囲

- KC705 の PCIe（Gen2 x4 か x8、XDMA または 7 series の PCIe の hard block）: BAR0 = GHCI の register・doorbell、BAR2 = **DDR3 を VRAM** として見せる窓（GHCI §11 の discrete の形、MEM の capability）、MSI（または MSI-X）、GPU の DMA は DDR3 の上（host memory への DMA は後、下の Future Work）。
- bitstream の載せ方と link の保ち方（JTAG で載せた後の PCIe の再列挙: `pcie-reload.sh` の rescan、または flash の bitstream）。host の PC の電源の扱い（SwitchBot）を含む。
- host の Linux で VFIO に bind できること（IOMMU group の確認）。

## 先に行う部分（2026-10-25 15:49 ユーザー「PCIeブリッジを今から作ってください。」）

板の無い範囲を先に行う（板・bitstream・VFIO の bind は p024 の後）:

1. 設計の短い節（phase.md に）: 使う IP（7 series の XDMA の AXI-Lite master = BAR0、AXI の bypass か AXI master = BAR2、MSI の割り込みの口。Vivado 2026.1 の xc7k325t で使える物を確かめる）、clock の領域（PCIe の user clock 125/250 MHz と GPU の gclk の境は am_afifo・am_sync）、BAR の大きさ（BAR0 = GHCI の HCR の窓と DISP2 の窓、BAR2 = DDR3 の窓の大きさと base の register）、MSI のどの IRQ の bit をどう送るか、reset の順。
2. RTL（`rtl/` か `boards/kc705/`、vendor の primitive・IP は `boards/` と `rtl/common/xilinx/` だけ）: BAR0 の AXI-Lite → GHCI の register の口（今の JTAG の bridge と同じ口へ、両方を残す）、BAR2 の AXI → MC の client（DDR3 の窓、MEM の capability の範囲の検査）、GHCI の IRQ → MSI、clock の境。算術は p037 の規則（rtl/common の am_ の module）。
3. sim: XDMA の口の振る舞いの model（`sim/models/`）で、BAR0 で GHCI の ID を読む・doorbell、BAR2 で DDR3 の model への読み書き、IRQ → MSI が届く、を TB で。lint clean。
4. board top（atg_kc705_raster）に PCIe の口を足す（IP の instance は wrapper の stub で lint が通る形、IP の生成の Tcl を tools/ か boards/kc705/ に）。

## 設計（p013 の板の要らない部分、2026-10-25 16:3x P2、Q1 が転記）

- IP（Vivado 2026.1 の data/ip で確かめた事）: xdma_v4_2・axi_pcie_v2_9（AXI Memory Mapped To PCI Express）・pcie_7x_v3_3 は kintex7 に対応。PG195・PG055 は host に無く未読（port の名前は component.xml から）。XDMA の 7 series の AXI Bridge mode の有無と CONFIG の名前は create_ip で確かめる（未確認）。
- **判断 P1**: XDMA の DMA mode（BAR0 = AXI-Lite master、BAR2 = DMA bypass）か、AXI の bridge（XDMA の Bridge mode か axi_pcie）か。P2 の推し: bridge（(1) GPU → host memory の AXI slave がそのまま MC の root の AXI の先に付く（p029・両方の形に要る）、(2) XDMA の user IRQ は DMA の BAR の IRQ block の設定が要り GHCI の汎用の driver から外れる（記憶、未確認）、axi_pcie は標準の MSI だけ、(3) axi_pcie の BAR は 32/64 bit の一括で BAR0 32 bit・BAR2 64 bit prefetchable が取れない可能性）。RTL は IP に依らない wrapper の口で書くので、判断は wrapper の中と IP の Tcl だけに効く。
- BAR2 の経路（Q1 の「MC の client」から変える、P2 の推し）: MC を通さず、MIG の前の AXI interconnect（atg_kc705_mig の中、GPU と 2 port）で DDR3 へ。MC の client だと aperture（reset で空）・quiesce・RESET の flush の対象になり、host の BAR2 の access が error・停止・completion timeout になり得るため。範囲は BAR2 の大きさ = WINDOW_BYTES（64 MiB）で構成上満たす。
- clock: refclk 100 MHz（IBUFDS_GTE2）、user clock は Gen2 x4・128 bit・125 MHz。BAR0 は am_axi_cdc（新、am_afifo の 5 channel）で gclk の atg_top の s_*（17 bit = GHCI の窓 + DC の +0x10000）へ。JTAG の bridge は残す。BAR2 は user clock のまま interconnect へ。
- MSI（atg_pcie_msi、新、GHCI §15.2）: vector 0 = GHCI の irq、1 = DC の irq。立ち上がり、または IRQ_STATUS/ENABLE への write の後に線が 1 のままなら再送。gclk の事象を toggle と am_sync で user clock へ、req/ack。
- reset: PERST# → IP → axi_aresetn。CDC の両側の reset の OR を各 domain へ。GPU の reset 中の BAR0 の access は FIFO で待つ（completion timeout の恐れ、運用で避ける）。FLR は v1 に無し。
- wrapper の口（boards/kc705/lib/atg_kc705_pcie.v）: PCIe の pin、user_clk・user_rst_n・link_up、BAR0 の AXI-Lite master、BAR2 の AXI4 master、msi_req[1:0]/msi_ack、（bridge なら）GPU → host の AXI4 slave は v1 では DECERR の端。sim は sim/models/kc705/atg_kc705_pcie.v（host の task）に差し替え。
- class code 0x038000、vendor 0x10EE、device id は IP の既定を記録。
- 範囲の外: GPU → host memory の DMA の実働、XDMA の H2C/C2H、bitstream と再列挙、VFIO（p024 の後）。

## 受け入れ

host の Linux で KC705 が PCIe の device として見え、BAR0 の GHCI の ID の register と BAR2 の DDR3 の読み書きが通る（host の小さな道具）、MSI が届く。VFIO に bind できる。

## 記録

- 2026-10-25 01:08 Q1: Future Work fw-kc705-pcie から戻して計画（ユーザーの最初のゴール）。
- 2026-10-25 15:49 Q1: ユーザー「PCIeブリッジを今から作ってください。」→ 板の要らない部分（上の 1〜4）を P2 に（q1579、p038 の今の OOC の記録の後）。
- 2026-10-25 16:30 ユーザー「AXI の bridge」（判断 P1、原文は Master の decisions-log）。BAR2 を MC を通さず interconnect で DDR3 へ、の P2 の案も Q1 が採用。
- 2026-10-25 17:07 P2 → Q1（申告）: 60d6e25（p2-p013、基点 aaf8e35）。Vivado 2026.1 の probe で XDMA の functional_mode は xc7k325t で DMA のまま（AXI_Bridge は無視される）→ 決定どおり axi_pcie 2.9。RTL: rtl/common/am_axi_cdc.v（5 channel の am_afifo、両側の reset を合わせる）、rtl/gpu/top/atg_pcie_msi.v（§15.2、vector 0 = GPU・1 = DC、立ち上がりと IRQ_STATUS/ENABLE の write の後の再送、4 相の handshake）、boards/kc705/lib/atg_kc705_pcie.v（IBUFDS_GTE2、bridge、BAR0 128 KiB → AXI 0・BAR2 64 MiB → AXI 0x8000_0000 の crossbar、BAR0 の 128 → 32 の変換、MSI、GPU → host の S_AXI は本物の port で v1 は idle）、MIG の wrapper に BAR2 の s2_*（clock 変換・128 → 512・GPU と 2:1 の crossbar）、board top に pcie_* と BAR0 → atg_top s_* の CDC（JTAG の bridge は残す）、LED4 = link_up、XDC に KC705 1.7 の board file の PCIe の pin、boards/kc705/ip.tcl（7 つの core を Vivado で生成して xci と port を読み戻した、lint の stub はそこから生成）。sim: sim/models/kc705/atg_kc705_pcie.v、sim/tb/atg_kc705_pcie_tb.v（bar0・bar2・msi の group PASS、再送を外すと msi が FAIL）。atg_kc705_raster PASS、lint 142 clean、arith-lint atg_pcie_msi 0。逸脱と未確認: BAR は両方 32 bit（axi_pcie は BAR の幅が一括で prefetchable の選択が無い、§15.2 は BAR2 64 bit prefetchable）、PG055 未読（INTX_MSI_* の handshake と axi_aresetn = PERST# は port の一覧から）、PCIe の user clock と gclk の clock group の制約は comment だけ。板・MIG の core の Tcl・bitstream・VFIO・host の道具は p024。make sim・Verilator は T。
- 2026-10-25 17:07 Q1: p013 の板の要らない部分は済み（T 待ち）。残りは p024 と一緒。
