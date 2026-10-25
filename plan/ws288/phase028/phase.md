<!-- awesome-plan project=zedbsd record=ws288-p028 -->

# ws288-p028 QEMU の passthrough の guest の zedBSD から KC705 の GPU で Vulkan（最初のゴール）

- 親: [WS288](../ws.md)。
- 状態: planned（2026-10-25 01:08 Q1 新設）
- 依存: p011（Verilator の端から端）、p013（PCIe）、p006（PCIe の driver）、p019（`vk/`）、p024（板）。
- 承認: 2026-10-25 ユーザー「了解です。SAはfuture workに回します。p009, msaaを優先して実装してください。そのあと、p010,p018,p019,p006,p011を実装します。KC705のDDR3をVRAMにしてPCIeのGPUボードにすることで、QEMUからパススルーで利用し、ゲストのzedBSDからVulkanを動作させることを、最初のゴールにします。必要なphaseは作成してください。」

## 範囲

- host の Linux の QEMU（KVM、amd64）に KC705 を VFIO で passthrough、guest の zedBSD の GHCI の driver が PCIe で attach、firmware の load、session、DDR3 の VRAM の割り当て。
- guest の libvulkan → `vk/` → ring → firmware → GPU で vkdemo（または同等の三角形・texture の program）を描き、結果を readback で参照と比べる。表示は KC705 の HDMI（DC）の scanout を確かめられれば加える。
- 検証の形（QEMU の zedBSD の guest の判定の規則に合わせて、boot-test.sh・SSH/QMP の PNG のどれを使うか）は着手の前にユーザーに確かめる。

## 受け入れ

guest の zedBSD から Vulkan の program が KC705 の GPU で動き、readback が参照と一致。detach/reattach（guest の再起動）でも動く。

## 記録

- 2026-10-25 01:08 Q1: 新設（ユーザーの最初のゴール）。VRAM を host の memory にする形は Future Work（fw-kc705-hostmem、Sukuna RISC で KC705 の CPU + GPU を動かす実験が先）。
- 2026-10-26 04:41 ユーザー「グラフィックデバイスなしでもPCのUEFIって起動できるんでしたっけ？」→「じゃあPCのLinux qemuじゃなくて、Pi5のM.2でzedBSDで試そうか」→（Q1 が zedBSD に BCM2712 の pcie1 の root complex の driver・MSI・非一貫の DMA の cache の扱いが無いと答えた後）「うーん、じゃあ先にLinuxを試しましょう。」→ Q1 の問い「「先に Linux」はどちらの意味ですか？」の回答「PC の Linux + QEMU VFIO」。→ 最初のゴールは元の p028 のまま（PC の Linux を host に KC705 を vfio-pci に渡し、QEMU の guest の zedBSD から Vulkan）。host の PC は内蔵 GPU のある機種を使う。Pi 5 の M.2 で zedBSD が直に使う案は後の候補（Future Work: pcie1 の driver・MSI・cache の clean/invalidate）。
