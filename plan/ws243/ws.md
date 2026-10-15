<!-- awesome-plan project=zedbsd record=ws243 -->

# WS243: nvrtx の NVDEC で H.264 の decode（Vulkan Video）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-15 作成。code だけ先に書き、実機の debug は [WS241](../ws241/ws.md) の完了の後）
Primary Milestone: MG006
Related Milestones: MG002
Parent: [Master](../master.md)
Related: [WS037](../ws037/ws.md)（nvrtx、GSP-RM・channel・Vulkan の executor）、[WS083](../ws083/ws.md)（Vulkan Video の拡張と i915 の H.264、`src/drivers/gpu/i915/render/video.c`）、[WS236](../ws236/ws.md)（Pi 4 の Vulkan Video、`src/drivers/gpu/bcm2711/vulkan-video.c`）、[WS202](../ws202/ws.md)（libmedia の `media_vkvideo_ops`）、[WS242](../ws242/ws.md)（RTX 20/30/40）
Resume point: p001（設計）から。担当は Q1 が割り当てる。
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-15）

「それから、NVDECでH.264を利用できるようにするWSを作ります。Vulkan Videoで利用できるようにします。」
「NVDECはまずコードだけ書いておいて、WS241が完了してから実機デバッグします。」

## 目的と完了の条件

- nvrtx の Vulkan の device が `VK_KHR_video_queue`・`VK_KHR_video_decode_queue`・`VK_KHR_video_decode_h264` を出し、libmedia の今の `media_vkvideo_ops` の道（i915・Pi と同じ）で H.264 の mp4 が NVDEC で decode される。
- 完了: RTX 2070（TU106）の実機で Video Player が H.264 の mp4（WS236 の試験の clip、720p・1080p）を NVDEC で再生し、decode の結果が software の decoder と hash で一致する。30/40 は実機の確かめなし（WS242 と同じ扱い）。
- 範囲外: HEVC・AV1・VP9、encode（NVENC）。

## 前提（Q1 の理解、設計で置き換える）

- NVDEC は stateless（driver が picture の parameter と slice の bitstream を渡し、DPB は呼び手が持つ）で、Vulkan Video の形に近い。Turing の NVDEC の class・picture の parameter の構造は NVIDIA の公開の header（open-gpu-doc・open-gpu-kernel-modules、MIT）にある。Mesa NVK の video の decode（MIT）も参考になる。
- GSP-RM の環境では NVDEC の engine の channel を RM に割り当ててもらう（GR の work channel と同じ道、engine の class が違う）。NVDEC の firmware は GSP-RM が load するかを設計で確かめる。
- 出力は NV12（または生の block linear の形）で、compositor が sample できる VkImage に置く。
- 参考の source の扱いは WS037 と同じ（値は事実として使い、名前・構造・comment は写さない、guardrail）。

## Phase（案、p001 の設計で確定）

| ID | 内容 | 状態 |
| --- | --- | --- |
| p001 | 設計: NVDEC の channel の取り方（RM の alloc、class、firmware）、picture の parameter の組み立て、DPB と出力の image の layout、Vulkan Video の object と executor の口（WS083・WS236 の共通の部分の再利用）、license の監査、試験の道 | planned |
| p002 | 実装: NVDEC の channel と engine の起動（code のみ、host 試験） | planned |
| p003 | 実装: Vulkan Video の object（profile・capability・session・parameters・DPB）と H.264 の decode の命令（code のみ、host 試験） | planned |
| p004 | 実機の debug（WS241 の完了の後、RTX 2070 の素の起動、T の試験） | planned |
| p005 | 規約の全文の見直し | planned |
