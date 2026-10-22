<!-- awesome-plan project=zedbsd record=ws282-p001 -->

# ws282-p001 camera の枠組みと source の driver の設計

- 親: [ws282](../ws.md)、Queue: Q1 の割当（P3、2026-10-23。版 2 は worktree p3-cam2・branch agent/p3-cam2、基点 main 61832637b）
- Status: in-progress（設計の版 2 を書いた。Q1 の判定待ち）
- 成果物: [design.md](../design.md)（版 2）、[review-20261023.md](review-20261023.md)（版 1 の review、Q1 の手配）

## 目的と受け入れの条件

- `/dev/camN` の枠組みの API（format の列挙、mode、buffer の queue と GPU との共有、start/stop、frame の metadata、control、source の分離、node の権限、hotplug）と、最初の source の driver の設計、Phase の分け方と見積もり、試験の計画、危険、ユーザーの判断を design.md に書く。実際の code を読み、仕様と参照の出所を段ごとに示す。コードは書かない。
- 受け入れ: design.md が上の項目を全部持ち、Q1 の手配する review で致命の欠落が無い。
- 範囲の変更（2026-10-23 ユーザー、Q1 経由）:「USB Video classを優先しましょう。CSIは今回はやりません。/dev/camNのAPIはそのまま使います。uvcドライバを実装します。カメラは今から購入します。まずは手元にあるHDMIキャプチャでもデバッグには使えそうですか？」→ 最初の source は **UVC（uvc）**。Pi 5 の CSI（rpicam）は design.md §13「今回はやらない」と付録 A（版 1 の調べ）に移した。

## ユーザーの判断（2026-10-23「decisionは推しでいいです」「カメラケーブルは手元にあります。」）

| # | 答え | 反映 |
| --- | --- | --- |
| D1 | connector はどちらでも（driver が 1 → 0 の順に探す）。22 pin の cable はある | design §6.1、付録 A.1（CSI は今回やらない） |
| D2 | (a) `lpss-i2c.c` から DesignWare の core を `src/drivers/i2c/dw-i2c-core.c` に抜いて共有、T1 に x86 の i2c-hid の回帰 | 付録 A.7（CSI は今回やらない） |
| D3 | A: GPU が確保・camera が取り込む | §3.6、p008 |
| D4 | 0600 + sessiond の seat | §3.1、p003（devfs）・p009（seat） |
| D5 | IMX219 は後で（ID の読みだけ） | 付録 A.5 |
| D6 | 1296x972 RAW10 約 32 fps、sensor の AEC/AGC on | 付録 A.5・A.6 |
| D7 | 既定は 16 bit unpacked（`sample_shift` を ABI で） | §3.3、付録 A.2 |

今回の問い（§6.2）: D8 HDMI capture は使える（制約付き、答えを design に）、D9 GPU の差分の担当、D10 xHCI の isoch の拡張の担当（WS197 の source）、D11 板の試験の担当、D12 買う camera（任意）。

## 記録（2026-10-23 P3、版 2）

- 読んだ物（版 1 の物に加えて）: `include/drivers/usb/usb.h`、`src/drivers/usb/usb.c`（alternate・zero-bandwidth・URB）、`usb-bt.c`（SCO の isochronous・worker・停止）、`usb-hid.c`（attach・detach・quiesce）、`src/drivers/pci/pci-xhci.c`（`xhci_isoch_enqueue`・`xhci_isoch_finish`・`xhci_request_alloc`）、`pci-xhci-isoch.c/.h`、`include/drivers/pci/pci-xhci-control.h`（endpoint context の encoder）、`include/kern/cdev.h`、`src/drivers/generic/hidraw.c`、`src/kern/vm/device.c`、`include/kern/pmem.h`・`src/kern/pmem.c`、`src/drivers/audio/audio.c`、`src/kern/devfs.c`、`userland/desktop/sessiond/seat.c`、`userland/desktop/libjpeg-compat/`、`platform/{amd64,arm64,riscv64}/vmunix.mk`、`Makefile`、`config/ci/config-rpi4.mk`・`config-rpi5.mk`、GPU 側（`gpu.c`・`bcm2711/{share.c,render-device.c,buffer.c,vk/vulkan-memory.c}`・`gpu-share.h`・`gpu-scanout.h`、`libvulkan/memory.c`）。Linux の rp1_cfe・ov5647 などは版 1 と review の写し（scratchpad、commit しない）で照合。
- 実行した command: `git worktree add /home/awe/zedBSD-worktrees/p3-cam2 -b agent/p3-cam2 main`（61832637b）、`grep`/`sed` で source を読んだ。build・QEMU・実機は無し（設計の Phase）。
- 結論: design.md 版 2 = 枠組み（§1〜§3、§5）+ uvc（§4）+ 試験（§7）+ Phase p002〜p011（§11、29〜40 LW、p010 を除けば 27〜37）+ review の反映の表（§12）+ CSI の付録（§13、付録 A）。
- 確かめた事実（設計の根拠）: (1) `drv_xhci_endpoint_context_encode` は isochronous を FS/HS の 1 transaction に限り、high-bandwidth（wMaxPacketSize の bit 11〜15）と SuperSpeed を拒む。`xhci_isoch_enqueue` も packet を `max_packet_size & 0x7ff` 以下に限る → 1024 byte/interval（HS 8.19 MB/s）が今の上限（design §4.7、p005）。(2) `xhci_isoch_enqueue` は 64 KiB の境界をまたぐ packet を EINVAL で拒む → packet の stride を 4096 に（§4.3）。(3) `xhci_isoch_finish` は staging から class driver の buffer に `kern_memcpy` する → uvc の buffer に cache の操作は要らない（§3.4）。(4) isochronous の URB の callback は xHCI の command gate の下で走り得る（usb.h）→ worker の thread（usb-bt の SCO の型）。(5) `bcm2711_shared_backing` は `has_image` 無しで ENOTSUP（review の H1 の通り）、Vulkan の memory は `bcm2711_buffer_create_uncached` の uncached の run（H2 の反映）。(6) `vm_device_create` は 1 つの物理 base を取る → buffer は連続の run。(7) devfs の既定は 0666（M10）。
- HAL の API（hal.h）の変更: 不要。
- 他の WS の source に頼む変更（Q1 へ、design §11）: p003 `src/kern/devfs.c`（`cam` の 0600）、p005 `include/drivers/pci/pci-xhci-control.h`・`src/drivers/pci/pci-xhci.c`（isochronous の high-bandwidth と SS、WS197）、p008 `src/drivers/gpu/gpu.c`・`include/drivers/gpu/{gpu.h,gpu-share.h,gpu-scanout.h}`・`src/drivers/gpu/bcm2711/{share.c,render-device.c}`（WS031/WS141/WS279）、p009 `userland/desktop/sessiond/seat.c`。
- 未実施: 版 2 の review（Q1 が手配）、実機の確認（p003 以降）、HDMI capture の descriptor の dump（p003 で実体が決まる）。
- 敵対的に読み直した点（版 2）: (1) 版 1 の案 A の「gpu.c に 1 関数」は share.c の `has_image` の検査で成り立たない事を自分でも読んで確かめ、bcm2711 の新しい op と GPU core の関数に直した。(2) uvc の経路は device の DMA が xHCI の staging に閉じるので H2/H3 の cache の穴は uvc には無い事を `xhci_isoch_finish` で確かめ、枠組みの buffer の種類を flag で分けた（DMA の source は付録）。(3) xHCI の isochronous の制約（1024 byte/interval）を encoder と enqueue の両方で確かめ、HDMI capture の実用には p005 の拡張が要ると書いた。(4) 64 KiB の境界の検査で packet を詰めると 22 番目で失敗する事を計算し stride 4096 に。(5) 退役（抜去）の順序（admission → waiter → unregister → finalizer）を hidraw と usb-hid の型で決めた。(6) MJPEG の DHT 無しと `dwMaxVideoFrameSize` の嘘、EOF 無し、PROBE の stall を仕様の範囲の対処として入れ、device ごとの quirk は p006 で。

## review の指摘の反映（版 1 の review → 版 2）

| 指摘 | 変更 | design の節 |
| --- | --- | --- |
| H1 GPU の取り込みが bcm2711 で ENOTSUP | bcm2711 の `get_allocation_backing` + `DRV_GPU_BACKING_UNCACHED` + gpu.c の `drv_gpu_shared_backing`、担当 D9、p008 の依存 | §3.6、§6.2、§11 |
| H2 取り込んだ GPU の buffer の cache | `UNCACHED` だけ受け、枠組みの uncached の alias で CPU が書く。cached は EXDEV。fence の契約 | §3.6、§9 |
| H3 driver の buffer の alias と cache | uvc は CPU 書きの cached の run（DMA は xHCI の staging）。DMA の source は `drv_dma_alloc_coherent` の形を flag で分け、p002 は ENOTSUP | §3.4、付録 A.3 |
| M1 Bayer の表 | GBRG に直した（付録） | 付録 A.5 |
| M2 REMAP の値の位置 | ABI の `sample_shift`（測定は CSI の WS） | §3.3 |
| M3 `kern_usleep_range` | 眠る op は mutex の下・spinlock の外、slot の API は眠らない | §1、§2.2、§5 |
| M4 停止の順序 | uvc の停止の順。CSI は付録 | §4.3、付録 A.4 |
| M5 所有 | `SET_FORMAT` も所有を取る | §3.1 |
| M6 電源と control の保持 | uvc には無い。CSI は付録 | 付録 A.6 |
| M7 interface の欠け | source の関数表と slot の API を作り直し、DEQUEUE の待ち・QUEUE の重複・STREAM_OFF の列 | §2、§3.2、§5 |
| M8 AEC/AGC の metadata | UNKNOWN の flag | §3.5 |
| M9 誤りの割り込み | CSI: IRQ_MASK 0（付録） | 付録 A.4 |
| M10 devfs の順 | p003 の前に Q1 に依頼 | §3.1、§11 |
| M11 build | §4.8（3 platform の USB の class の列） | §4.8 |
| L1 mmap の offset | `index × roundup(image_bytes, PAGE)` | §3.2、§3.4 |
| L2 mapping と close | 書いた、host の試験に | §3.1、§7.1 |
| L3 sequence と INCOMPLETE | take ごとの sequence、INCOMPLETE の意味 | §3.5、§4.5 |
| L4 I2C の時間 | 付録 | 付録 A.5 |
| L5 cable | D1 | §6.1 |
| L6 Phase | 作り直し、D11 | §11、§6.2 |
| L7 その他 | EXPORT は ENOTTY、別の fd は EBUSY、fence | §3.2、§3.6 |

## 見積もりの変更

- 版 1: p002〜p009 で 25〜34 LW（CSI）。版 2: p002〜p011 で 29〜40 LW（p010 の control を除けば 27〜37）。増えた理由: xHCI の isochronous の拡張（p005、3〜4 LW）、画素の試験と堅牢化（p006）、3 枚の板（p007）、hotplug（枠組み）。減った物: CSI の I2C・CFE・sensor（付録に）。
- 依存: p005 は WS197 の source（Q1 の承認）、p008 は WS279 の V3D と GPU core の差分、p003/p009 は devfs/seat（Q1）。
