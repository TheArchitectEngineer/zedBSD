<!-- awesome-plan project=zedbsd record=ws256-p012 -->

# ws256-p012 周辺の設計（VisionFive 2）

Status: in-progress（2026-10-19 q1270-i01、P1。初版の設計を書いた。design-reviewer と Q1 の合格の前）

- 親: [WS256](../ws.md)。Queue: q1270-i01。
- 範囲: p011（framebuffer の調べ）と p013〜p019（clock・reset・pinctrl/GPIO・PMIC、SD、rootfs からの起動、PCIe と USB・keyboard、NVMe、GbE と PHY、sound）の設計を `plan/ws256/design-peripherals.md` に書く。code は書かない。HAL の API の差分は案だけ。toolchain は変えない。
- 完了の条件: 部品と register の事実を StarFive の公開資料で確かめ（URL・SHA-256 を記録、確かめられない事は未確認）、各 Phase の既存／新規・HAL・DT・試験・依存・LW・並行の組、実機の判定（#15）と接続（#10）の選択肢と既定の案がそろい、design-reviewer を通り、Q1 が合格とする。

## 成果物

- [design-peripherals.md](../design-peripherals.md)（初版）。

## 実行した事（q1270-i01、2026-10-19）

- worktree `/home/awe/zedBSD-worktrees/p1`（agent/p1）に main ff7c0c597 を取り込んだ（fast-forward、`git log -1` = ff7c0c597）。子のエージェントは起動していない（Q1 の指示）。
- 取得（`build/tmp/ws256-docs/`、git に入れない、URL と SHA-256 は design-peripherals.md §0.1 と同じ directory の `SHA256SUMS`）: StarFive の JH7110 TRM（Preliminary V2）・JH7110 datasheet・VisionFive 2 datasheet、Linux v6.12 の JH7110・VF2 の DTS と binding の header、`sifive_ccache.c`、StarFive の配布の U-Boot と mainline v2025.01 の VF2 の defconfig、StarFive の SDK の README、OpenBSD の JH7110 の driver（ISC）。pdftotext で text にして grep した。
- 読んだ zedBSD の code: ws.md・design.md（改訂 2）・guardrail.md、`src/drivers/pci/pci-brcmstb.c` の構成、`src/drivers/platform/rpi4/rpi4-pcie.c`（VL805 の扱い）、`src/drivers/generic/dma.c`（coherent の確保）、`src/drivers/i2c/lpss-i2c.c`（DW I2C）、driver の directory の一覧。
- QEMU・実機は使っていない。build はしていない（code 無し）。

## 主な所見

- ws.md の p012 の行の部品の名前（Q1 の覚え）の照合: AXP15060（I2C5、0x36）・DesignWare MMC・Synopsys の Ethernet QoS・Motorcomm の PHY（YT8531）・PLDA の PCIe は公開資料で合った。**VL805 は公開資料に無く未確認**（U-Boot の構成から PCIe の先の xHCI は確か）。「XpressRICH」の名前は公開資料に無い。**sound は codec ではなく PWMDAC**（3.5 mm、DTS）。
- **design.md §1.6 の「JH7110 の DMA は非 coherent」は JH7100 の事実の取り違えの可能性が高い**（Linux の ccache の非 coherent の操作は jh7100 だけ、JH7110 の DTS に dma-noncoherent が無い）。推定は coherent で、p013 の実機で確かめる（design-peripherals.md §1.2、判断 #25）。
- p016 を p016a（QEMU virt の ECAM の host で既存の xHCI・HID・NVMe を riscv64 で試す、板の前に可）と p016b（PLDA、実機）に分ける案。
- HAL の API の変更は要らない見込み（coherency の推定が外れた時の予備は HAL の実装の変更だけ）。

## 残り

- design-reviewer（Q1 が回す）、その結果の反映（改訂）。
- ユーザーの判断（Q1 経由）: design-peripherals.md §6 の #15・#10・#2・#20〜#26。
- `build/tmp/ws256-docs/starfive,jh7110-crg.h`（重複の取得の残り）の片付けは Q1。
