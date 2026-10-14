<!-- awesome-plan project=zedbsd record=ws236-p001 -->

# ws236-p001: 調査と設計（VCHIQ・MMAL・Vulkan Video の橋渡し）

Parent: [WS236](../ws.md)
Status: in-progress（P3、2026-10-14。設計の第 1 版を書いた。design-reviewer の review と J1〜J5 の返事待ち。返事を待たずに p002 へ進む）
Phase disposition: normal

## 由来

2026-10-14 ユーザー:「WS236を実装します。」「VC6では、Vulkan VIdeoのドライバとして実装してほしいです。V3Dと連携しなかったとしても。…HEVCはライセンスがないのでやらないです。エンコードは今回は置いておきます。」

## 成果

- 設計: [design.md](../design.md)（第 1 版）。
- 調べた tree の事実（2026-10-14、main 9fc15a7b8）:

| 所 | 事実 |
| --- | --- |
| libvulkan | 各 `/dev/gpu*` の node が 1 つの VkPhysicalDevice。capset 176 byte の native の語（byte 168 = "TANZ"、172 の bit 0）で video の拡張と synchronization2 を列挙し、queue family の video の flag と format の video の feature を通す（`context.c`・`instance.c`）。video の command は zedBSD の opcode 0x10000〜0x1000d（`uapi/gpu-op.h`） |
| i915 | executor の `render/vulkan.c` が capset を出し、`render/video.c`（3280 行）が wire の decode・session・parameters・記録・MFX への実行を持つ。手本にする（wire の形は同じ、実行の先が MMAL に変わる） |
| bcm2711（WS141） | V3D の executor は route の表（`vulkan-dispatch.c`）、queue family は 1 つ（GRAPHICS・TRANSFER）、capset は 168 byte（`render-runtime.c`）、memory は全て 1 GiB の下の連続の uncached（`vulkan-memory.c`）、image は RGBA8 だけ（`vulkan-resource.c`）、buffer と image の copy は CPU の byte copy（`vulkan-buffer-copy.c`）、native job は prepared の event を順に実行し、copy・barrier は CPU の仕事として並ぶ（`vulkan-native-job.c`）。executor は全 session で 1 本の thread、step ごとに round robin |
| libmedia | `vkvideo.c`・`vkvideo-runtime.c`: decode の family と transfer の family を同じ device に求め（同じ family でよい）、NV12 の OPTIMAL の DPB（COINCIDE なら DPB＝出力）、host-visible の bitstream、1 枚ごとに submit して fence を待ち、`vkCmdCopyImageToBuffer` の PLANE_0・PLANE_1 で読み出す。表示の順への並べ替えは libmedia 自身（POC）。status の query は family が対応する時だけ |
| vkvideo-probe | 最初の device、NV12 の image を host-visible に置き map、`vkGetImageSubresourceLayout` で plane を引き、Intel の Tile Y を de-tile（Pi では直しが要る） |
| rpi4 の platform | mailbox の property は `rpi4-firmware.c`、DT は `drv_fdt_*`、DMA の窓は `drv_dma_device_create_window`（0xc0000000 + 物理）、IRQ は `kern_irq_*`。driver は `CONFIG_DRIVER_BCM2711_*` で `vmunix.mk` と `rpi4.c` の `#if` |
| VCHIQ・MMAL | tree に無い。参考は FreeBSD の contrib/vchiq（BSD-3 / GPL dual）と Raspberry Pi の userland（BSD-3）。Linux の source は見ない |

## 判断が要る点

design.md §12 の J1〜J5。既定で進める。

## 検証

- 設計だけの Phase。build と試験は無し。
- 参考の source は P3 の scratchpad に取り寄せた（commit しない、license は design.md §2）。

## 残り

- design-reviewer の指摘の反映（Q1 から届いたら）。
