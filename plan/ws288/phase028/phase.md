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
- 2026-10-26 04:4x ユーザー「Core i5 12400Fのマシンなので、GPUを抜いたらUEFIが起動を拒む可能性があります。念のためリスクとして。」→ リスクとして記録: host の PC（Core i5-12400F）は内蔵 GPU が無い。GPU を抜くと UEFI が POST で止まる恐れ。対策の案（Q1）: 今の GPU を x16 の slot に残して host の Linux の表示に使い、KC705 は別の slot（chipset の x4 など。x8 の card は端の開いた slot か riser）に挿して vfio-pci に渡す。確かめる事: KC705 の slot の IOMMU group が他の装置と分かれているか（chipset の slot は group を共有しがち、ACS）、KC705 の bitstream を電源の前に flash から読む（PC は UEFI の起動時に link が要る）。
- 2026-10-26 04:5x ユーザー「とても安いマザーボードなので、PCIe x16が1つと、PCIe x1が1つしかないんです。スロット形状。」→ Q1 の案（推し A）: A) KC705 を x16 の slot（CPU の root port、IOMMU の group が分かれやすい）、表示の GPU を x1 の slot に x1 → x16 の riser で（表示だけなら x1 で足りる）か x1 の安い GPU。B) GPU を x16 のまま、KC705 を x1 の slot に riser で（Gen2 x1 約 500 MB/s、chipset の slot は IOMMU の group を共有しがち）。C) GPU を抜いて headless（12400F は POST で止まる恐れ、試すだけ）。ユーザーの選びを待つ。
- 2026-10-26 04:59 ユーザー「ライザーは今注文したので、今日の夜には届きます。まずは既存GPUを取り外してKC705を装着して起動してみることにします。だめならライザーを使います。IOMMUのグループについては了解です。」→ 順: まず C（GPU を抜いて KC705 を x16）で起動を試す、POST で止まれば A（riser で GPU を x1、KC705 を x16）。riser は 2026-10-26 夜に届く。bitstream は q1586（P1）の build の後。
