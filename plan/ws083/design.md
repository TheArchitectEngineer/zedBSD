# WS083 の設計: Vulkan Video の H.264 decode と i915 の VCS・MFX（ws083-p001）

2026-10-07、P1（kernel-and-driver-designer）。範囲は [ws.md](ws.md) と Q1 の 2026-10-07 の指示: p001 はこの設計、p002 は libvulkan の骨組み（`VK_KHR_video_queue`・`VK_KHR_video_decode_queue`・`VK_KHR_video_decode_h264` の queue family・capability・format・session・parameters・memory の bind・`vkCmdBegin/End/ControlVideoCodingKHR`・`vkCmdDecodeVideoKHR` の記録と host の試験）、その後は host で build・試験できる物（VCS の ring・context の立ち上げの code、MFX AVC の command stream の builder と host の fixture）。実機（Dell Latitude 5330、Alder Lake-P、Gen12 LP、`8086:46a8`）の確認は 5330 が戻ってから。最初の到達点は H.264 8 bit 4:2:0 progressive、I frame から、次に P・B と DPB。encode・H.265・AV1 は範囲の外。Mesa・intel-media-driver の code は写さず、公開の PRM と両 project の **事実** だけを使う（§7）。

この文書の「事実」は 2026-10-07 の worktree `agent/p1`（`04f719aac`）の file を読んだ物で、file と行を添える。「決定」は D 番号、「人の判断」は H 番号、確かめていない物は §11 に集める。

## 0. 読んだ物と前提

- 規則: `AGENTS.md`、`plan/guardrail.md`（HAL・UAPI・license・kernel が include できる libc の header は `libc/vulkan/*` だけ）、`plan/coding-style.md`（§2 file の構成、§13 著作権 header、ほか全文）、`plan/ws031/i915-rebuild-rules.md`（`intel/` の header の書き方、転記の出典・SHA の記載、名前の規則）、`plan/ws075/ws.md`（HAL・UAPI の変更は `proposed/` に提示して承認まで当てない）、`plan/ws167/phase001/phase.md`・`phase002/phase.md`（Kei GPU command protocol、`GPU_OP_OWN_FIRST = 0x10000` から zedBSD だけの command を足す）。
- libvulkan: `userland/desktop/libvulkan/`（`internal.h`・`instance.c`・`device.c`・`commands.c`・`commands-generated.inc`・`external-properties.c`・`context.c`・`codec.h`・`dispatch.c`・`dispatch-table.inc`・`query.c`・`Makefile`・`exports.map`・`tools/maintain-*.noct`・`README.md`）、`include/libc/vulkan/`（`vulkan_core.h`・`vulkan_external.h`・`API-PROVENANCE.md`）、`include/uapi/gpu.h`・`gpu-op.h`・`gpu-job.h`・`gpu-fence.h`。
- i915: `src/drivers/gpu/i915/`（`engine.[ch]`・`context.[ch]`・`submit.[ch]`・`request.[ch]`・`request-queue.h`・`session.[ch]`・`worker.[ch]`・`irq.c`・`mmio.[ch]`・`device.c`・`device-info.[ch]`・`gt-power.c`・`workarounds.c`・`firmware.[ch]`、`intel/engine-table.inc`・`forcewake-ranges.inc`・`gt-regs.h`・`commands.h`・`lrc-offsets.h`・`genxml.h`・`mocs.h`、`render/internal.h`・`dispatch.c`・`instance.c`・`command.c`・`draw.c`・`image.[ch]`・`memory.h`・`gfx.h`・`object.h`・`vulkan.c`・`batch.h`、`tests/render/README.md`）、`src/drivers/gpu/venus/venus.c`、`plan/ws031/tests/run-vk-host-tests.sh`・`i915-vk-cmdbuf-test.c`・`README-vk-host-tests.md`。
- 事実の参照（code は写さない）: Mesa 25.0.7（Debian の source package、worktree の `build/mesa-tools/mesa-25.0.7/`。`src/intel/genxml/gen120.xml` の SHA-256 `e2452c7dd2d19f9c506f487ce98e938984b6bdf8a3ea2504c64afdd4facd542e` は `intel/genxml.h` の記載と一致）: `src/intel/vulkan/anv_video.c`・`genX_cmd_video.c`・`anv_image.c`・`anv_formats.c`・`anv_physical_device.c`・`anv_private.h`、`src/intel/genxml/gen{120,110,90,80,75}.xml`、`src/vulkan/runtime/vk_video.c`、`include/vk_video/*.h`。Khronos の registry `/usr/share/vulkan/registry/vk.xml`（libvulkan-dev 1.4.309.0-1）の `depends` 属性。`/usr/include/vulkan/vulkan_core.h`（1.4.309）の video の構造体・enum の一覧。
- 道具: host の `ffmpeg` 7.1.5（`libx264` の encoder あり、`/usr/bin/ffmpeg`）。

## 1. 今の形（事実）

### 1.1 libvulkan は薄い転送器

- 公開 API は Vulkan 1.0 の 137 command と WSI・properties2・external の KHR で、`physical->properties.apiVersion` は `VK_API_VERSION_1_0` に固定（`instance.c` 1005〜1009）。instance の `apiVersion` は 1.1 を要求して開く（`instance.c` 756）。
- `vkGetPhysicalDevice*` は backend に `GPU_OP_GET_PHYSICAL_DEVICE_*` を送って返事を decode する。queue family は `physical_load_queues`（`instance.c` 1130〜1260）が backend の返事をそのまま cache し、`queueFlags` を**濾さない**（`VULKAN_QUEUE_TIMELINE_COUNT` 64 に収まるよう `queueCount` だけ調整）。
- 拡張の有無は libvulkan が自分で決める（`physical->supported_extensions`、`instance.c` 1075〜1093、capset の `GPU_CAP_*` で分岐）。`vkCreateDevice` は名前を `strcmp` で照合する（`device.c` 306〜382）。properties2 系は `external-properties.c` が**手元で**答える（`vkGetPhysicalDeviceQueueFamilyProperties2KHR` は cache を写し pNext を触らない、156〜190 行。`vkGetPhysicalDeviceFormatProperties2KHR` は core を呼ぶだけ、80〜90 行）。
- 記録: 各 `vkCmd*` は `command_record_begin`（`commands.c` 535〜560）で `[opcode u32][0 u32][cmdbuf id u64]` を書き、引数を続け、`command_record_finish`（564〜618）が command buffer の `recording` に追記する（1 MiB か `max_resource_bytes` を超えると途中で backend に流す）。`vkEndCommandBuffer` が残りを流す。記録の encoder は `commands-generated.inc`（`tools/maintain-commands.noct`）、構造体の encoder/decoder は `codec.c/h`（`tools/maintain-codec.noct`、pointer の長さの規則を表で持つ）。
- queue: 論理 device の queue ごとに renderer の timeline 1〜63 を予約する（`device.c` 585〜641）が、`GPU_COMMAND_SUBMIT` は常に `timeline = 0`（`context.c` 824）。
- 入口の表 `dispatch-table.inc`・`api-commands.tsv` は `tools/maintain-dispatch.noct` が公開 header（`vulkan_core.h`・`vulkan_wayland.h`・`vulkan_external.h`）から作り、件数 173 を検査する。`exports.map` は `vk*` を global にする。
- 公開 header `include/libc/vulkan/vulkan_core.h`（Khronos 1.3.269 からの選択）には video の **enum の値** は入っている（`VK_STRUCTURE_TYPE_VIDEO_*` 419〜、`VK_IMAGE_USAGE_VIDEO_DECODE_DST/SRC/DPB_BIT_KHR` 2432〜2434、`VK_QUEUE_VIDEO_DECODE_BIT_KHR` 2498、`VK_IMAGE_LAYOUT_VIDEO_DECODE_*`、`VK_FORMAT_G8_B8R8_2PLANE_420_UNORM` 1651）が、`VkVideo*` の構造体・関数・`StdVideo*`（`vk_video/` の header）・`VK_KHR_synchronization2` は無い。
- opcodes は `include/uapi/gpu-op.h`（ws167、版 1 は Venus の wire format 1 の番号、zedBSD だけの command は `GPU_OP_OWN_FIRST 0x10000` から）。

### 1.2 i915 の Vulkan 実行器（`src/drivers/gpu/i915/render/`）

- `dispatch.c` が opcode を graphics object → command 記録 → 範囲 → transport・instance の順に渡す。知らない opcode は reader を poison し submission を失敗させる（長さの word が無いので飛ばせない）。object は `drv_i915_object_insert/lookup`（`object.h`、`enum i915_vk_object_kind`、`internal.h` 47〜76）。
- **同期実行**: `vkQueueSubmit`（`command.c` 3380〜3500）は command buffer の操作列を 1 つの batch に記録し、`drv_i915_gfx_submit_end` → `draw.c` 426 の `drv_i915_worker_run_sync(session->gpu->contexts[I915_ENGINE_RCS0], batch_va)` で**最後まで走らせてから**返事し、fence はその後に latch する。barrier・semaphore は decode するだけで何もしない（`command.c` 996「nothing for a barrier to order」）。
- memory: `VkDeviceMemory` の storage は libvulkan が export する blob（`memory.h`、`gfx.h` 126〜）。GPU address は `drv_i915_gfx_memory_va(memory, offset)`、CPU view は `drv_i915_gfx_memory_cpu`。memory type は 1 つ、`DEVICE_LOCAL | HOST_VISIBLE | HOST_COHERENT`（`instance.c` 482〜492）。
- image: linear の 1D/2D/3D と mip、depth と multisample だけ Y tile（`image.c` 73〜80・397〜420）。planar（NV12）は無い。format の feature の表は `instance.c` 565〜600（R8・R8G8・RGBA8 など）。`vkGetPhysicalDeviceImageFormatProperties` は usage から要る feature を引いて照合する（`instance.c` 732〜762）。
- queue family は 1 つ（graphics|compute|transfer、`instance.c` 507〜545）。`vkGetDeviceQueue2` は family を読み飛ばす（860〜900、「XXX: there is one timeline, on RCS0」）。
- capset（`vulkan.c` 275〜296）: 168 byte、byte 160 に vendor tag `0x5a424453`、byte 164 に vendor flags 7（OPAQUE|STRICT_QUEUE|QUIESCE）。libvulkan はこの flags を**完全一致**で見る（`context.c` 172〜195）。
- MOCS は `GEN12_MOCS(I915_MOCS_UNCACHED_INDEX)` を surface に（`draw.c` 519、`blit.c` 634）、batch の間接 state には `I915_MOCS_WRITEBACK_INDEX`（`state.c` 800）。

### 1.3 i915 の GT と engine

- engine の表 `intel/engine-table.inc` は RCS0（0x2000）・BCS0（0x22000）・**VCS0（0x1c0000）**・VCS2（0x1d0000）・VECS0（0x1c8000）。media の fuse で消えた物は落とす（`device-info.c` 507〜560）。`drv_i915_engines_init`（`engine.c` 385〜440）は **GT が報告する全 engine** に status page・execlists の state・kernel context（4 KiB ring）を作り、`drv_i915_gt_resume` も全 engine を enable する（`engine.c` 512・543 の loop）。
- LRC: `context.c` 542〜545 が class で `gen12_xcs_offsets` / `gen12_rcs_offsets`（`intel/lrc-offsets.h`）を選ぶ。render だけの物は `CTX_R_PWR_CLK_STATE`（246）と indirect ctx の CMD_BUF/state cache の WA（798・811）。request の flush は class で分かれ、video は `MI_INVALIDATE_BSD` を立てる（`request.c` 534〜575）。
- 割込み: `irq.c` 466〜474 が RCS・BCS・VCS・VECS の class の割込みを enable・unmask し、identity を class・instance で engine に引き当てて（778〜830）、user interrupt と context switch で**1 つの** engine waitq を起こす（706〜741）。
- forcewake: domain に `I915_FORCEWAKE_MEDIA_VDBOX0`・`VDBOX2`・`VEBOX0`（`mmio.h` 40〜46、request/ack は `mmio.c` 34〜42）。範囲表 `intel/forcewake-ranges.inc` 78 行は `0x1c0000〜0x1c3fff` を VDBOX0 に。device の start は **5 domain 全部**を service の間ずっと持つ（`device.c` 118〜129 `i915_start_domains`、`worker.c` 310）。park で返し unpark で取り直す（`worker.c` 1216・1240）。
- 電源: `gt-power.c` 148 で render・media・media sampler の power gating を常に有効、154〜166 で存在する VCS ごとに `VDN_HCP_POWERGATE_ENABLE`・`VDN_MFX_POWERGATE_ENABLE`。WA: `Wa_14011060649`（偶数 VCS の IECP の clock gating、`workarounds.c` 1025〜1047）。
- firmware: `firmware.c` は `/lib/firmware/<name>` を読む provider で、driver は GuC・HuC を使わない（`firmware.c` に GuC/HuC の参照なし、WS031 設計 §5「GuC/HuC 不使用」）。submission は execlists（`submit.h`）。
- **worker は RCS0 だけ**: 「XXX: only the render engine is connected」（`worker.c` 582〜586、1383〜1388 で `engine->index != I915_ENGINE_RCS0` は ENOTSUP）。hardware context の record は render engine の 32 個（`I915_WORKER_CONTEXTS`、16 KiB ring）。engine の record は RCS0・BCS0 の 2 つ（`request-queue.h` 33〜35）、session は open で record ごとに context を作る（`session.c` 161〜170）。timeline → record の写像は `session.c` 69〜97。実行は 1 request ずつ、終わりは CSB の処理と engine の割込みでの wake。hang は fail するだけで reset・回復は無い（`worker.h` XXX）。

### 1.4 Venus（QEMU の host の renderer）

- kernel の `venus.c` は host の capset 4 をそのまま渡す（716〜771）。libvulkan は host の queue family を濾さないので、host の Vulkan が video の family を出せば `VK_QUEUE_VIDEO_DECODE_BIT_KHR` がそのまま app に見える。
- Debian 13 の Mesa 25.0.7 の ANV は video decode を**既定で無効**（`anv_physical_device.c` 2557 `debug_get_bool_option("ANV_VIDEO_DECODE", false)`、2349 で family の flag はその値に従う）。QEMU の host の renderer が Venus の wire に video の command を持つかは**未確認**（virglrenderer の source が手元に無い。§11）。
- 結論: QEMU/Venus では video の queue family も拡張も**出さない**（D3）。

### 1.5 Mesa（ANV 25.0.7）と genxml から取った事実

- H.264 decode の capability（`anv_video.c` 112〜161）: `minBitstreamBufferOffsetAlignment` 32、`SizeAlignment` 1、`pictureAccessGranularity`・`minCodedExtent` 16x16、`maxCodedExtent` 4096x4096、`flags = SEPARATE_REFERENCE_IMAGES`、decode caps `DPB_AND_OUTPUT_COINCIDE`、`maxDpbSlots` 17（`ANV_VIDEO_H264_MAX_DPB_SLOTS`）、`maxActiveReferencePictures` 16、`maxLevelIdc` 5.1、`fieldOffsetGranularity` 0、std header `VK_STD_vulkan_video_codec_h264_decode` 1.0.0。luma≠chroma の bit depth、4:2:0 以外、8 bit 以外は `VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR`。
- session の scratch（`anv_video.c` 405〜421、`width_in_mb = align(maxCodedExtent.width, 16) / 16`）: intra row store `width_in_mb * 64`、deblocking filter row store `width_in_mb * 64 * 4`、BSD/MPC row scratch `width_in_mb * 64 * 2`、MPR row scratch `width_in_mb * 64 * 2`。各 bind index に alignment 4096（478〜501）。
- 画像ごとの direct MV buffer（`anv_image.c` 883〜894）: `w_mb * h_mb * 128` byte、64 KiB 整列、image の private binding に置く。
- 命令列（`genX_cmd_video.c` 882〜1256、`anv_h264_decode_video`）: `MI_FLUSH_DW`（Video Pipeline Cache Invalidate）→ Gen12 では `MI_FORCE_WAKEUP`（MFX Power Well Control、Mask Bits 768）→ `MFX_WAIT`（Sync Control）→ `MFX_PIPE_MODE_SELECT`（AVC・Decode・**Short Format**・VLD・Post Deblocking Output）→ `MFX_WAIT` → `MFX_SURFACE_STATE`（出力、PLANAR_420_8、Interleave Chroma、Tile Y、Y offset for U = chroma plane の offset / pitch）→ `MFX_PIPE_BUF_ADDR_STATE`（post deblocking の宛先、intra row store、deblock row store、参照 16 枚の address）→ `MFX_IND_OBJ_BASE_ADDR_STATE`（bitstream の 4 KiB 整列の base）→ `MFX_BSP_BUF_BASE_ADDR_STATE`（BSD/MPC・MPR の row store）→ `MFD_AVC_DPB_STATE`（slot ごとの non-existing・long term・used for reference・frame num）→ `MFD_AVC_PICID_STATE`（slot index、未使用は 0xffff）→ `MFX_AVC_IMG_STATE`（SPS・PPS・picture info から）→ `MFX_QM_STATE` ×2（4x4 intra/inter、zig-zag 順）、transform_8x8 なら ×2（8x8 intra/inter）→ `MFX_AVC_DIRECTMODE_STATE`（参照の MV buffer と POC、書き込み先の MV buffer、現 picture の POC は index 32・33）→ slice ごとに `MFD_AVC_SLICEADDR`（次の slice の位置、最後の slice 以外）と `MFD_AVC_BSD_OBJECT`（3 byte の start code を飛ばした位置と長さ、Last Slice、error concealment の bit）。`MFX_AVC_SLICE_STATE`・`REF_IDX_STATE`・`WEIGHTOFFSET_STATE` は**使わない**（short format では hardware が slice header を読む）。
- genxml の命令の長さと header の既定値（Mesa の `gen120.xml` が import する `gen110.xml` 以下。Gen12 の anv がこの定義で compile しているので Gen12 LP に有効）: 下の表（§6.3）。field の bit 位置は genxml から転記する（§7）。
- 画像の tiling: `anv_formats.c` 1441〜1444「only allow Y-tiling/Tile4 for video decode」（modifier の場合）、`anv_image.c` 245〜249 で video の usage は `ISL_SURF_USAGE_VIDEO_DECODE_BIT`。Tile4 は Gen12.5 以降で、Gen12 LP は **Tile Y**（`MFX_SURFACE_STATE` の Tile Walk = YMAJOR、anv も `TW_YMAJOR`）。

### 1.6 Vulkan の registry の依存

`vk.xml`（1.4.309）: `VK_KHR_video_queue` は `depends="(VK_VERSION_1_1+VK_KHR_synchronization2),VK_VERSION_1_3"`、`VK_KHR_video_decode_queue` は `VK_KHR_video_queue+(VK_KHR_synchronization2,VK_VERSION_1_3)`、`VK_KHR_video_decode_h264` は `VK_KHR_video_decode_queue`、`VK_KHR_synchronization2` は `VK_KHR_get_physical_device_properties2,VK_VERSION_1_1`。libvulkan は 1.0 + properties2 なので、規格どおりには video の拡張を名乗れない（§3.1、H2）。

## 2. 決定の一覧

| ID | 決定 | 理由 | 選ばなかった案 |
| --- | --- | --- | --- |
| D1 | **decode は short format**（hardware が slice header を読む）: `MFX_PIPE_MODE_SELECT` の Decoder Short Format Mode = Short Format Driver Interface、`MFD_AVC_DPB_STATE`・`MFD_AVC_PICID_STATE`・`MFD_AVC_SLICEADDR`・`MFD_AVC_BSD_OBJECT` で slice を渡す | Vulkan Video は slice header を app が解析しない（`StdVideoDecodeH264PictureInfo` は frame_num・POC・idr_pic_id・flags だけで、ref list の修正・重み表は無い）。long format は kernel に Exp-Golomb の slice header parser が要る。ANV も short format | long format（`MFX_AVC_SLICE_STATE`・`REF_IDX_STATE`・`WEIGHTOFFSET_STATE`）: short format が実機で期待どおり動かない時の fallback として §11 に残す |
| D2 | video の API は **i915 の backend だけ**で名乗る。capset の vendor flags に bit 16 `VIDEO_H264` を足し、libvulkan はそれが立った session でだけ拡張と family を出す | QEMU の host は video を出さず（§1.4）、Venus の wire に video の command が有るか不明。実機でしか試験しない（ws.md） | Venus に転送する: 番号が確かめられず、host の ANV も既定で無効 |
| D3 | libvulkan は backend の `queueFlags` から `VK_QUEUE_VIDEO_DECODE_BIT_KHR`（と ENCODE）を、video の拡張を出さない session では**落とす** | 今は濾していないので、将来 host が video の family を出すと拡張の無い family が見える（規格の違反） | そのまま: 害は小さいが規格違反 |
| D4 | 新しい command は **zedBSD 独自の opcode**（`GPU_OP_OWN_FIRST` から 14 個、§4）。UAPI の変更なので承認を待つ（H1） | ws167 の header の約束。Venus の host には送らない（D2） | Venus の番号を推測して使う: 確かめる source が無い |
| D5 | **Vulkan 1.0 の device のまま** video の拡張（と `VK_KHR_synchronization2` の翻訳層）を名乗る。規格の依存（§1.6）に合わない点を README・API-PROVENANCE に「非適合」として書く | apiVersion 1.1 に上げるには 1.1 core の全 command（約 30）が要り、本 WS の目標でない。利用者は自前の app（WS122・WS121）だけ | 1.1 に上げる（別 WS の規模）、sync2 無しで出す（H2 で選べる） |
| D6 | `VK_KHR_synchronization2` は **libvulkan の中の翻訳**で実装する（`vkCmdPipelineBarrier2KHR` → `GPU_OP_CMD_PIPELINE_BARRIER`、`vkQueueSubmit2KHR` → `vkQueueSubmit`、event2・timestamp2 も 1.0 の command へ。stage2/access2 の 64 bit を 1.0 の 32 bit に保守的に写し、video の bit は `ALL_COMMANDS`/`MEMORY_READ|WRITE` へ）。新しい opcode は要らない | i915 の実行器は barrier を無視し submission を同期で終えるので意味を保つ。Venus の host でも 1.0 の command は通る。video の app は `VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR` を使える | sync2 を backend に新しい opcode で送る: 不要 |
| D7 | queue family は **index 1、`VK_QUEUE_VIDEO_DECODE_BIT_KHR` だけ、queue 1 つ**。family 0（graphics）は変えない | VCS0 は RCS0 と別の engine。transfer bit は付けない（VCS では copy を実装しない） | family 0 に VIDEO_DECODE を足す: RCS の batch に MFX の command は書けない |
| D8 | DPB と出力は **coincide**（`VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR`、decode の宛先 = その picture の DPB slot の image）。`SEPARATE_REFERENCE_IMAGES` を立て、DPB は **1 層の image を slot ごと**に（`maxArrayLayers` 1） | MFX の post deblocking の宛先がそのまま参照になる形（ANV と同じ）。配列の DPB は image の layout（Y tile の QPitch）の仕事が増える | distinct（出力の copy が要る）、配列の DPB（後の候補） |
| D9 | 出力・DPB の format は `VK_FORMAT_G8_B8R8_2PLANE_420_UNORM`（NV12）だけ、tiling `OPTIMAL` = **Tile Y**、pitch は 128 B の倍数、plane は 32 行の倍数 | Gen12 LP の MFX の宛先は tiled（§1.5）。linear 出力が MFX で動くかは不明（§11） | linear: 実機の bring-up で実験してよいが設計の既定にしない |
| D10 | direct MV buffer は **session の memory**（`vkGetVideoSessionMemoryRequirementsKHR` の bind index 4〜4+maxDpbSlots-1、slot ごとに `w_mb*h_mb*128` byte、4 KiB 整列）に置く。image には足さない | 実行器の image は libvulkan の blob の中で、private の領域を足せない。session の memory は実装定義で app が bind する | ANV の方法（image の private binding）: 本実行器の memory の形に合わない |
| D11 | i915 に **engine record `I915_ENGINE_VCS0`**（index 2、`I915_ENGINE_COUNT` 3）を足し、session の VCS0 の context は **遅延で**（最初の `vkCreateVideoSessionKHR` の時）hardware context を作る。worker は engine ごとの context record を持つ（render 32、video 8） | compositor と app の全 session に VCS の context を作ると memory（image + 16 KiB ring + timeline page）が無駄 | open で作る（既存の形）: 簡単だが無駄 |
| D12 | 実行は**既存の同期の形**: decode の batch を `drv_i915_worker_run_sync(contexts[VCS0])` で走らせ、終わってから返事。request は RCS と同じ `request.c`（xcs の flush、BSD invalidate）、submission は VCS0 の execlists | 実行器全体が同期（§1.2）。非同期化は ws075-p018 の範囲 | VCS だけ非同期: 1 worker の形を崩す |
| D13 | MFX の command の定義は **`intel/genxml-video.h`**（新、`intel/genxml.h` と同じ書き方: Mesa の genxml の値を出典・SHA 付きで転記、組み立ての論理は新規）。file は gen110/gen90/gen80/gen75.xml の該当の命令 | WS031 と同じ license の扱い（§7） | PRM の PDF から手で起こす: 版の照合ができない |
| D14 | 公開 header: `include/libc/vulkan/vulkan_video.h`（3 拡張 + sync2 の宣言を pinned 1.3.269 の `vulkan_core.h` から `tools/maintain-video.noct` で選ぶ）と `include/libc/vulkan/vk_video/`（Khronos Vulkan-Headers v1.3.269 の `vulkan_video_codecs_common.h`・`vulkan_video_codec_h264std.h`・`vulkan_video_codec_h264std_decode.h`、Apache-2.0）。kernel の実行器も同じ header を include する | guardrail: kernel が include できる libc の header は `libc/vulkan/*` だけ。既存の `vulkan_external.h` と同じ作り方 | Mesa 25.0.7 の `include/vk_video/`（1.4.305 世代）を使う: API の pin（1.3.269）と版がずれる |
| D15 | query（`VK_QUERY_TYPE_RESULT_STATUS_ONLY_KHR`）と inline query は**最初の目標に入れない**（`queryResultStatusSupport = VK_FALSE`、`VK_VIDEO_SESSION_CREATE_INLINE_QUERIES_BIT_KHR` は拒否）。decode の失敗は submission の結果（hang → fail）で表す | 規格で任意。MFX の Pic Status/Error Report は実機で確かめてから（Future） | 最初から status query: 実機無しに決められない |
| D16 | 実機の判定は **frame ごとの hash**（crop 後の NV12 の Y・UV plane の SHA-256）を host の ffmpeg の `-pix_fmt nv12` の raw 出力と比べる。test の stream は host の ffmpeg（libx264）で `testsrc` 等から作り tree に入れない | ws.md の方針。ffmpeg の H.264 decoder は規格の参照として十分（I・P・B の bit 一致は規格が保証） | 実写の stream を commit: license と大きさ |

## 3. API の面（libvulkan が報告する物）

### 3.1 拡張

i915 の backend（capset の vendor flags に bit 16、§4.3）でだけ、`vkEnumerateDeviceExtensionProperties` に次を足す。spec version は p002 で pinned header（1.3.269）の `*_SPEC_VERSION` から取る（Mesa 25.0.7 の 1.4.305 では video_queue 8・video_decode_queue 8・video_decode_h264 9・synchronization2 1。1.3.269 の値は p002 で確かめる）。

| 拡張 | 条件（`vkCreateDevice` の照合、`device.c` 306〜382 の形） | bit（`enum vulkan_device_extension_bits`） |
| --- | --- | --- |
| `VK_KHR_synchronization2` | instance に `VK_KHR_get_physical_device_properties2` | `VULKAN_DEVICE_SYNCHRONIZATION2 = 256` |
| `VK_KHR_video_queue` | `VK_KHR_synchronization2` も enable | `VULKAN_DEVICE_VIDEO_QUEUE = 512` |
| `VK_KHR_video_decode_queue` | `VK_KHR_video_queue` | `VULKAN_DEVICE_VIDEO_DECODE_QUEUE = 1024` |
| `VK_KHR_video_decode_h264` | `VK_KHR_video_decode_queue` | `VULKAN_DEVICE_VIDEO_DECODE_H264 = 2048` |

`vkGetPhysicalDeviceFeatures2KHR` の chain の `VkPhysicalDeviceSynchronization2FeaturesKHR.synchronization2` は TRUE（翻訳層、D6）。`vkCreateDevice` の `pNext` の同 struct は受け取り、TRUE/FALSE どちらでも拒まない。D5 の非適合（apiVersion 1.0 で名乗る）は `userland/desktop/libvulkan/README.md` と `include/libc/vulkan/API-PROVENANCE.md` に書く。

### 3.2 queue family

backend（i915 の実行器）が返す family を 2 つにする（`render/instance.c` の `i915_instance_queue_families`、`array_count > 2` を拒む）:

| index | queueFlags | queueCount | timestampValidBits | minImageTransferGranularity |
| --- | --- | --- | --- | --- |
| 0 | GRAPHICS \| COMPUTE \| TRANSFER（今のまま） | 1 | 0（今のまま） | 1,1,1 |
| 1 | `VK_QUEUE_VIDEO_DECODE_BIT_KHR` | 1 | 0 | 0,0,0（transfer の無い family の規格の値） |

libvulkan の `vkGetPhysicalDeviceQueueFamilyProperties2KHR` は pNext を見て、`VkQueueFamilyVideoPropertiesKHR.videoCodecOperations` に family 1 なら `VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR`、他は 0、`VkQueueFamilyQueryResultStatusPropertiesKHR.queryResultStatusSupport` は FALSE を書く。family ごとの codec operation は新しい op `GPU_OP_GET_PHYSICAL_DEVICE_QUEUE_FAMILY_VIDEO_PROPERTIES`（§4）で backend から 1 回取り、physical device に cache する。`vkGetDeviceQueue(2)` で family 1 の queue を取ると、libvulkan は今と同じく timeline を予約し `GPU_OP_GET_DEVICE_QUEUE2` を送る。実行器はこの command の **family を読んで** queue object に覚える（今は読み飛ばし、§1.2）。family 1 の queue への `vkQueueSubmit` は video の操作だけを含む command buffer を受け、graphics の操作が混ざれば `VK_ERROR_UNKNOWN`（実行器が EINVAL）。family 1 の command pool から取った command buffer に graphics の `vkCmd*` を記録した時は実行器の submit が拒む（libvulkan は規格の valid usage なので検査しない）。

### 3.3 `vkGetPhysicalDeviceVideoCapabilitiesKHR`

受ける profile: `videoCodecOperation = DECODE_H264`、`chromaSubsampling = 420`、`lumaBitDepth = chromaBitDepth = 8`、pNext の `VkVideoDecodeH264ProfileInfoKHR.stdProfileIdc` ∈ {BASELINE 66, MAIN 77, HIGH 100}、`pictureLayout = PROGRESSIVE`。他は規格の error（codec 以外は `VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR`、format は `..._FORMAT_NOT_SUPPORTED_KHR`、interlace は `VK_ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR`、`VkVideoDecodeUsageInfoKHR` は読むだけ）。

| field | 値 | 出典・理由 |
| --- | --- | --- |
| `flags` | `VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR` | D8。`PROTECTED_CONTENT` は無し |
| `minBitstreamBufferOffsetAlignment` | 32 | `MFD_AVC_BSD_OBJECT` の start address は byte 単位（genxml 64:92 offset）、`MFX_IND_OBJ_BASE_ADDR_STATE` の base は 4 KiB 整列にし低 12 bit を start に足す（§6.3）。ANV と同じ |
| `minBitstreamBufferSizeAlignment` | 1 | 同上 |
| `pictureAccessGranularity` / `minCodedExtent` | 16x16 | MB |
| `maxCodedExtent` | 4096x4096 | ANV の値。`MFX_AVC_IMG_STATE` の Frame Width/Height は 8 bit の MB 数（≤ 256 MB = 4096）。実機で 4096 幅は未確認（§11） |
| `maxDpbSlots` | 17 | `MFD_AVC_DPB_STATE`・`PICID_STATE` の 16 枠 + 現 picture |
| `maxActiveReferencePictures` | 16 | `MFX_PIPE_BUF_ADDR_STATE` の Reference Picture 16 |
| `stdHeaderVersion` | `VK_STD_vulkan_video_codec_h264_decode`、1.0.0 | header の値 |
| `VkVideoDecodeCapabilitiesKHR.flags` | `DPB_AND_OUTPUT_COINCIDE` | D8 |
| `VkVideoDecodeH264CapabilitiesKHR.maxLevelIdc` | `STD_VIDEO_H264_LEVEL_IDC_5_1` | ANV の値。実機の性能で下げる可能性（§11） |
| `VkVideoDecodeH264CapabilitiesKHR.fieldOffsetGranularity` | 0,0 | progressive だけ |

### 3.4 format

- `vkGetPhysicalDeviceVideoFormatPropertiesKHR`（`VkPhysicalDeviceVideoFormatInfoKHR.imageUsage` と pNext の `VkVideoProfileListInfoKHR`）: profile の list が §3.3 の profile だけなら 1 件 `{ format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, componentMapping = identity, imageCreateFlags = 0, imageType = 2D, imageTiling = OPTIMAL, imageUsageFlags = imageUsage ∩ (VIDEO_DECODE_DST | VIDEO_DECODE_DPB) }`。他の usage（SAMPLED・TRANSFER_SRC）は最初の目標では**付けない**（実行器に tiled NV12 の sampler・copy が無い。§9 の p007 と ws031-p037 の後）。list が空か他の profile なら `VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR`。
- `vkGetPhysicalDeviceImageFormatProperties2KHR`（`external-properties.c` 93〜）: pNext に `VkVideoProfileListInfoKHR` があれば libvulkan が profile を §3.3 で照合し、NV12・2D・OPTIMAL・usage ⊆ {DST, DPB} なら core の query を backend に送る。backend（`render/instance.c` の image format properties）は NV12 + OPTIMAL + その usage に `maxExtent 4096x4096x1、maxMipLevels 1、maxArrayLayers 1、sampleCounts 1、maxResourceSize = pitch*rows` を返す。NV12 の `vkGetPhysicalDeviceFormatProperties` は `optimalTilingFeatures = 0`（video の feature は FormatFeatureFlags2 だけで、2 系の format query は本 WS で足さない）。
- `vkCreateImage`: NV12・2D・1 level・1 layer・1 sample・OPTIMAL・usage ⊆ {DST, DPB}・pNext に `VkVideoProfileListInfoKHR`（無くても受ける）。layout は §6.4。`vkGetImageSubresourceLayout` は aspect `VK_IMAGE_ASPECT_PLANE_0_BIT`/`PLANE_1_BIT` で plane の offset・rowPitch・size を返す（optimal でも返す。試験の道具が使う、§8.3）。
- `vkCreateImageView`: NV12 の 2D view、format は同じ、aspect `COLOR`。`VkVideoPictureResourceInfoKHR.imageViewBinding` に使う。

### 3.5 session と parameters

- `vkCreateVideoSessionKHR`: `queueFamilyIndex` 1、`flags` 0（`INLINE_QUERIES` と `PROTECTED_CONTENT` は `VK_ERROR_FEATURE_NOT_PRESENT`）、`pVideoProfile` §3.3、`pictureFormat` = `referencePictureFormat` = NV12、`maxCodedExtent` ≤ 4096x4096 かつ 16 の倍数に切り上げ、`maxDpbSlots` ≤ 17、`maxActiveReferencePictures` ≤ 16（0 なら I だけ）、`pStdHeaderVersion` は名前が一致し version ≤ 1.0.0（違えば `VK_ERROR_VIDEO_STD_VERSION_NOT_SUPPORTED_KHR`）。libvulkan は object を作り backend に `GPU_OP_CREATE_VIDEO_SESSION` を送る。backend は session を object 表に入れ、scratch の大きさ（§6.4）を計算して保持。
- `vkGetVideoSessionMemoryRequirementsKHR`: backend に聞く（`GPU_OP_GET_VIDEO_SESSION_MEMORY_REQUIREMENTS`）。bind index 0〜3 は row store 4 本、4〜4+maxDpbSlots-1 は slot ごとの MV buffer（D10）。`memoryTypeBits` は唯一の type。
- `vkBindVideoSessionMemoryKHR`: 全 bind index を 1 度ずつ bind するまで `vkCmdBeginVideoCodingKHR` の submit は失敗（実行器が EINVAL）。bind は `GPU_OP_BIND_VIDEO_SESSION_MEMORY` で `[session][count]{[bindIndex][memory id][memoryOffset][memorySize]}`。
- `vkCreateVideoSessionParametersKHR`: pNext の `VkVideoDecodeH264SessionParametersCreateInfoKHR`（`maxStdSPSCount` ≤ 32、`maxStdPPSCount` ≤ 256、`pParametersAddInfo`）と `videoSessionParametersTemplate`（あれば template の SPS・PPS を写してから add）。`vkUpdateVideoSessionParametersKHR`: `updateSequenceCount` が今の +1 でなければ `VK_ERROR_INVALID_VIDEO_STD_PARAMETERS_KHR`（規格）、既にある id の追加も同 error（update では置き換えない、規格）。SPS・PPS は **libvulkan が手元に持ち**（app の pointer を deep copy: `pOffsetForRefFrame`・`pScalingLists`・VUI は VUI を捨てる）、同時に backend にも全体を送る（実行器が `MFX_AVC_IMG_STATE`・`QM_STATE` を組むのは kernel なので）。`StdVideoH264ScalingLists` は `scaling_list_present_mask`・`use_default_scaling_matrix_mask` と 6+2 本の表をそのまま。
- 破棄: `vkDestroyVideoSessionParametersKHR`・`vkDestroyVideoSessionKHR` は `GPU_OP_DESTROY_*`。session の destroy は bind した memory を解放しない（app の物）。

### 3.6 command buffer の video coding scope

family 1 の pool の command buffer に次を記録する（`commands.c` の `command_record_begin/finish` の形、新 file `video.c`）:

- `vkCmdBeginVideoCodingKHR(pBeginInfo)`: `videoSession`・`videoSessionParameters`（NULL 可、decode で要る）・`referenceSlotCount` と `pReferenceSlots[]`（`slotIndex` -1 は「この slot を無効に」、`pPictureResource` の image view と `codedOffset`・`codedExtent`・`baseArrayLayer`（0 だけ）、pNext の `VkVideoDecodeH264DpbSlotInfoKHR` は Begin では無視）。
- `vkCmdControlVideoCodingKHR(flags)`: `VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR` だけ（他の bit は encode）。session の最初の使用の前に必須（規格）。実行器は DPB の slot 表を空にする。
- `vkCmdDecodeVideoKHR(pDecodeInfo)`: `srcBuffer`・`srcBufferOffset`（32 の倍数）・`srcBufferRange`、`dstPictureResource`、`pSetupReferenceSlot`（NULL 可: 参照にしない picture）、`referenceSlotCount`・`pReferenceSlots[]`（各 pNext に `VkVideoDecodeH264DpbSlotInfoKHR.pStdReferenceInfo`）、pNext の `VkVideoDecodeH264PictureInfoKHR`（`pStdPictureInfo`、`sliceCount`、`pSliceOffsets[]`）。coincide なので `dstPictureResource` と `pSetupReferenceSlot->pPictureResource` は同じ image（違えば実行器が EINVAL）。
- `vkCmdEndVideoCodingKHR`。

記録の形は §4.2。実行器はこれらを操作列に記録し、`vkQueueSubmit` で §6.3 の batch にして VCS0 で走らせる。

### 3.7 synchronization2 の翻訳（D6）

`vkCmdPipelineBarrier2KHR`・`vkCmdSetEvent2KHR`・`vkCmdResetEvent2KHR`・`vkCmdWaitEvents2KHR`・`vkCmdWriteTimestamp2KHR`・`vkQueueSubmit2KHR` の 6 つ。`VkDependencyInfo` の `VkMemoryBarrier2`/`VkBufferMemoryBarrier2`/`VkImageMemoryBarrier2` を 1.0 の barrier に写す: stage2 の下位 32 bit に 1.0 の bit が有ればそれ、無い bit（`VIDEO_DECODE`、`COPY`・`RESOLVE`・`BLIT`・`CLEAR` の個別 bit など）は `ALL_COMMANDS`、`NONE` は `TOP_OF_PIPE`（src）/`BOTTOM_OF_PIPE`（dst）。access2 は同様に `MEMORY_READ`/`MEMORY_WRITE`。`VkSubmitInfo2` の `VkSemaphoreSubmitInfo` は semaphore と stage mask（1.0 へ写す）、`VkCommandBufferSubmitInfo` は command buffer に。timeline semaphore の値（`VkSemaphoreSubmitInfo.value`）は timeline semaphore を出さないので 0 のまま。

## 4. protocol（Kei GPU command protocol の追加）と UAPI の差分

### 4.1 `include/uapi/gpu-op.h` に提案する差分（承認まで当てない、H1）

```diff
--- a/include/uapi/gpu-op.h
+++ b/include/uapi/gpu-op.h
@@ enum gpu_op {
 	GPU_OP_SET_REPLY_STREAM = 178,	/* the stream's replies go to a resource */
 	GPU_OP_SEEK_REPLY_STREAM = 179,	/* moves the reply position */
-	GPU_OP_EXECUTE_STREAMS = 180	/* runs command streams held in resources */
+	GPU_OP_EXECUTE_STREAMS = 180,	/* runs command streams held in resources */
+
+	/*
+	 * zedBSD's own commands (ws083): Vulkan Video, H.264 decode.  Only a
+	 * backend whose capset declares video takes them (the native i915);
+	 * they are never sent to a Venus host.
+	 */
+	GPU_OP_GET_PHYSICAL_DEVICE_VIDEO_CAPABILITIES = GPU_OP_OWN_FIRST + 0,	/* vkGetPhysicalDeviceVideoCapabilitiesKHR */
+	GPU_OP_GET_PHYSICAL_DEVICE_VIDEO_FORMAT_PROPERTIES = GPU_OP_OWN_FIRST + 1,	/* vkGetPhysicalDeviceVideoFormatPropertiesKHR */
+	GPU_OP_GET_PHYSICAL_DEVICE_QUEUE_FAMILY_VIDEO_PROPERTIES = GPU_OP_OWN_FIRST + 2,	/* the codec operations of each queue family (VkQueueFamilyVideoPropertiesKHR) */
+	GPU_OP_CREATE_VIDEO_SESSION = GPU_OP_OWN_FIRST + 3,	/* vkCreateVideoSessionKHR */
+	GPU_OP_DESTROY_VIDEO_SESSION = GPU_OP_OWN_FIRST + 4,	/* vkDestroyVideoSessionKHR */
+	GPU_OP_GET_VIDEO_SESSION_MEMORY_REQUIREMENTS = GPU_OP_OWN_FIRST + 5,	/* vkGetVideoSessionMemoryRequirementsKHR */
+	GPU_OP_BIND_VIDEO_SESSION_MEMORY = GPU_OP_OWN_FIRST + 6,	/* vkBindVideoSessionMemoryKHR */
+	GPU_OP_CREATE_VIDEO_SESSION_PARAMETERS = GPU_OP_OWN_FIRST + 7,	/* vkCreateVideoSessionParametersKHR */
+	GPU_OP_UPDATE_VIDEO_SESSION_PARAMETERS = GPU_OP_OWN_FIRST + 8,	/* vkUpdateVideoSessionParametersKHR */
+	GPU_OP_DESTROY_VIDEO_SESSION_PARAMETERS = GPU_OP_OWN_FIRST + 9,	/* vkDestroyVideoSessionParametersKHR */
+	GPU_OP_CMD_BEGIN_VIDEO_CODING = GPU_OP_OWN_FIRST + 10,	/* vkCmdBeginVideoCodingKHR */
+	GPU_OP_CMD_END_VIDEO_CODING = GPU_OP_OWN_FIRST + 11,	/* vkCmdEndVideoCodingKHR */
+	GPU_OP_CMD_CONTROL_VIDEO_CODING = GPU_OP_OWN_FIRST + 12,	/* vkCmdControlVideoCodingKHR */
+	GPU_OP_CMD_DECODE_VIDEO = GPU_OP_OWN_FIRST + 13	/* vkCmdDecodeVideoKHR */
 };
 
 /* The protocol's version, and the first number of zedBSD's own commands. */
-#define GPU_OP_PROTOCOL_VERSION	1U
+#define GPU_OP_PROTOCOL_VERSION	2U
 #define GPU_OP_OWN_FIRST	0x10000U
+
+/* The last number of zedBSD's own commands so far (ws083). */
+#define GPU_OP_OWN_LAST		(GPU_OP_OWN_FIRST + 13U)
```

- 版を 2 にするのは「版 1 = Venus の番号だけ」を保つため。capset の wire version（byte 0、今 1）は Venus の約束なので**変えない**（libvulkan は `wire_version != 1` を拒む、`context.c` 157）。`GPU_OP_PROTOCOL_VERSION` は header の記録で、libvulkan と実行器は同じ header から build する。
- `GPU_OP_OWN_LAST` は実行器の `dispatch.c` の範囲の分岐（`i915_dispatch_route`）が video の module へ渡すのに使う。
- `include/uapi/gpu.h` など他の UAPI は変えない。capset の中身（§4.3）は libvulkan と i915 の間の約束で `gpu_capset.data` の byte 列にあり、UAPI の header の変更ではないが、ここに書いて一緒に承認を受ける。

### 4.2 record の形（wire）

既存の framing（`[opcode][reply flag][...]`、object は u64 の id、「present」の u64、配列は u64 の数、構造体は `sType u32`・`pNext` 連鎖）に揃える。構造体の encoder/decoder は `maintain-codec.noct` の表に pointer の規則を足して生成する（`codec.c/h`、i915 側は `vulkan-codec.inc`）。pNext は既存と同じく**知っている sType だけ**を書く（`vulkan_encode_image_external` の形）: `VkVideoProfileInfoKHR.pNext` → `VkVideoDecodeH264ProfileInfoKHR`・`VkVideoDecodeUsageInfoKHR`、`VkVideoCapabilitiesKHR.pNext` → `VkVideoDecodeCapabilitiesKHR`・`VkVideoDecodeH264CapabilitiesKHR`（返事）、`VkVideoSessionParametersCreateInfoKHR.pNext` → `VkVideoDecodeH264SessionParametersCreateInfoKHR`（その `pParametersAddInfo`）、`VkVideoSessionParametersUpdateInfoKHR.pNext` → `VkVideoDecodeH264SessionParametersAddInfoKHR`、`VkVideoDecodeInfoKHR.pNext` → `VkVideoDecodeH264PictureInfoKHR`、`VkVideoReferenceSlotInfoKHR.pNext` → `VkVideoDecodeH264DpbSlotInfoKHR`、`VkPhysicalDeviceVideoFormatInfoKHR.pNext` → `VkVideoProfileListInfoKHR`。`StdVideo*` は field を宣言順に（bit field の flags は u32）、`StdVideoH264SequenceParameterSet` の `pOffsetForRefFrame` は `num_ref_frames_in_pic_order_cnt_cycle` 個、`pScalingLists` は present + 本体、`pSequenceParameterSetVui` は**書かない**（present 0）。

| op | 送る物 | 返事 |
| --- | --- | --- |
| VIDEO_CAPABILITIES | `[physical][present][VkVideoProfileInfoKHR+chain][present][VkVideoCapabilitiesKHR の chain の sType 列]` | `[result][present][VkVideoCapabilitiesKHR+chain]` |
| VIDEO_FORMAT_PROPERTIES | `[physical][present][VkPhysicalDeviceVideoFormatInfoKHR+chain][count 要求][present][array count]` | `[result][count][array count]{VkVideoFormatPropertiesKHR}` |
| QUEUE_FAMILY_VIDEO_PROPERTIES | `[physical][count]` | `[present][count]{u32 videoCodecOperations}` |
| CREATE_VIDEO_SESSION | `[device][present][VkVideoSessionCreateInfoKHR+chain][allocator 0][present][id]` | `[result][present][id]` |
| DESTROY_VIDEO_SESSION | `[device][session][allocator 0]` | なし |
| GET_VIDEO_SESSION_MEMORY_REQUIREMENTS | `[device][session][count 要求][array count]` | `[result][count][array count]{VkVideoSessionMemoryRequirementsKHR}` |
| BIND_VIDEO_SESSION_MEMORY | `[device][session][count]{VkBindVideoSessionMemoryInfoKHR}` | `[result]` |
| CREATE/UPDATE/DESTROY_VIDEO_SESSION_PARAMETERS | create: `[device][present][VkVideoSessionParametersCreateInfoKHR+chain][allocator 0][present][id]`、update: `[device][params][present][VkVideoSessionParametersUpdateInfoKHR+chain]`、destroy: `[device][params][allocator 0]` | create・update `[result]`（create は `[present][id]` も）、destroy なし |
| CMD_BEGIN/END/CONTROL_VIDEO_CODING、CMD_DECODE_VIDEO | `[cmdbuf][present][struct+chain]`（End は `VkVideoEndCodingInfoKHR`、Control は `VkVideoCodingControlInfoKHR`） | なし（記録） |

### 4.3 capset

`render/vulkan.c` の `I915_CAPSET_VENDOR_FLAGS` を `7 | 16`（`I915_CAPSET_VENDOR_VIDEO_H264 = 16U`）にし、`context.c` の完全一致の比較（172〜195）を「既知の bit の mask の比較」に直す（`vendor_flags & ~KNOWN` が 0 の時だけ解釈し、OPAQUE/STRICT_QUEUE/QUIESCE/HOST_SCANOUT の組は今の判定のまま、bit 16 は `context->video_h264 = VK_TRUE`）。Venus の host の capset は 156 byte で vendor 部が無いので video は立たない。

## 5. libvulkan の構造

| 物 | 変更 |
| --- | --- |
| `include/libc/vulkan/vulkan_video.h`（新） | `tools/maintain-video.noct` が pinned 1.3.269 の `vulkan_core.h`（`API-PROVENANCE.md` の SHA の file）から `VK_KHR_video_queue`・`VK_KHR_video_decode_queue`・`VK_KHR_video_decode_h264`・`VK_KHR_synchronization2` の宣言（§1.6 の 26 struct・13 command・11 enum と sync2 の struct・6 command・flags64）を選ぶ。`vulkan_external.h` と同じ形。`vulkan.h` が include する。先頭で `vk_video/vulkan_video_codec_h264std_decode.h` を include |
| `include/libc/vulkan/vk_video/`（新、3 file） | Khronos Vulkan-Headers **v1.3.269** の `include/vk_video/` から写す（Apache-2.0、`API-PROVENANCE.md` に tag・SHA を記録。取得は network が要る: p002 の最初の作業、H3）。`VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION` は 1.0.0 |
| `internal.h` | `enum vulkan_object_kind` に `VULKAN_OBJECT_VIDEO_SESSION`・`VULKAN_OBJECT_VIDEO_SESSION_PARAMETERS`、`enum vulkan_device_extension_bits` に §3.1 の 4 bit、`struct vulkan_context` に `VkBool32 video_h264`、`struct VkPhysicalDevice_T` に `uint32_t *family_codec_ops` |
| `video.c`（新） | §3.3〜3.6 の 13 entry point と session/parameters の object（SPS 最大 32・PPS 最大 256 の表、`updateSequenceCount`）、scaling list の deep copy。record の形は §4.2 |
| `sync2.c`（新） | §3.7 の 6 entry point（翻訳） |
| `instance.c` | 拡張の列挙（`video_h264` の時だけ 4 件）、`physical_load_queues` の後に codec ops の取得（D3 の bit の濾し） |
| `device.c` | `vkCreateDevice` の照合に 4 拡張と依存 |
| `external-properties.c` | `vkGetPhysicalDeviceQueueFamilyProperties2KHR` の pNext（§3.2）、`vkGetPhysicalDeviceFeatures2KHR` の sync2 の struct、`vkGetPhysicalDeviceImageFormatProperties2KHR` の `VkVideoProfileListInfoKHR`（§3.4） |
| `codec.c/h`・`commands-generated.inc`・`dispatch-table.inc`・`api-commands.tsv` | 生成し直し（`maintain-codec.noct` の規則の表に video の pointer、`maintain-dispatch.noct` の件数 173 → 173 + 13 + 6 = 192、extension の bit の対応に 4 拡張） |
| `Makefile` | `video.c`・`sync2.c` を `LIBVULKAN_SOURCES` に。ELF の export の数の検査（README「155 exports」の checker の所在は p002 で確かめる）を更新 |
| `README.md`・`API-PROVENANCE.md` | 非適合（D5）、header の来歴、vk_video の license |

object の寿命: session・parameters は device の子（`vulkan_object_alloc(... &owner->object ...)`）、`vkDestroyDevice` で残った物は `vulkan_object_destroy_remote` の既存の経路。command buffer の video の記録は `recording` の byte 列で、pointer を借りない（`command_record_finish` の後に app の struct を参照しない）。

## 6. i915 の側

### 6.1 engine record と VCS0 の context・submission

| 物 | 変更（file） |
| --- | --- |
| engine record | `request-queue.h`: `I915_ENGINE_VCS0 2U`、`I915_ENGINE_COUNT 3U`、`I915_CLASS_VIDEO I915_VIDEO_DECODE_CLASS`。`device.c` の record の初期化（`device->engines[]` を埋める所）に VCS0 を足す。GT に VCS0 が無い（fuse で消えた）device では record の `initialized = 0` のままにし、実行器は capset の video の bit を立てず family 1 を出さない |
| session の context | `session.c` 161〜170 の loop は record ごとに `drv_i915_worker_context_create` を呼ぶ。VCS0 は **record だけ**（`created = 1`、hardware context なし）にし、実行器が最初の `vkCreateVideoSessionKHR` で `drv_i915_worker_context_attach(device, &session->contexts[I915_ENGINE_VCS0])`（新）を呼んで hardware context を作る（D11）。close は今の `drv_i915_worker_context_destroy` が VCS0 の record も解放する |
| worker | `worker.c`: `struct i915_worker` に `video_index`（GT の engine set の VCS0 の index、無ければ -1）と `video_contexts[I915_WORKER_VIDEO_CONTEXTS = 8]`。`i915_worker_find` は engine の index で表を選ぶ。`i915_worker_run` の「RCS0 以外は ENOTSUP」（1383〜1388）を「record の engine に hardware context があれば走る」に変え、`drv_i915_execlists_submit` に **その engine** の `ge`・`el` を渡す（今は `render_index` 固定、1440〜1443）。LRC の alloc は `drv_i915_lrc_alloc(&record->ce, &gt.engines.ge[video_index], ...)` で、`context.c` が class で xcs の offsets を選ぶ（§1.3）。ring は 16 KiB（RCS と同じ） |
| request | `request.c` はそのまま: class が VIDEO_DECODE なら xcs の flush と `MI_INVALIDATE_BSD`（既存）。breadcrumb は xcs の形（306〜315 で class 分岐済み） |
| batch の開始 | `i915_worker_emit`（1474〜1510）の `MI_BATCH_BUFFER_START_GEN8 | MI_BATCH_NON_SECURE_I965` は VCS でも同じ MI 命令。初期 breadcrumb の `MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT` も同じ |
| 完了の待ち | `i915_worker_wait` は engine の割込みの sequence（`irq.c` 266〜330）で起き、CSB は engine ごとの `el` を処理する。VCS0 の user interrupt と context switch は既に enable・dispatch されている（§1.3）。待ちの対象の `ge`/`el` を engine で選ぶ |
| 割込み | `irq.c` は**変えない**（VCS の class は enable 済み、identity は class/instance で引ける） |
| forcewake・電源 | service は 5 domain を持ち続ける（§1.3）ので VCS の register の書き込み（`RING_EXECLIST_SQ_CONTENTS(0x1c0000+0x510)` は VDBOX0 の範囲）は held で通る。media の power gating は有効なので、batch の先頭で `MI_FORCE_WAKEUP`（MFX Power Well Control = 1、Mask Bits = 1<<9 の 768 の形は genxml: bit 41 と mask bit 48:63）と `MFX_WAIT` を書く（ANV の Gen12 と同じ、§6.3）。park（ws052）の間は VCS も idle |
| firmware | **HuC は要らない**: AVC の VLD decode は MFX の固定機能で、ANV は firmware を一切扱わず decode する（§1.5）。HuC は VDEnc の encode や保護 content の用途。GuC は使わない（execlists、既存）。実機での最終の確認は p005（§11） |
| reset・hang | 今の worker は hang を fail にするだけ（`worker.h` XXX）。VCS でも同じ。engine 単位の reset（`I915_GRDOM_MEDIA`、`device-info.c` 591）は Future |
| display | `display/power.c`・`interrupts.c`・`hotplug.c` ほか `display/` は**触らない**（別の担当が変更中）。video は GT 側だけ |
| device-info | 変えない。VCS0 の `context_size` は class から（`i915_engine_context_size`、`device-info.c` 476〜） |

### 6.2 実行器の video の module（`render/video.c`・`video.h`、新）

- object kind: `I915_VK_OBJ_VIDEO_SESSION`・`I915_VK_OBJ_VIDEO_SESSION_PARAMETERS`（`internal.h` の enum の末尾、`I915_VK_OBJ_KIND_COUNT` の前）。`dispatch.c` の `i915_dispatch_route` に `GPU_OP_OWN_FIRST..GPU_OP_OWN_LAST` → video を足す。
- `struct i915_video_session`: profile、`max_coded`（16 の倍数）、`max_dpb_slots`、`max_refs`、bind の表（index → `{memory, offset, size}`、`bound_mask`）、scratch の大きさ（§6.4）、DPB の slot 表 `slots[17]`（`{image, dmv_bind_index, frame_num, poc[2], long_term, non_existing, active}`）、`reset_done`、親の session、VCS0 の context の有無。
- `struct i915_video_params`: SPS 32・PPS 256 の表（`StdVideoH264SequenceParameterSet`・`StdVideoH264PictureParameterSet` を値で、`pScalingLists` の本体を一緒に持ち、`pOffsetForRefFrame` は 255 個まで、VUI は持たない）、`update_sequence`。
- 記録: `command.c` の操作列の型（`enum i915_gfx_op`）に `VIDEO_BEGIN`・`VIDEO_CONTROL`・`VIDEO_DECODE`・`VIDEO_END` を足す。操作は decode 済みの値（session・params の pointer、slot の配列、`StdVideoDecodeH264PictureInfo`、slice offset の配列（上限 `I915_VIDEO_MAX_SLICES = 256`、超えれば EINVAL））を持つ。`vkQueueSubmit` は queue の family を見て、family 1 なら `drv_i915_video_submit`（新）: VCS0 の batch object（session の batch pool から）に §6.3 を書き、`drv_i915_worker_run_sync(contexts[I915_ENGINE_VCS0], batch_va)`。family 0 の submit に video の操作があれば EINVAL、family 1 に graphics の操作があれば EINVAL。
- batch の書き込みは `render/batch.c` の `drv_i915_batch_emit`（溢れを数えて拒む）。batch の大きさの上限: 固定部 ≈ 5+1+1+5+1+6+65+26+10+27+10+21+18*4+71 = 320 dword、slice ごとに 4+7 = 11 dword、`MI_FLUSH_DW` 5、`MI_BATCH_BUFFER_END` 1。256 slice でも 4 KiB に収まるが batch pool の object の大きさに合わせて見積もる。

### 6.3 MFX AVC VLD の command 列と `StdVideo*` からの写像

命令の header（Command Type 3 << 29 | Pipeline 2（media）<< 27 | Media Command Opcode << 24 | SubOpcode A << 21 | SubOpcode B << 16 | DWord Length = 総 dword − 2）と長さは genxml の既定値（§1.5）。`intel/genxml-video.h` に `GEN12_CMD_MFX_*`（header の上位 16 bit）と `GEN12_MFX_*_DWORDS`、field の shift・mask を転記する。

| 順 | 命令 | 長さ（dword） | Opcode/SubA/SubB | 主な field と出どころ |
| --- | --- | --- | --- | --- |
| 1 | `MI_FLUSH_DW` | 5 | MI 0x26 | Video Pipeline Cache Invalidate（bit 7）。batch の先頭 |
| 2 | `MI_FORCE_WAKEUP` | 2 | MI 0x1d | dword 1: MFX Power Well Control = 1（bit 41、dword 1 の bit 9）、HEVC Power Well Control = 0（bit 40）、Mask Bits（48:63、dword 1 の 16:31）= 0x300（bit 40・41 を書く印。ANV の 768 と同じ値） |
| 3 | `MFX_WAIT` | 1 | Command Type 3（29:31）、Command Subtype（27:28、genxml に既定値が無い。PRM の MFX_WAIT の header で確かめる、見込み 1）、SubOpcode 0（16:26）、MFX Sync Control Flag bit 8 | 2 回（PIPE_MODE_SELECT の前後）。U14 |
| 4 | `MFX_PIPE_MODE_SELECT` | 5 | 0/0/0 | Standard Select AVC(2)、Codec Select Decode(0)、Decoder Short Format Mode = Short(0)、Decoder Mode Select VLD(0)、Post Deblocking Output Enable 1、Pre Deblocking 0、Stream-Out 0、Pic Error/Status Report 0（D15） |
| 5 | `MFX_SURFACE_STATE` | 6 | 0/0/1 | Surface ID 0（decode の宛先）、Width/Height = coded extent −1（dword 2 の 68:81・82:95）、Tile Walk YMAJOR、Tiled Surface 1、Surface Pitch = pitch −1、Interleave Chroma 1、Surface Format PLANAR_420_8(4)、Y Offset for U(Cb) = Y plane の行数（§6.4）。X offset 0 |
| 6 | `MFX_PIPE_BUF_ADDR_STATE` | 65 | 0/0/2 | Post Deblocking Destination = 宛先 image の GPU address、Intra Row Store Scratch = bind 0、Deblocking Filter Row Store = bind 1、Reference Picture[0..15] = `pReferenceSlots[i]` の image（slot の順でなく **参照の並び順 i**、D1 の PICID が slot へ写す）、各 Attributes の MOCS = `GEN12_MOCS(I915_MOCS_UNCACHED_INDEX)`（実行器の surface と同じ）。他の address は 0 |
| 7 | `MFX_IND_OBJ_BASE_ADDR_STATE` | 26 | 0/0/3 | MFX Indirect Bitstream Object Address = `srcBuffer` の address + (`srcBufferOffset` & ~4095)、Upper Bound = buffer の末尾（bind の範囲の終わり、4 KiB 切り上げ）。他は 0 |
| 8 | `MFX_BSP_BUF_BASE_ADDR_STATE` | 10 | 0/0/4 | BSD/MPC Row Store = bind 2、MPR Row Store = bind 3、Bitplane 0 |
| 9 | `MFD_AVC_DPB_STATE` | 27 | 1/1/6 | i 番目の参照（`pReferenceSlots[i]`、`StdVideoDecodeH264ReferenceInfo`）: Non-Existing Frame[i] = `flags.is_non_existing`、Long Term Frame[i] = `flags.used_for_long_term_reference`、Used for Reference[i] = top/bottom の flag が共に 0 なら 3、else `top | bottom<<1`、LTST Frame Number List[i] = `FrameNum`。View ID・View Order は 0（MVC なし） |
| 10 | `MFD_AVC_PICID_STATE` | 10 | 1/1/5 | PictureID Remapping Disable 0、Picture ID[i] = `pReferenceSlots[i].slotIndex`、残りは 0xffff |
| 11 | `MFX_AVC_IMG_STATE` | 21 | 1/0/0 | Frame Width = `sps.pic_width_in_mbs_minus1`、Frame Height = `pic_height_in_map_units_minus1`（frame_mbs_only なら そのまま、でなければ ×2 −1）、Frame Size = 幅×高さ（MB）、Image Structure Frame(0)、Weighted BiPrediction IDC = `pps.weighted_bipred_idc`、Weighted Prediction Enable = `pps.flags.weighted_pred_flag`、First/Second Chroma QP Offset = `pps.chroma_qp_index_offset`/`second_...`、Field Picture 0、MBAFF = `sps.flags.mb_adaptive_frame_field_flag && !field_pic`、Frame MB Only = `sps.flags.frame_mbs_only_flag`、8x8 IDCT Transform Mode = `pps.flags.transform_8x8_mode_flag`、Direct 8x8 Inference = `sps.flags.direct_8x8_inference_flag`、Constrained Intra Prediction = `pps.flags.constrained_intra_pred_flag`、Non-Reference Picture = `!pic.flags.is_reference`、Entropy Coding Sync Enable = `pps.flags.entropy_coding_mode_flag`（CABAC）、Chroma Format IDC = `sps.chroma_format_idc`（1）、Trellis Quantization Chroma Disable 1、Number of Reference Frames = `referenceSlotCount`、Number of Active Reference Pictures from L0/L1 = `pps.num_ref_idx_l{0,1}_default_active_minus1 + 1`、Initial QP Value = `pps.pic_init_qp_minus26`、Pic Order Present = `pps.flags.bottom_field_pic_order_in_frame_present_flag`、Delta Pic Order Always Zero = `sps.flags.delta_pic_order_always_zero_flag`、Pic Order Count Type = `sps.pic_order_cnt_type`、Deblocking Filter Control Present = `pps.flags.deblocking_filter_control_present_flag`、Redundant Pic Count Present = `pps.flags.redundant_pic_cnt_present_flag`、Log2 Max Frame Number = `sps.log2_max_frame_num_minus4`、Log2 Max Pic Order Count LSB = `sps.log2_max_pic_order_cnt_lsb_minus4`、Current Picture Frame Number = `pic.frame_num`。他は 0 |
| 12 | `MFX_QM_STATE` ×2（+2） | 18 | 0/0/7 | AVC 0=4x4 Intra（list 0〜2）、1=4x4 Inter（list 3〜5）、transform_8x8 の時 2=8x8 Intra（list 0）、3=8x8 Inter（list 1）。Forward Quantizer Matrix は 64 byte、**zig-zag 順に並べ替えて**書く（4x4: 3 本×16、8x8: 64）。表は規格の派生（§6.5） |
| 13 | `MFX_AVC_DIRECTMODE_STATE` | 71 | 1/0/2 | Direct MV Buffer[i] = 参照 i の slot の MV buffer（bind 4+slotIndex）、Direct MV Buffer (Write) = `pSetupReferenceSlot->slotIndex` の MV buffer（setup slot が無い picture は session の予備の bind（index 4+maxDpbSlots、1 本）を使う）、POC List[2i], [2i+1] = 参照 i の `PicOrderCnt[0..1]`、POC List[32], [33] = 現 picture の `PicOrderCnt[0..1]` |
| 14 | slice ごと: `MFD_AVC_SLICEADDR`（最後以外）と `MFD_AVC_BSD_OBJECT` | 4 / 7 | 1/1/7、1/1/8 | `base = srcBufferOffset & 4095`、slice s の `start = base + pSliceOffsets[s] + 3`（3 byte の start code `00 00 01` を飛ばす）、`length = end − pSliceOffsets[s] − 3`（`end` は次の slice の offset、最後は `srcBufferRange`）。SLICEADDR は**次の** slice の start と length（hardware が先読みする、ANV と同じ）。BSD_OBJECT の Inline Data: Last Slice（bit 35）、Fix Prev MB Skipped（39）、Intra Prediction Error Control（64）、Intra 8x8/4x4 Prediction Error Concealment Control（65）、I Slice Concealment Mode（95）を 1。他 0 |
| 15 | `MI_FLUSH_DW` + `MI_BATCH_BUFFER_END` | 5 + 1 | | 終わりの flush（Video Pipeline Cache Invalidate 無し、Post-Sync 0）。request の最終 breadcrumb は worker が ring に書く |

start code について: Vulkan Video の `pSliceOffsets` は「各 slice の NAL の先頭（start code を含む）」の offset で、`srcBufferRange` 内の bitstream は Annex B（start code 付き）と規格が定める（`VK_KHR_video_decode_h264`: slice は 3 byte start code で始まる）。4 byte start code（`00 00 00 01`）は app の責任で 3 byte に揃える（規格）。

### 6.4 memory の形と大きさ

| 物 | 置き場所と形 | 大きさ・整列 |
| --- | --- | --- |
| bitstream | app の `VkBuffer`（`srcBuffer`）。実行器の buffer は allocation の範囲（`memory.c` の bind）。GPU address は PPGTT の va | offset 32 の倍数。`MFX_IND_OBJ_BASE_ADDR_STATE` は 4 KiB 整列の base と upper bound（buffer の範囲の終わりを 4 KiB に切り上げ、allocation の外に出るなら allocation の終わり）。bitstream の末尾の読み越しのため app に `srcBufferRange` の後ろに余白を取らせない（upper bound で止める） |
| 出力・DPB の image（NV12、Tile Y） | `VkImage` を `VkDeviceMemory`（blob）に bind。実行器の `struct i915_gfx_image` に `planar`（1）、`tiled`（1）、`chroma_offset`（byte）、`chroma_rows` を足し、`drv_i915_gfx_image_layout` が NV12 を割る | `pitch = align(width, 128)`、Y plane = `pitch * align(height, 32)`、UV plane = `pitch * align(height/2, 32)`、`chroma_offset` = Y plane の byte 数（Tile Y の行の境界）、全体を 4 KiB に切り上げ。`MFX_SURFACE_STATE` の Y Offset for U = `align(height, 32)`。memory requirements の alignment 4096。width・height は 16 の倍数に切り上げて確保（coded extent） |
| Tile Y の中身 | 128 B × 32 行 = 4 KiB の tile、tile の中は 16 B 幅の列（OWord）ごとに 32 行 × 16 B = 512 B（Gen の Tile-Y、swizzle なし）。試験の道具の de-tile が使う（§8.3） | |
| intra row store | session の bind 0 | `width_in_mb * 64`（§1.5）、4 KiB 整列 |
| deblocking filter row store | bind 1 | `width_in_mb * 64 * 4` |
| BSD/MPC row store | bind 2 | `width_in_mb * 64 * 2` |
| MPR row store | bind 3 | `width_in_mb * 64 * 2` |
| direct MV buffer | bind 4 〜 4+maxDpbSlots−1（slot ごと）と bind 4+maxDpbSlots（setup slot の無い picture 用の予備） | `w_mb * h_mb * 128`（§1.5）、4 KiB 整列（ANV は 64 KiB 整列の private binding。MFX の address field は 64 bit の 6 bit 以上の整列（genxml の address は bit 6 から）なので 4 KiB で足りる見込み。§11） |

`width_in_mb = align(maxCodedExtent.width, 16) / 16`、`w_mb`・`h_mb` も maxCodedExtent から（session の寿命の間 固定）。全部で bind index は `5 + maxDpbSlots`（最大 22）。memory type は唯一の type。

### 6.5 session の状態と DPB

- session: `created` → `bound`（全 bind 済み）→ `reset`（`vkCmdControlVideoCodingKHR(RESET)` を含む submit が走った）→ 以後 decode 可。`reset` 前の decode は EINVAL。`vkDestroyVideoSessionKHR` は submit 中の session には来ない（同期実行）。
- DPB の slot 表は **実行器が submit の中で**持つ: Begin の `pReferenceSlots[]` が slot を有効化・無効化（`slotIndex` −1）、Decode の `pSetupReferenceSlot` が現 picture の slot を更新（image・frame_num・POC・long term は次の Decode の `pReferenceSlots[].pNext` の `StdVideoDecodeH264ReferenceInfo` が与えるので表には image と `dmv_bind` だけ要る）。各 Decode の MFX の表（DPB_STATE・PICID・DIRECTMODE）は**その Decode の `pReferenceSlots[]`** から組む（app が規格どおり渡す）。Begin の slot の image と Decode の slot の image が違えば EINVAL。
- scaling list（`MFX_QM_STATE`）: 規格 7.4.2.1.1.1・7.4.2.2 の fall-back 規則 A・B で SPS/PPS の `scaling_list_present_mask`・`use_default_scaling_matrix_mask` から 8 本を導く（`pps.flags.pic_scaling_matrix_present_flag` なら PPS、でなければ SPS、どちらも無ければ Flat_16）。Default_4x4_Intra/Inter・Default_8x8_Intra/Inter（Table 7-3・7-4）と zig-zag（Table 8-12・8-13 の frame scan）は規格の表を zedBSD の `render/video-tables.inc`（新）に書く（規格の値、license の問題なし）。
- 並行性: object 表は session の mutex（ws075-p025）、submit は device の worker の queue で直列。VCS0 と RCS0 の batch は 1 worker が順に走らせるので、decode と描画は**互いに待つ**（性能の限界、§11）。session の `vkDestroy*` は object 表の lock の下で slot 表から外す。
- 失敗: decode の batch が終わらない → worker の timeout → submit が失敗（`VK_ERROR_DEVICE_LOST` を返す既存の形）。bitstream の誤りは hardware の concealment（BSD_OBJECT の bit）で絵が崩れるだけで hang しない想定（§11）。bind されていない index・範囲外の slot・coded extent 超えの image・profile の違いは submit 前に EINVAL（`VK_ERROR_UNKNOWN`）。

### 6.6 既存の枠組みとの関係

- drv_gpu（`src/drivers/gpu/gpu.c`）・cdev・ioctl: **変えない**。全部 `GPU_COMMAND_SUBMIT` の stream の中。
- HAL: 触れない。PCI: 触れない。
- display: 触れない（§6.1）。
- Venus の driver（`src/drivers/gpu/venus/`）: 変えない（capset は host の物のまま、video は立たない）。
- i915 の `tests/`（kernel 内の scenario）: p005 で VCS の scenario（空の batch・fence）を `tests/execution/` に足す（T1 の実機）。

## 7. ライセンスと転記

- zedBSD の新しい code（libvulkan の `video.c`・`sync2.c`、実行器の `render/video.c`、worker の変更、`maintain-video.noct`）は Zlib、`coding-style.md` §13 の header。
- `intel/genxml-video.h`: Mesa の genxml（MIT の repo の data。`intel/genxml.h` の先頭と同じ notice と「transcribed hardware definitions (values only)」の書き方）から MFX の命令の opcode・長さ・field の bit 位置・enum の値を転記。出典と SHA-256: Mesa 25.0.7（Debian 25.0.7-2+deb13u1）`src/intel/genxml/gen110.xml` `6598e556ffedf4fe051c78af3bb08030a39af865c76e2784dba5374646fce35d`、`gen90.xml` `d86fb566b9292280e2d6a385107711fb7ee8008e5c54bb3ba70a2c0343262ddb`、`gen80.xml` `2962677cf69dc947345fd88bd7010427900160eb7a7b076463e6e8d28772439d`、`gen75.xml` `a56886791a06675a2d0b9f578935c8c4fb5d8b172acc15548fc332bc027f12e0`、`gen120.xml` `e2452c7d…`（`MI_FORCE_WAKEUP`・`VD_*`）。`plan/ws031/i915-vk-license-audit.md` の表に行を足す（`audit-mesa-refs.sh`）。命令列の組み立て・写像・DPB の論理は新規に書く（ANV の code は読んで事実を取っただけ、写さない）。
- Khronos の header（`vk_video/*.h`、`vulkan_video.h` の選択元 `vulkan_core.h`）: Apache-2.0（Khronos Group）。`include/libc/vulkan/LICENSE-API` の対象に含め、`API-PROVENANCE.md` に tag・path・SHA を記録（既存の Wayland・external の節と同じ形）。
- H.264 の規格の表（default scaling list・zig-zag）: ITU-T H.264 の値。`video-tables.inc` に出典（規格の表番号）を書く。
- 試験の stream: host の ffmpeg（libx264 は GPL）で**合成の絵**（`testsrc`・`mandelbrot` 等）から作る。bitstream の著作物性は作った側（我々）にあり、encoder の license は出力に及ばない。tree に入れない（D16）。参照の NV12 も入れない。

## 8. 試験計画

### 8.1 host（p002〜p004、p006）

| 試験 | 中身 | 判定 |
| --- | --- | --- |
| libvulkan の video の record（`plan/ws083/tests/host-libvulkan-video.c`） | libvulkan の `objects.c`・`wire.c`・`codec.c`・`video.c`・`sync2.c`・`external-properties.c` を host で compile し、`context.c` の transport を stub（`vulkan_context_execute` を byte の記録と用意した返事に置き換える `libvulkan-transport-stub.inc`、新）で差し替える。capability・format・session・parameters（update の sequence の規則、32 SPS・256 PPS の上限、template の写し）・bind・Begin/Control/Decode/End の record を作る | 記録した byte 列が §4.2 の形（opcode・present・配列の数・`StdVideo*` の field の順）。返事の decode が struct に正しく入る。sync2 の翻訳の stage/access の写像の表（1.0 の bit、ALL_COMMANDS、NONE） |
| 往復（`plan/ws083/tests/host-video-roundtrip.c`） | 上の stub が記録した byte 列を、実行器の host fixture（`plan/ws031/tests/run-vk-host-tests.sh` の `res`/`cmdbuf` と同じく `render/*.c` を 1 file 1 翻訳単位で link、`i915-vk-render-stubs.inc` の kernel の stand-in）に流し込む | 実行器が同じ struct を decode し、session の scratch の大きさ（§6.4）・DPB の slot 表・EINVAL の条件（未 bind・reset 前・slot の image の不一致・family の混在）が期待どおり |
| MFX の command stream の golden（`plan/ws083/tests/host-mfx-avc.c`、p004・p006） | `drv_i915_video_submit` を stand-in の worker（batch を走らせず dword を取る）で呼び、I frame（Baseline 64x64、1 slice）、複数 slice、High（transform_8x8、PPS の scaling list）、P・B（参照 1〜16、long term、non-existing）の入力に対する batch の dword 列を取る | golden の dword 列（`plan/ws083/tests/golden/*.txt`、作り方は fixture 自身の `--write`）と一致。golden は人が §6.3 の表と genxml の bit 位置で**手で検算した**物（最初の 1 本は全 dword に注釈）。命令の header（opcode・長さ）、address の整列、Y Offset for U、PICID の 0xffff、SLICEADDR の先読み、Last Slice |
| NV12 の layout（`host-mfx-avc.c` の一部） | `drv_i915_gfx_image_layout` に NV12 16x16・1920x1080・4096x4096 | pitch・plane の offset・全体の大きさ・`vkGetImageSubresourceLayout` の返事 |
| VCS の立ち上げの host（p003） | 既存の `plan/ws031/tests/`・`src/drivers/gpu/i915/tests/contracts` の形で: engine record 3 つの表、session の open が VCS0 を record だけで作る、`drv_i915_worker_context_attach` が VCS0 の GT engine を選ぶ（engine set の mock）、`i915_worker_run` が engine の `ge`/`el` を選ぶ、request の xcs flush に `MI_INVALIDATE_BSD`（既存の request の fixture があればそこに） | 1 file ごとの compile（kernel と同じ flag、`-Werror`）と fixture の PASS |
| build | `make -j16`（i915 の config、warning 0）、libvulkan・vkdemo・vmunix。GPU の無い config で symbol 0 | |

### 8.2 QEMU（T1、p002 の後に 1 回）

Venus の経路の回帰だけ: boot-test と compositor の起動、`vkdemo`。確かめる点は「video の拡張と family 1 が**出ない**」（test app `vkvideo-probe --list` が family 1 無しと拡張 0 を印字）と、sync2 を名乗る場合はその翻訳が Venus で `vkdemo` を壊さないこと。QEMU で decode はしない（host に video が無い）。

### 8.3 実機（T1、5330、p005・p006）

| 段 | 試験 | 判定 |
| --- | --- | --- |
| VCS の bring-up | kernel の test scenario（`tests/execution/`、`I915_TEST_SET=execution` の形）: VCS0 の context を作り、空の batch（`MI_BATCH_BUFFER_END` だけ）と `MI_STORE_DWORD_IMM` の batch を VCS0 で走らせる | request が終わる（breadcrumb）、store の値が読める、user interrupt で起きた回数、`drv_i915_engine_dump` に error 無し。ログの `verdict` 行（`vkloop-hw.sh` の形） |
| I frame | test app `vkvideo-probe`（`userland/tests/vkvideo-probe/`、新。標準の Vulkan API だけ）: host で作った stream（§8.4）の最初の I frame を decode し、出力 image の memory を map（唯一の memory type は HOST_VISIBLE）、`vkGetImageSubresourceLayout` の plane の offset・pitch で Tile Y を de-tile（§6.4）、crop（SPS の frame_crop）して NV12 の Y・UV の SHA-256 を印字 | host の `ffmpeg -i stream.h264 -f rawvideo -pix_fmt nv12` の frame 0 の SHA-256 と一致（Baseline・Main・High の 3 本、64x64・352x288・1920x1080） |
| P・B と DPB | 同じ app で全 frame を順に decode（app が SPS/PPS/slice header の最小の解析で `StdVideo*` を作る: frame_num・POC・ref の管理は app。この解析は test app の中に書き、libvulkan には入れない） | 全 frame の hash が ffmpeg と一致（30 frame、GOP に B を含む、long term 1 本、複数 slice 1 本） |
| 性能 | 1080p 30 frame の decode の時間と fps（`drv_i915_perf` の RUN の時間、app の壁時計） | 記録（受け入れの数値は p007 で決める） |

全て `flock /tmp/i915-hw.lock` の下、T1 に依頼し、QEMU の証拠と分けて書く。

### 8.4 試験の stream（host で作る、tree に入れない）

`plan/ws083/tests/make-streams.sh`（新、`build/tmp/ws083/` に出力）: `ffmpeg -f lavfi -i testsrc=size=WxH:rate=30 -frames:v N -c:v libx264 -profile:v {baseline,main,high} -g 15 -bf {0,2} -x264-params "slices=4" ... -bsf:v h264_mp4toannexb -f h264 out.h264` と、参照 `ffmpeg -i out.h264 -f rawvideo -pix_fmt nv12 ref.nv12`、frame ごとの SHA-256 を `ref.sha256` に。`vkvideo-probe` の出力と `cmp`。stream・参照・hash は commit しない（script だけ）。

## 9. Phase の分け方と受け入れ

ws.md の表は Q1 が直す（この文書は提案）。

| Phase | 内容 | 受け入れ | 依存 |
| --- | --- | --- | --- |
| ws083-p001 | この設計、敵対的 review、H1〜H4 | review の反映、Q1 の ACK | — |
| ws083-p002 | libvulkan の骨組み: `vulkan_video.h`・`vk_video/`・`maintain-video.noct`、`video.c`・`sync2.c`、拡張の列挙と照合、queue family 2 の濾しと pNext、record の形、codec・dispatch の生成し直し、capset の bit の読み、README・PROVENANCE。host の試験 §8.1 の 1 行目 | host の試験 PASS、libvulkan・vkdemo の build warning 0、export の数の検査、`API-PROVENANCE.md` に SHA、T1 の QEMU の回帰（§8.2）PASS（p002 は T1 の結果を待って cleared） | p001、H1〜H3 |
| ws083-p003 | i915 の VCS の host 部分: engine record VCS0、session の record、worker の engine ごとの context 表と `context_attach`、`i915_worker_run` の engine の選択、実行器の capset bit、family 2、`vkGetDeviceQueue2` の family の記憶、`dispatch.c` の範囲、`render/video.c` の object（session・params・bind・memory requirements・EINVAL の規則）。往復の host 試験 | §8.1 の 2 行目と 5 行目 PASS、vmunix の build warning 0、GPU 無しの config で symbol 0。実機は未実施と明記 | p001、p002（record の形） |
| ws083-p004 | MFX AVC の I frame の command builder（host）: `intel/genxml-video.h`、NV12 Tile Y の image、`drv_i915_video_submit`、§6.3 の 1〜8・11・12（I frame では DPB/PICID/DIRECTMODE も空で書く）・14・15、golden の fixture、license の監査の行 | §8.1 の 3・4 行目 PASS（I frame の golden 2 本以上を手で検算）、build warning 0 | p003 |
| ws083-p005 | 実機: VCS の空の batch と fence（kernel scenario）、I frame の decode の hash 一致（`vkvideo-probe`、3 profile）、HuC 不要の確認、`MI_FORCE_WAKEUP` の要否の確認 | §8.3 の 1・2 行目 PASS（T1）。失敗の解析は gdbstub でなく実機の log と `engine_dump`（T1 に撮らせる） | p004、5330 が戻る |
| ws083-p006 | P・B と DPB: §6.3 の 9・10・13、slot 表、long term・non-existing、複数 slice、High の 8x8・scaling list の fall-back。host の golden と実機の hash | §8.1 の golden（P・B）PASS、§8.3 の 3 行目 PASS（T1） | p005 |
| ws083-p007 | test app の整え（`userland/tests/vkvideo-probe/` を master の Tools に）、性能の測定、利用者（WS122・WS121）への API の案内、SAMPLED/TRANSFER_SRC の usage（ws031-p037 の tiled image と合流）の判断、result status query の要否 | 測定の記録、ws.md の制限の一覧 | p006 |
| ws083-p009 | 全文規約の見直しと回帰（必須の最終確認） | 変えた全 source の `coding-style.md` 全文との照合、`make -j16` warning 0、boot-test、実機の回帰 1 回 | 全 Phase |

p003 と p004 は並行できる（p004 の builder は p003 の worker に依らず host で golden を取る）。実機の Phase（p005）は 1 つにまとめ、T1 の 1 回の依頼で VCS の bring-up と I frame を続けて流す。

## 10. 人の判断

| ID | 問い | 推奨 |
| --- | --- | --- |
| H1 | §4.1 の UAPI の差分（`include/uapi/gpu-op.h` に zedBSD 独自の 14 opcode、`GPU_OP_PROTOCOL_VERSION` 2、`GPU_OP_OWN_LAST`）と capset の vendor flags の bit 16 の承認 | 承認（番号は `GPU_OP_OWN_FIRST` から、ws167 の約束どおり） |
| H2 | Vulkan 1.0 の device のまま video の 3 拡張を名乗る（規格は 1.1 + synchronization2 を要求、§1.6）。(a) `VK_KHR_synchronization2` を libvulkan の翻訳層（D6、6 command）で足し、apiVersion 1.0 の点だけ「非適合」と記録する、(b) sync2 も足さず非適合を 2 点記録する、(c) apiVersion を 1.1 に上げる（別 WS の規模） | (a)。利用者は自前の app（WS122・WS121）で、video の stage bit を使える |
| H3 | Khronos Vulkan-Headers v1.3.269 の `vk_video/` 3 file（Apache-2.0）を `include/libc/vulkan/vk_video/` に入れ、`API-PROVENANCE.md`・`LICENSE-API` に記録する（network で取得して SHA を記録、Wayland・external の header と同じ扱い） | 可 |
| H4 | Mesa の genxml（MIT の data）から MFX の命令の値を `intel/genxml-video.h` に転記する（WS031 の `genxml.h` と同じ方針の確認。code は写さない） | 可（既存の方針の範囲） |
| H5 | 試験の stream を host の ffmpeg（libx264、GPL）で合成の絵から作り、tree には script だけ入れる（stream・参照・hash は `build/tmp/`） | 可 |

H1 以外は既存の方針の確認で、他に新しい判断は無い。

## 11. リスクと確かめていない事

| | 内容 | 影響・備え |
| --- | --- | --- |
| U1 | short format（D1）の `MFD_AVC_DPB_STATE`・`PICID`・`SLICEADDR` の意味は genxml と ANV の事実から。実機で動くかは p005 まで不明 | 動かなければ long format（slice header の parser を kernel に）へ。設計の表 §6.3 を差し替える |
| U2 | HuC 不要（§6.1）は ANV が firmware を扱わないことからの推論。Linux の i915 が ADL-P で HuC を既定で load している環境で ANV が試験されているので、HuC が「有るから動く」可能性は否定できない | p005 で確かめる。要るなら `userland/firmware/` の HuC の package と `firmware.c` の load（GuC 経由の認証が要り、大きい。Future の候補） |
| U3 | Gen12 LP の MFX が linear の宛先を受けるか（D9 は Tile Y） | Tile Y で進める。実験は p005 の余裕で |
| U4 | `MI_FORCE_WAKEUP`（MFX power well）が、service が forcewake を持ち続ける本 driver でも要るか | ANV と同じく書く（害なし）。p005 で外して試す必要はない |
| U5 | direct MV buffer の整列 4 KiB（ANV は 64 KiB） | genxml の address field は bit 6 以上。実機で崩れれば 64 KiB に |
| U6 | `maxCodedExtent` 4096x4096・`maxLevelIdc` 5.1 は ANV の値。実機の性能と大きい frame の動作は未測定 | p007 で測り、値を下げる判断 |
| U7 | Venus の wire に video の command が有るか（virglrenderer の source が手元に無い） | D2 で送らないので影響なし。将来 QEMU で decode するなら別途 |
| U8 | 1 worker の同期実行で decode と描画が互いに待つ（30 fps の 1080p で 1 frame 数 ms の見込み、未測定） | p007 で測る。非同期化は ws075-p018 と合わせる |
| U9 | hang の回復が無い（既存の限界）。bitstream の誤りで MFX が止まる場合 | request の timeout → DEVICE_LOST。engine reset（`GRDOM_MEDIA`）は Future |
| U10 | H.264 の `pSliceOffsets` が 4 byte の start code を指す app | 規格で 3 byte。app（test app・WS122）の責任、README に書く |
| U11 | spec version の値（1.3.269 の `VK_KHR_VIDEO_*_SPEC_VERSION`）は pinned header から取る（手元の 1.4.305/1.4.309 は 8・8・9） | p002 で確かめる |
| U12 | zig-zag と default scaling list の表の転記の誤り | golden の手の検算と、実機の High profile の hash |
| U13 | libvulkan の ELF の export の数の checker の所在（README「155 exports」） | p002 で探して更新 |
| U14 | `MFX_WAIT` の header の Command Subtype（27:28）は genxml に既定値が無い（他の MFX 命令は Pipeline 2 が入る位置） | p004 で Intel の公開 PRM（Gen12 Command Reference、MFX_WAIT）の dword 0 の値で決め、`genxml-video.h` に出典を書く。golden に入る |

## 12. 敵対的レビュー（自己、設計の時点）

| 指摘 | 扱い |
| --- | --- |
| A1 video の拡張は規格上 1.1 + sync2 が要るのに 1.0 で名乗る | H2 に出した。推奨は sync2 の翻訳を足し、apiVersion の点だけ非適合として記録 |
| A2 Venus の host が将来 video の family を出すと拡張の無い family が見える | D3: `video_h264` の無い session で VIDEO の bit を落とす |
| A3 `vkGetDeviceQueue2` を実行器が読み飛ばすので family 1 の queue と family 0 の queue を区別できない | §3.2: family を読んで queue object に覚え、submit で照合する。libvulkan の `device.c` 476〜496 は family を送っている（`[family][index]`） |
| A4 session の memory を bind する前の `vkCmdBeginVideoCodingKHR` の記録は規格では app の責任だが、実行器は何で守るか | submit で `bound_mask` を照合して EINVAL（§6.5） |
| A5 coincide（D8）では `dstPictureResource` と setup slot の image が同じだが、参照にしない picture（`pSetupReferenceSlot == NULL`）は宛先の image が DPB に無い。その MV buffer は？ | 予備の MV buffer（bind 4+maxDpbSlots、§6.4）。宛先は `dstPictureResource` の image |
| A6 `MFX_PIPE_BUF_ADDR_STATE` の Reference Picture の並びと `MFD_AVC_PICID_STATE` の対応が逆だと参照が狂う | §6.3: 両方とも **参照の並び i** で書き、PICID[i] = slotIndex（ANV の形と同じ）。golden で固定 |
| A7 試験の readback に実行器の tiled copy が無い | §8.3: memory は HOST_VISIBLE、`vkGetImageSubresourceLayout` の plane の情報と Tile Y の de-tile を test app に（実装の知識を test app が持つことは記録）。SAMPLED/TRANSFER_SRC は p007 と ws031-p037 |
| A8 1 MiB で記録を途中で流す `command_record_finish` の形で、Begin〜End の record が別の submit に割れないか | 流すのは「decode 済みの prefix を実行器に渡す」だけで GPU は走らない（既存の形。`command_record_flush` は `reply_capacity 0` で decode 完了を待つ）。実行器の記録は command buffer 単位の操作列なので割れない |
| A9 worker の context 表が engine ごとに分かれると `drv_i915_worker_context_destroy` の検索が壊れる | `i915_worker_find` を engine の index で表を選ぶ形に（§6.1）。host の試験（p003）で open/close を回す |
| A10 slot 数 17 と `maxActiveReferencePictures` 16 の組で、`referenceSlotCount` が 16 を超える入力 | submit で EINVAL（規格の上限） |
| A11 `MFX_AVC_IMG_STATE` の Number of Reference Frames に `referenceSlotCount` を入れると、non-existing の frame を含む | ANV と同じ。規格の `num_ref_frames` ではなく表の長さ。実機の P/B の hash で確かめる |
| A12 `vkUpdateVideoSessionParametersKHR` の deep copy を libvulkan と実行器の両方が持つ二重 | 実行器が真実（MFX の表を組む）、libvulkan は規格の error（sequence・重複）を返すための写し。二重は受け入れる（libvulkan は backend に error を返させる経路が無い） |
| A13 display の file を触らない約束と、`device.c` の engine record の初期化を触ること | `device.c` は GT 側。display の担当の変更範囲（`display/`）とは別 file。merge は Q1 |
| A14 `GPU_OP_PROTOCOL_VERSION` を 2 にすると何かが壊れないか | この macro の利用箇所は無い（ws167-p002 の header の記録だけ）。p002 で `grep` して確かめる |

## 13. 範囲外

encode、H.265・AV1、interlace（field・MBAFF）、配列の DPB、保護 content、result status query と inline query、decode 出力の sampler・copy（p007 で判断）、VCS2 の利用、engine reset、非同期の実行、Venus での video。


## 13. design-reviewer の review（2026-10-07、未反映。次の session で §2〜§10 に織り込む）

結果: blocking 4・should-fix 17・minor 6+。このままでは承認しない。

- B1 Begin の DPB slot の意味が逆: `slotIndex = -1` は「slot 無しで bind」（setup picture）、slot を無効にするのは slot index と `pPictureResource = NULL`（vk.xml:7439 optional）。§3.6・§6.5 を spec から書き直し、§4.2 で NULL を符号化、host 試験（-1 の setup、NULL の無効化、Control RESET）。
- B2 scaling list の並べ替えが逆: `StdVideoH264ScalingLists` は scan 順、hardware は raster 順（ANV `genX_cmd_video.c` 1155–1182 `Forward[m*16 + zscan[q]] = list[q]`）。非対称の cqm の試験 stream を足す。
- B3 kernel が app の SPS の値を検べずに MFX の command を作る: SPS の MB 幅・高さ ≤ session の maxCodedExtent と出力・参照 image の extent、総 MB 数、slice の offset が srcBufferRange の中で増える、を batch の前に検べる（WS121 は信頼できない stream を流す）。
- B4 capset の vendor bit が曖昧（D2 は bit 16、§4.3 は値 16 = bit 4）。Venus の host（zedBSD の virglrenderer の fork、`src/drivers/gpu/venus/transport.c` 35–39・1860–1893、libvulkan `context.c` 27–32・165–190、patch は plan/ws014）も同じ magic `0x5a424453` で flags 3・7・15 の 168 byte を出す。「host には vendor 部分が無い」は誤り。flag の空間を fork と共有し、exact match の方針（context.c 165–168）を緩めることになる → 人の判断 HD1。
- should-fix の主なもの: S1 engine の record は `i915.c` 143–160（`drv_i915_publish`、index 2 が COPY になる）、`reset.c`・`job.c`・`command.c` の `I915_ENGINE_COUNT`、queue の id が捨てられる（`command.c` ~3420）・family を保たない（`render/instance.c` ~870）。S2 family 1 でも barrier・event・ExecuteCommands・query reset を受ける。S3 EINVAL は `VK_ERROR_INITIALIZATION_FAILED`（command.c 316–333）で vkQueueSubmit には不正。S4 NV12 と plane の aspect は `VK_KHR_sampler_ycbcr_conversion`（1.1）が要る、H2 を全部の非適合で言い直す。S5 NV12 の OPTIMAL は `VIDEO_DECODE_OUTPUT/DPB` の format feature を出す（vulkan_core.h 2348–2349、ANV anv_formats.c 619–620）、未対応の usage は `VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR`。S6 VCS の hang の封じ込め（timeout 後は video を死なせる、hardware が持つ context を解放しない、engine reset は HD2）。S7 Frame Size は 16 bit（4096x4096 の 65536 MB は入らない、level 5.1 の 36864 で上限）。S8 golden が自己参照（genxml の XML を読む host の decoder か、5330 の Linux の ANV と比べる = HD3）。S9 x264 では長期参照・frame_num の gap・非平坦な CQM が出ない、ffmpeg の `-fps_mode passthrough` と POC 順、4 byte の start code、試験 stream は AGENTS.md の image の規則と合わない（HD4）。S10 `vkvideo-probe` を作る Phase が無い、OPTIMAL の `vkGetImageSubresourceLayout` は spec 違反（private の契約として記録）。S11 Phase の依存の矛盾、capset bit は p005 まで build/boot の option の後ろに。S12 `MFX_WAIT` の subtype は gen75.xml:2019 に既定 1（U14 は誤り）。S13 batch の大きさ（固定 322 dword + slice 256 × 11 ≈ 12.3 KiB、1 MiB の batch と溢れた時の返り値）。S14 `maintain-video.noct` の入力（pinned の vulkan_core.h）も disk に無い、Mesa の `include/vk_video`（1.4.305）が代わりの候補。S15 使わない参照の address を 0 にしない（dummy の surface）。S16 parameters の kernel memory（SPS 32・PPS 256 × scaling list で約 150 KB）を必要な時に確保。S17 遅延の VCS context の排他・解放・`i915_worker_find` の両表。
- minor: 行番号のずれ（worker.c 582–586 → 598–605 など、`drv_i915_worker_run_sync` は device が最初の引数）、gpu-op.h の hunk の順と header の version の注、`GPU_OP_OWN_LAST` は executor に、差分は `plan/ws083/proposed/` に、ScalingLists は 4x4 6 と 8x8 6、V の Y Offset、`.inc` の名の規則、M8 5330 の dmesg に vcs0（`plan/uat/2026-10-05pm/dmesg.txt:68`）と毎 boot の VCS0 の execlists の実績（`defaults.c` 61–135）を引く。
- 正しいと確かめられた点: short format（ANV `genX_cmd_video.c` 910–918）、§6.3 の command の長さ・opcode、genxml の SHA、row store・MV の大きさ、Gen12 LP の Tile Y、既存の forcewake・AUX・TLB・xcs flush・VCS の割込み・execlists。
- 追加の人の判断: HD1 capset の bit の予約（fork）と exact match を緩めること、HD2 信頼できない動画を reset 無しの VCS に流す危険の受け入れか reset を先に、HD3 5330 の Linux（chaos）で ANV と比べる許可、HD4 試験 stream の方針（ITU-T の conformance か小さな合成 stream を tree に）、HD5 fg019 に表示できる出力（SAMPLED・TRANSFER_SRC）が要るか、HD6 H2 を全部の非適合で。

### 再開の情報（2026-10-07 ラップアップ）

- 済み: 設計の第 1 版（§1〜§12、kernel-and-driver-designer）、design-reviewer の review（上）。
- 次の 1 手: §13 の B1〜B4・S1〜S17 を §2〜§10 に織り込み第 2 版にし、HD1〜HD6 と H1〜H5 を Q1 経由でユーザーに出す。UAPI の差分は `plan/ws083/proposed/` に置く（当てない）。
- 未 commit の物: 無し（この commit で全部）。
- 待っている物: 人の判断（H1〜H5、HD1〜HD6）。実装は判断の後。
