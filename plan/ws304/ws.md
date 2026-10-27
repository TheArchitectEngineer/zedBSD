<!-- awesome-plan project=zedbsd record=ws304 -->

# WS304 Raspberry Pi 5 の外の PCIe（pcie1、M.2）と NVMe

- Master: [plan/master.md](../master.md)（2026-10-27 10:33 作成）。Primary Milestone: MG003。関係: WS278・WS279（Pi 5、completed）、WS048（Pi 4 の PCIe、src/drivers/pci/pci-brcmstb.c は BCM2711 の root complex）、[WS298](../ws298/ws.md)（5330 の NVMe）、[WS299](../ws299/ws.md)（NVMe の速さ）、Future Work fw-kc705-rpi5。
- Status: incomplete（2026-10-27 p001・p002 を q1618-i01 で実施。実機の p003 は SwitchBot 待ち）
- 出典: 2026-10-27 10:33 ユーザー「まだテストできないのですが、Pi5のNVMeも対応したいです。PCIeを使えるようにしないと。」

- 2026-10-27 10:34 ユーザー「Pi5のNVMeハットを買って装着しました。Switchbotの空きがないので、今夜新しいSwitchbotが届くまでテストできないですが、コードだけ書いておくのがいいと思います。」→ p001・p002 を今（P3、q1618）、p003 の実機は新しい SwitchBot が届いた後。

## 今の事実（Q1 が読んだ範囲）

- src/drivers/pci/pci-brcmstb.c は BCM2711（Pi 4）の Broadcom STB の PCIe の root complex（reset・SerDes・outbound/inbound の窓・link、CPU の cache を snoop しない）。
- Pi 5 は src/drivers/platform/rpi5/rp1.c が "brcm,bcm2712-pcie" を探して RP1（内蔵の pcie2）を使う。外の M.2 の pcie1 の root complex を起こす code は無い見込み（p001 で確かめる）。
- p001 の確かめ（2026-10-27）: 無かった。p002 で pci-brcmstb.c の BCM2712 の variant と rpi5-pcie.c・rpi5-mip.c を足した（[design.md](design.md)）。

## 単一の到達目標

Pi 5 の外の PCIe（pcie1、M.2 HAT）の root complex を zedBSD が起こし、その下の NVMe を既存の pci-nvme で使えるようにする（MSI は BCM2712 の MIP、DMA は非一貫の cache の扱い）。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 調査と設計（BCM2712 の pcie1 と BCM2711 の差、firmware の初期化の有無、MSI（MIP）、inbound の窓・4 GiB の上の DMA、非一貫の cache、pci-nvme の arm64 の扱い）、design-reviewer。[design.md](design.md) 版 2 | — | cleared（2026-10-27） |
| [p002](phase002/phase.md) | 実装・host 試験・build | p001 | cleared（2026-10-27、host 試験 3 本 PASS、rpi5・rpi4 の build warning 0） |
| [p003](phase003/phase.md) | 実機（Pi 5 ＋ M.2 の NVMe）での確かめ（手順は phase.md） | p002、実機の用意（ユーザー） | planned（実機待ち） |
| p004 | 規約の全文の見直し | p003 | planned |
