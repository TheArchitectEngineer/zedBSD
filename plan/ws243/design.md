<!-- awesome-plan project=zedbsd record=ws243-design -->

# WS243 の設計: nvrtx の NVDEC で H.264 を decode（Vulkan Video）

Parent: [WS243](ws.md)、Phase [p001](phase001/phase.md)
状態: 案（2026-10-29 P6）。実機・QEMU の観測は無い。値は全て source を読んだだけの物（下の「出典」）。

## 0. 要約

**本線は GPU の道**（SAMPLED の NV12 を compositor・app が GPU で使う、p006 案）。この文書の readback の道（下）は正しさを先に確かめるための一時の物で、guardrail「画像の処理は GPU で」からの一時の例外として [ws.md](ws.md) に記録した（2026-10-29 Q1 の指示）。

- nvrtx の GPU device に **NVDEC の channel** を 1 本足す。GR の work channel と**同じ address の空間**（`NVRTX_HANDLE_WORK_SPACE`）に作るので、
  app の memory は GR と同じ GPU の address のまま NVDEC から見える。firmware は GSP-RM が持つので driver は load しない。
- executor（`src/drivers/gpu/nvrtx/vk/`）に Vulkan Video の object（capability・session・parameters）と 4 つの記録の命令を足す。
  wire は libvulkan の `video.c`（i915・Pi と同じ）で、libvulkan・libmedia は変えない。
- decode は**同期**: executor が decode の記録に来たら、それまでの GR の push を流して終わりを待ち、NVDEC の push を出して終わりを待つ。
- 出力と DPB は **NV12 の block linear（GOB 2 段）**、DPB と出力は同じ image（COINCIDE）。
  readback の `vkCmdCopyImageToBuffer`（PLANE_0・PLANE_1）は **copy engine（GRCE）が GPU で de-tile** する（work channel の subchannel 4 に copy の class を置く）。CPU は画素を触らない（guardrail「画像の処理は GPU で」）。
- 範囲は progressive の H.264（Baseline・Main・High、8 bit 4:2:0）。field・MBAFF の絵は飛ばす（status の ERROR）。

## 1. 道・class・engine

| 世代 | NVDEC の class | method | pic の struct |
| --- | --- | --- | --- |
| TU102・TU104・TU106・TU116・TU117（RTX 20・GTX 16） | `0xC4B0` | C5B0 と同じ | `nvdec_h264_pic_s` |
| GA10x（RTX 30） | `0xC7B0` | 同じ | 同じ |
| AD10x（RTX 40） | `0xC9B0` | 同じ | 同じ |

- class は family（`enum nvrtx_family`）から `nvdec.c` が選ぶ（profile の位置づけの表 `identify.c` は変えない。P2・P5 と重ならないため）。
- method（C5B0、open-gpu-doc `classes/video/clc5b0.h`）: APPLICATION_ID 0x200（H264 = 3）、EXECUTE 0x300、CONTROL_PARAMS 0x400、
  DRV_PIC_SETUP 0x404、IN_BUF_BASE 0x408、PICTURE_INDEX 0x40C、SLICE_OFFSETS 0x410、COLOC 0x414、HISTORY 0x418、NVDEC_STATUS 0x424、
  LUMA_OFFSET0〜16 0x430〜、CHROMA_OFFSET0〜16 0x474〜、H264_MBHIST 0x500。address は GPU の仮想 address を 8 bit 右へずらす（256 byte 整列）。
- RM の engine type: NVDEC0 = 0x13（`cl2080_notification.h`）。devinfo の entry（100 byte）の +0 が ENG_DESC、+8 が RM の engine type、+12 が runlist。

## 2. channel（`nvdec.c`、新しい段 `nvdec-channel`）

- 条件: boot の parameter `nvrtx.video=1`（既定 off。RTX 2070 の実機で確かめるまで既定にしない）、かつ work channel が動いた後。
  失敗しても boot は続ける（video だけ無い device になる）。
- 手順（nouveau の GSP の時の順。Procedure referenced: Linux v6.19 `nvkm/subdev/gsp/rm/r535/fifo.c`・`r570/fifo.c`・`r535/nvdec.c`）:
  1. devinfo（`nvrtx_devinfo_pick` が既に読む表）から engine type 0x13 の entry の runlist と ENG_DESC を取る（`struct nvrtx_engine_pick` に欄を足す）。無ければ ENODEV。
  2. `NV2080_CTRL_CMD_GPU_GET_CONSTRUCTED_FALCON_INFO`（0x208001b0、internal の subdevice）で ENG_DESC が一致する falcon の ctxBufferSize を取る。
  3. VRAM の allocator から 64 KiB（instance 0x0、USERD 0x1000）と context の buffer（ctxBufferSize を 4 KiB に切り上げ、0 で埋める）を取る。
     instance block は work の空間の root を指す（`nvrtx_mmu_instance`）。context の buffer は `nvrtx_work_map_vram` で work の空間に map する。
  4. system memory の ring（512 entry）・semaphore の page・fence の slot と push の page を取り、`nvrtx_work_map_system` で map する。method buffer も取る。
  5. channel の確保（class は profile の `class_channel`、engine 0x13、空間 `NVRTX_HANDLE_WORK_SPACE`、chid **48**）→ BIND（engine 0x13）→ SCHEDULE。
  6. `NV2080_CTRL_CMD_GPU_PROMOTE_CTX`（0x2080012b、subdevice）: engineType 0x13、hClient・hChanClient = driver の client、ChID、hObject = channel、
     virtAddress・size = context の buffer、entryCount 0。
  7. NVDEC の object（`NV_BSP_ALLOCATION_PARAMETERS`: size 12、prohibitMultipleInstances 0、engineInstance 0）。
  8. 最初の push: subchannel 4 に SET_OBJECT（NVDEC の class）、host の fence。2 s 待つ。
- submit と待ち: `nvrtx_nvdec_submit()`（push の segment + host の semaphore release、WFI つき）、`nvrtx_nvdec_wait()`。GR の work channel と同じ形の
  ring・fence の slot（`nvrtx_push_fence`）。doorbell は `(runlist << 16) | chid`。lock は channel ごと。
- 停止: work channel の停止の前に SCHEDULE off → object → channel の free、release で buffer を返す（`gsp.c` の stop・release に 1 行ずつ）。
- chid 48（handle 0xf1f00030・0xc4b00030）は GR の 1〜4・copy の 8・session の channel の 16〜47（WS250、`NVRTX_WORK_SESSION_CHID_FIRST` から 32 本）と重ならない。2026-10-29 の main の取り込みで 16 から直した（16 と handle 0xf1f00010 は session の最初の channel と同じだった）。

## 3. 1 回の decode の push（`nvdec-records.c`、host 試験）

NVK の decode の method の並び（Values referenced: Mesa `src/nouveau/vulkan/rust/video/decode/h264.rs`）を値として使い、組み立ては自分で書く:

APPLICATION_ID(3) → CONTROL_PARAMS（codec 3、GPTIMER_ON、ERR_CONCEAL_ON、ERROR_FRM_IDX = 通し番号 mod 17、MBTIMER_ON）→ DRV_PIC_SETUP（pic の address）→
IN_BUF_BASE（bitstream の address）→ PICTURE_INDEX（通し番号）→ SLICE_OFFSETS → COLOC → HISTORY → NVDEC_STATUS → LUMA_OFFSET0〜16 → CHROMA_OFFSET0〜16 →
H264_MBHIST → EXECUTE（0）。subchannel 4。

- bitstream: Vulkan の `srcBuffer + srcBufferOffset` をそのまま IN_BUF_BASE にする（minBitstreamBufferOffsetAlignment 256 なので 256 byte 整列）。
  終わりに EOS の 16 byte が要る: `stream_len = srcBufferRange + 16`、`explicitEOSPresentFlag = 1`、pic の `eos[16]` に
  `00 00 01 0b 00 00 00 00 00 00 01 0b 00 00 00 00`（NVK の値）。NVDEC は pic の eos を使うので bitstream の buffer に書き足さない。
- slice の offset の表: 256 語。slice ごとの開始（`pSliceOffsets`）と最後に srcBufferRange。
- pic・slice の表・status は executor の system memory の run（`nvrtx_vk_gpu_alloc_system`、work の空間に map 済み）に、decode ごとの区画を取って書く。
- surface の番号: DPB の slot の番号（0〜16）をそのまま surface の番号（LUMA/CHROMA_OFFSETn、CurrPicIdx・CurrColIdx・dpb[].index・col_idx）にする。
  setup の slot が無い（参照にしない）decode は、どの active な参照にも使われていない番号を使い、その番号の LUMA/CHROMA に出力の image を置く。
- coloc の区画は slot ごと（coloc の大きさ ÷ 17）。

### pic（`nvdec_h264_pic_s`、764 byte）の主な値

p001 の表の byte・bit の位置（host で測った物）で書く。値:

- SPS から: log2_max_pic_order_cnt_lsb_minus4、delta_pic_order_always_zero_flag、frame_mbs_only_flag、PicWidthInMbs（pic_width_in_mbs_minus1 + 1）、
  FrameHeightInMbs（(2 − frame_mbs_only) × (pic_height_in_map_units_minus1 + 1)）、direct_8x8_inference、log2_max_frame_num_minus4、chroma_format_idc、
  pic_order_cnt_type、qpprime_y_zero_transform_bypass。
- PPS から: entropy_coding_mode、bottom_field_pic_order_in_frame_present（pic_order_present_flag）、num_ref_idx_l0/l1_default_active_minus1、
  deblocking_filter_control_present、redundant_pic_cnt_present、transform_8x8_mode、weighted_pred、weighted_bipred_idc、constrained_intra_pred、
  pic_init_qp_minus26、chroma_qp_index_offset、second_chroma_qp_index_offset。
- 絵から: ref_pic_flag（is_reference）、frame_num、CurrFieldOrderCnt[2]（PicOrderCnt[0..1]）、field_pic・bottom_field は 0（progressive だけ）。
- 固定: tileFormat 1（KBL）、gob_height 0（GOB 2 段）、gptimer_timeout_value 81000000、output_memory_layout 0（NV12）、frame_surfaces 0、
  pitch_luma = pitch_chroma = image の行の byte（64 の倍数）、luma_frame_offset・chroma_frame_offset 0（surface ごとの address を method で渡す）、
  luma_bot_offset = PicWidthInMbs × 16 と chroma_bot_offset = pitch_chroma ÷ 2（NVK の値。progressive では使われない）、HistBufferSize = history ÷ 256、
  mbhist_buffer_size = mbhist の byte。
- dpb[16]: Vulkan の参照の slot ごとに 1 entry（順は pReferenceSlots の順）: index = col_idx = slot、state 3（frame の参照）、is_long_term、
  not_existing（`nonExistingFrame`）、FieldOrderCnt[2]、FrameIdx（FrameNum または LongTermFrameIdx）。片方が INT32_MAX ならもう片方を写す（NVK の値の扱い）。
- scaling: `WeightScale[6][4][4]`・`WeightScale8x8[2][8][8]` を **raster の順**で埋める。PPS の scaling があればそれ、無ければ SPS、どちらも無ければ flat（16）。
  一つの list が無い時は H.264 の規格の fall-back の規則（Table 7-2 の A・B）で前の list か既定の表から取る。StdVideo の scaling list は zig-zag の順なので
  raster に並べ替える（zig-zag と既定の行列は規格の表、自分で書く）。

## 4. memory・NV12 の配置

- NV12 の image（`VK_FORMAT_G8_B8R8_2PLANE_420_UNORM`、optimal、1 layer・1 level）: plane 0（Y、1 byte の pixel）と plane 1（CbCr、2 byte の pixel、半分の幅と高さ）。
  両 plane とも block linear（GOB 64 byte × 8 行、block は GOB 1 × 2 = 64 byte × 16 行）。
  - pitch = align(width, 64) byte（両 plane で同じ。R8G8 の半分の幅は width byte）。
  - plane 0: 行 align(height, 16)、大きさ pitch × align(height, 16)。
  - plane 1: plane 0 の後（256 byte 整列はこの形で常に成り立つ）、行 align(height / 2, 16)、大きさ pitch × align(height / 2, 16)。
  - Value referenced: Mesa `src/nouveau/nil/image.rs`・`tiling.rs`（video の image の GOB 2 段、Fermi の GOB）。
- memory type は今の 1 つ（VRAM、BAR1 越しに host visible）。page の kind は今の VRAM の entry（非圧縮、generic）のまま。
- session の memory（3 binding、256 byte 整列、memory type bit 0）。Values referenced: Mesa `src/nouveau/vulkan/nvk_video_session.c`:
  - coloc: align(align(h_mb, 2) × w_mb × 64 − 63, 256) × (maxActiveReferencePictures + 1)
  - mbhist: align(w_mb × 104, 256)
  - history: align(w_mb × 0x300, 0x200)
  （w_mb・h_mb は session の maxCodedExtent の macroblock 数。）

## 5. fence と順序

- executor は session の command buffer を順に GR の push に変える（今の形）。decode の記録で:
  1. それまでの push を流して session の GR の run の終わりを待つ（`execute_drain` と同じ）。DPB・bitstream に先に書いた GR の仕事が終わる。
  2. NVDEC の push（executor の system memory）を `nvrtx_vk_video_decode()`（node が出す口、`vk-node.c`）で NVDEC の channel に出し、終わりを待つ（2 s で EIO）。
  3. push を次から始める。後の GR の命令（readback の copy）は decode の結果を読める。
- submission の fence は今のまま（最後の GR の run）。decode は同期なので fence の時には終わっている。
- 速さ: 1 枚ごとに CPU が 2 回待つ。1080p の NVDEC は数 ms の見込みで、30 fps に足りる見込み（p004 で測る）。GR が NVDEC の semaphore を
  acquire して待たない形は後の選択肢。

## 6. Vulkan Video の object（`vk-video.c`・`vk-video-h264.c`）

- 報告: queue family 0 に `VK_QUEUE_VIDEO_DECODE_BIT_KHR` を足す（video の channel がある時だけ。Pi と同じく decode と transfer が同じ family）。
  queue family の video properties は H.264 decode。capset を 176 byte にして native の tag `0x5a4e4154` と bit 0（H.264 decode）。
- capabilities（Values referenced: Mesa `nvk_video_session.c`・`nvk_physical_device.c`）: minBitstreamBufferOffsetAlignment 256、
  minBitstreamBufferSizeAlignment 256、pictureAccessGranularity 16×16、minCodedExtent 48×16、maxCodedExtent 4096×4096、maxDpbSlots 17、
  maxActiveReferencePictures 16、stdHeaderVersion（H.264 decode の拡張の名前と版）、decode の flags DPB_AND_OUTPUT_COINCIDE、
  H.264 の maxLevelIdc 5.2、fieldOffsetGranularity 0×0。profile は Baseline（66、constrained も）・Main（77）・High（100）、8 bit、4:2:0、
  pictureLayout は PROGRESSIVE だけ。
- format properties: NV12、optimal、usage VIDEO_DECODE_DST・DPB・TRANSFER_SRC（SAMPLED は出さない → libmedia は readback の道を使う）。
  `vkGetPhysicalDeviceFormatProperties(NV12)` は optimal に VIDEO_DECODE_OUTPUT・DPB・TRANSFER_SRC。
- session: maxCodedExtent・maxDpbSlots・maxActiveReferencePictures・profile を保ち、memory の要求 3 本・bind。
- parameters: SPS（32）・PPS（256）を StdVideo の field で保つ。update は key が重ならない物の追加（libvulkan が重なりを先に断る）。
  parameters の作成で template の parameters から写す。
- 記録: BeginVideoCoding（session・parameters・参照の slot の一覧）、EndVideoCoding、ControlVideoCoding（reset: session の通し番号を 0 に）、
  DecodeVideo（bitstream の buffer・offset・range、出力の view、setup の slot、参照の slot、H.264 の picture info と slice の offset）。
  記録は自分の struct に object を保持する（`record->regions` に置き、`record_drop` が video の drop を呼ぶ）。
- query（status）は出さない（queryResultStatusSupport 0）。

## 7. executor の口と既存の file への hook（最小）

| file | 変更 | 所有の重なり |
| --- | --- | --- |
| `vk/vk-dispatch.c` | route に `nvrtx_vk_video_route` を 1 行 | P5（WS250）が触る可能性 |
| `vk/nvrtx-vk-objects.h` | image に plane の欄（`planes`・`plane_offset[2]`・`plane_height[2]`・`gob_rows`）、video の route・record・drop・run の宣言 | 同上 |
| `vk/vk-record.c` | 4 つの opcode を route と decode に、drop で video の drop | 同上 |
| `vk/vk-execute.c` | decode の記録で drain → run、NV12 の image の buffer への copy は copy engine | 同上 |
| `vk/vk-resource.c` | NV12 の image の作成・memory の要求、view の作成で NV12 を通す | 同上 |
| `vk/vk-device.c` | family の flag、NV12 の format の features | 同上 |
| `vk/nvrtx-vk.h` | node の口 `nvrtx_vk_video_available()`・`nvrtx_vk_video_decode()` の宣言 | P5 |
| `vk-node.c` | 上の 2 つの実装（`nvrtx_nvdec_*` を呼ぶ） | P5 |
| `runtime.c` | video がある時 capset 176 byte | P5 |
| `work.c`・`objects.c`（`nvrtx_work_push_objects`） | work channel に copy の object（GRCE、subchannel 4） | P2（WS291 は channel を触る） |
| `objects.c`・`nvrtx-gsp.h` | devinfo の pick に NVDEC と GR の runlist の copy engine の欄 | P2 |
| `gsp.c`・`stage.c`・`nvrtx-gpu.h` | 段 `nvdec-channel`、`nvrtx.video=1`、state の欄 | P2 |
| `platform/amd64/vmunix.mk`（と nvrtx を build する他の mk） | 新しい file | — |

新しい file: `nvdec.c`（channel）、`nvdec-records.c`（RM の params・push・pic の組み立て、host 試験）、`nvrtx-nvdec.h`、`vk/vk-video.c`（wire と object）、
`vk/vk-video-h264.c`（parameters の保管・scaling・DPB の slot・pic の値、host 試験）、`vk/exec-copy.c`（copy engine の method、host 試験）。

### copy engine（GRCE）での de-tile

- GR の channel を作る時に copy の class（profile の `class_copy`）の object を GR の runlist の copy engine で確保し（`NVC5B5` の params: version 1、
  engine type = GR の runlist にある COPYn）、最初の push で subchannel 4 に SET_OBJECT。WS250 の session ごとの channel の後は、driver の work channel と
  session の channel のそれぞれに作る（`handle_copy`、session は 0xc5b50010 + 番号、free は 3D・copy・compute の順）。video の可否は driver の channel の
  copy の object（node）と session の channel の copy の object（session、channel が出来た後）。
- NV12 の image の plane を buffer へ写す時（`vkCmdCopyImageToBuffer`、aspect PLANE_0・PLANE_1）:
  OFFSET_IN = plane の address、OFFSET_OUT = buffer の address、PITCH_OUT = bufferRowLength（0 なら幅）× pixel の byte、
  LINE_LENGTH_IN = 幅 × pixel の byte、LINE_COUNT = 高さ、SET_SRC_BLOCK_SIZE（幅 1 GOB、高さ 2 GOB、深さ 1、GOB_HEIGHT_FERMI_8）、
  SET_SRC_WIDTH = pitch、SET_SRC_HEIGHT = plane の行、SET_SRC_DEPTH 1、SET_SRC_LAYER 0、SET_SRC_ORIGIN（x byte、y 行）、
  LAUNCH_DMA（NON_PIPELINED、FLUSH_ENABLE、SRC BLOCKLINEAR、DST PITCH、MULTI_LINE、仮想 address）。
  前後に今の barrier（GR の WFI）と host の WFI。
  Values referenced: NVIDIA open-gpu-doc `classes/dma-copy/clc5b5.h`。
- NV12 の image への copy（buffer → image）・NV12 の image の sample・blit は出さない（format の features に無い）。

## 8. 30/40（WS242）

class の番号だけが違う（C7B0・C9B0）。method・pic・session の memory の式は同じ。chid・doorbell は WS242 の channel の一般化に従う。実機の確かめは無い。

## 9. 試験（この WS の中）

- host（`plan/ws243/tests/`、gcc で driver の file をそのまま compile）:
  - T1 pic: 既知の SPS・PPS・picture info・参照の slot から pic を組み、p001 の表の byte・bit の位置に期待の値が入る。
  - T2 scaling: 規格の fall-back（A・B）と zig-zag → raster の並べ替え（flat・既定・明示の list）。
  - T3 push: 1 回の decode の method の並び・値・subchannel の header（address の >> 8）。
  - T4 session の memory の式（720p・1080p）と NV12 の配置（pitch・plane の offset・大きさ）。
  - T5 copy engine の method（plane 0・plane 1、bufferRowLength 0 と非 0）。
  - T6 RM の params（promote・BSP の alloc・falcon info の照合・devinfo の pick）。
- build: amd64 の `vmunix`（warning 0）。libvulkan は変えないが、venus-reg・arm64・riscv64 の libvulkan が壊れていないことを build で見る。
- 実機（p004、WS241 の後）: `nvrtx.video=1` で RTX 2070、vkvideo-probe（readback の道）で 720p・1080p の clip の hash を software の decoder と照らす。

## 10. 危険

| # | 危険 | 対処 |
| --- | --- | --- |
| R1 | GSP-RM が GR の channel に copy の object（GRCE）を許さない／GR の後の CE の順序が保たれない | p004 で最初に見る。駄目なら案 B: 3D の quad の copy に R8・R8G8 の format と GOB 2 段の texture header を足す |
| R2 | NVDEC の context の buffer・promote の形が 570 の RM と違う | 段の log に RM の status を出す。nouveau r570 の順を写した値で直す |
| R3 | readback の buffer が VRAM（BAR1 越しの UC の読み）で、1080p の 3 MiB の CPU の読みが遅い | p004 で測る。足りなければ host の cached な system memory の memory type（executor 全体、WS243 の外）を Q1 に提案 |
| R4 | NVDEC が eos を pic から取らず bitstream の後ろを読む | bitstream の buffer の range の後ろ 16 byte に eos を GPU の側で書く案（buffer の大きさの余裕が要る） |
| R5 | 同期の待ちで 30 fps に届かない | GR が NVDEC の semaphore を acquire する形へ |
| R6 | 1 枚ごとの status を見ないので decode の誤りが分からない | NVDEC_STATUS の区画を読み、mbs_in_error を log に出す（query は後） |

## 11. 判断（推しで進めた物、Q1 へ報告）

| # | 判断 | 推し（採った物） | 理由 |
| --- | --- | --- | --- |
| D1 | readback の de-tile | copy engine（GRCE）を work channel に置く | GPU で行い（guardrail）、format の拡張が要らず、CE は BL → pitch を任意の pitch・offset でできる（NVK も copy は CE） |
| D2 | decode の順序 | 同期（drain → NVDEC → 待つ） | 正しさが分かりやすい。速さは p004 |
| D3 | 既定の on/off | `nvrtx.video=1` の時だけ | 実機の確かめの前に既定の boot を変えない |
| D4 | SAMPLED の NV12 | 出さない | libmedia は readback の道を使う。sample には R8・R8G8 の BL の texture header が要る（後） |
| D5 | readback の memory | 今の 1 type のまま | memory type の追加は executor 全体の変更で WS243 の外（R3） |

## 12. license の監査

| 出典 | license | 使い方 |
| --- | --- | --- |
| NVIDIA open-gpu-doc（`classes/video/clc5b0.h`・`nvdec_drv.h`・`classes/dma-copy/clc5b5.h`）commit `9fdf5c4062007929d9f4e6cbad9c9771fe61b880` | MIT | method の offset・field の位置（値）。block ごとに「Value(s) referenced:」 |
| NVIDIA open-gpu-kernel-modules 570.144（`ctrl2080gpu.h`・`nvos.h`・`cl2080_notification.h`・`g_gpu_class_list.c`）| MIT | RM の control の番号・params の位置・class の表（値） |
| Mesa（`src/nouveau/vulkan/nvk_video_session.c`・`nvk_cmd_video.c`・`rust/video/decode/h264.rs`・`nil/`）main `921d028a5fff2beb4ec90451913b5fcc05fb1e6a` | MIT | 定数・式・method の並び（値と手順の参照）。code・comment・構造は写さない |
| Linux v6.19 nouveau（`r535/fifo.c`・`r570/fifo.c`・`r535/nvdec.c`）| GPL-2.0 | RM を呼ぶ順（Procedure referenced）と値だけ。code・comment・構造は写さない |
| ITU-T H.264（Table 7-2・8.5.6 の zig-zag・既定の scaling の表）| 規格 | 表の値 |

zedBSD の code は Zlib。名前（関数・型・macro）は自分で付け、出典の symbol の名前を写さない。

## 13. review 1（design-reviewer、2026-10-29）の対応

| 指摘 | 対応 |
| --- | --- |
| F1 query が無いと libmedia が動かない（libvulkan は queryResultStatusSupport を必ず VK_TRUE で返す） | **採る**。`VK_QUERY_TYPE_RESULT_STATUS_ONLY_KHR` の pool（作成・破棄・結果）と reset・begin・end の記録を `vk/vk-video.c` に足す。decode は同期なので結果は実行の時に決まる: NVDEC_STATUS の区画の mbs_in_error が 0 なら COMPLETE、他・飛ばした絵は ERROR（R6 も解く）。§6 の「query は出さない」を消す |
| I1 devinfo の engine type と control の番号は別 | **採る**。devinfo（RM の内部の番号）は 570 で RM_ENGINE_TYPE_NVDEC0 = **0x1d**（Linux v6.19 `r570/nvrm/engine.h`）。channel の確保・BIND・PROMOTE は NV2080 の 0x13（nouveau の r535_fifo の engn->id が NV2080 の番号）。選んだ entry を段の log に出す |
| I2 marking | 実装で入れた（短期 1・長期 2、両 field）。§3 に足す |
| I3 coloc | **採る**。COLOC は session の coloc の先頭だけ（NVK と同じ、区画は firmware が col_idx で選ぶ）。大きさは max(maxDpbSlots, maxActive + 1) 区画。§3 の「÷17」は誤り |
| I4 slice の表の溢れ・確かめ | **採る**。sliceCount 1〜255、offset は増えて range の中、srcBufferOffset + Range は buffer の中、出力・参照の image が session の絵を収める、session の memory が bind 済み、set の id が範囲の中。API の規則違反は記録を失敗に、bitstream の値の不正はその絵を飛ばす（status ERROR） |
| I5 timeout・fault の回復 | **採る（一部）**。timeout で NVDEC の channel を使わなくする（以後の decode は即 EIO、video は boot まで無い）。memory の解放を NVDEC の停止まで延ばす件と RC の通知の扱いは p004 で（R7・R8 に足す） |
| I6 hook の漏れ | **採る**。`vk-memory.c` の整列（video の image は 4 KiB）、`execute_buffer_image` の aspect の分岐を確かめの前に、NV12 の image を sample・copy_image・blit・clear・target にするのを断る、`resource_layout` は NV12 を除く、NV12 の linear の features 0、記録は自分の struct に deep copy |
| I7 D4 と guardrail「画像の処理は GPU で」 | **Q1 へ上げる**。readback の道は libmedia が CPU で crop と copy をする（app の側）。推し: WS243 では readback で正しさを先に（p004 の hash の照合はこの道）、SAMPLED の NV12（R8・R8G8 の BL で GOB 2 段の texture header）と export を後の Phase（p006 案）にして gpu の道を本線にする。それまで readback を例外として記録する |
| I8 copy object は video=1 の時だけ | 実装済み（断られても続け、video を出さない）。§7 の表の `nvrtx_work_push_objects` は work-records.c（訂正） |
| I9 R3 は今相談 | **Q1 へ上げる**（1080p30 で約 94 MB/s を BAR1 越しに読む。候補: host cached の system memory の memory type、または I7 の gpu の道） |
| I10 drain が全 session の GR を待つ | 危険の表に足す（R9）。p004 で compositor の frame 時間も測る |
| M1 page の kind | 今の VRAM の entry は kind 0（pitch）。NVDEC・CE が kind pitch の page に BL を書けるかを R10 に |
| M2 ERROR_FRM_IDX | NVK と同じ「通し番号 mod max_num_ref_frames」（0 なら 1） |
| M3 CE の offset | OFFSET_OUT = buffer + bufferOffset、ORIGIN は imageOffset（plane 1 は半分の単位を byte に） |
| M4 置き場所 | session ごとに executor の system memory の run（pic 1 KiB・slice 1 KiB・status 4 KiB・push 512 B）を作成の時に取る |
| M5 surface の空き | setup の slot の無い decode で空きが無い時は飛ばす（ERROR） |
| M6 update の順 | Pi と同じく updateSequenceCount が前の + 1 であることを見る |
| M7 capset | `capacity >= 176` の時だけ 176 byte |
| M8 出典の語 | method の並びは「Procedure referenced:」。版は Mesa main 921d028a（video）、Mesa 25.3.6（nv_push.h、既存の引用）を §12 に足す |
| M9 wire の往復の試験 | host 試験に libvulkan の video.c の encoder と nvrtx の reader を 1 本で往復させる物を足す（T7） |
| M10 Phase の表 | ws.md の p003 の範囲に CE・query・NV12 の hook を書く |

追加の危険: R7 NVDEC の timeout 後に NVDEC がまだ書いているかもしれない memory の解放、R8 GSP-RM の RC・MMU fault の通知、R9 node の lock の下の同期の待ちで compositor が止まる、R10 kind pitch の page の BL。
