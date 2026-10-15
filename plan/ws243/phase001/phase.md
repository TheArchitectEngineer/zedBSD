<!-- awesome-plan project=zedbsd record=ws243-p001 -->

# ws243-p001: 設計（NVDEC の H.264 を Vulkan Video で）

Parent: [WS243](../ws.md)
Status: in-progress（2026-10-16 ユーザーの指示で WS243 を一時停止。調べの途中で止めた。design.md はまだ書いていない）
担当: P6（2026-10-16）。code は書いていない。

## 再開の点

1. 下の「調べた事実」と「設計の案」をもとに `plan/ws243/design.md` を書く（節: 道・class・engine、channel、picture の parameter、memory と NV12 の配置、fence、Vulkan Video の object、executor の口と既存の file の hook、30/40 の差、試験、危険、判断、license の監査の表）。
2. design.md と、この phase.md を WIP commit し、Q1 に SHA を送る（Q1 が design-reviewer を回す）。
3. review を直してから p002。

参考の source は `plan/ws243/temp/`（git に入れない）にある。消えていたら下の版で取り直す。

| temp の中 | 中身 | 版 |
| --- | --- | --- |
| `ogd/` | NVIDIA open-gpu-doc（MIT）。`classes/video/clc5b0.h`（NVDEC の method）・`nvdec_drv.h`（picture の parameter の struct） | commit `9fdf5c4062007929d9f4e6cbad9c9771fe61b880` |
| `ogkm/` | NVIDIA open-gpu-kernel-modules（MIT）の sparse。`src/common/sdk/nvidia/inc/`（class・ctrl）、`src/nvidia/generated/g_gpu_class_list.c` | tag `570.144`（commit `8ec351aeb96a93a4bb69ccc12a542bf8a8df2b6f`） |
| `mesa/` | Mesa の sparse（`src/nouveau/vulkan`・`nil`・`headers`）。NVK の video decode（`nvk_video_session.c`・`nvk_cmd_video.c`・`rust/video/decode/h264.rs`）、NIL の block linear（`tiling.rs`・`copy.rs`） | main `921d028a5fff2beb4ec90451913b5fcc05fb1e6a`（2026-10-15） |
| `offsets.txt`・`off.c` | `nvdec_h264_pic_s` を host で compile して測った field の byte の位置と bit の位置（下の表の元） | 上の ogd |
| nouveau（Linux v6.19） | P1 の worktree の `plan/ws037/temp/linux/` を読んだ（自分の temp には写していない）。`nvkm/subdev/gsp/rm/r535/fifo.c`・`r570/fifo.c`・`r535/nvdec.c`・`rm/engine.c` | commit `05f7e89ab9731565d8a62e3b5d1ec206485eeb0b` |

## 守る規則（2026-10-16 の Q1 経由のユーザーの決定、guardrail の「MIT・GPL の出典のデータの引用」）

- MIT・GPL の source から使ってよいのは**データ**（method の offset、struct の field の位置、定数、表）だけ。block ごとに引用元（path・版・struct か表の名前・分かれば行）を comment に書く。著作権表示は付けない。
- **code・comment・構造は MIT でも写さない**。NVK の rust の手順（DPB の管理など）は読んで事実を得るだけで、zedBSD の code は自分で書く。

## 調べた事実（データ、全て source を読んだだけ、実機の観測なし）

### class と engine

- NVDEC の class（`ogkm/src/nvidia/generated/g_gpu_class_list.c`）: TU102・TU104・TU106・TU116・TU117 は `0xC4B0`、GA100 は `0xC6B0`、GA10x は `0xC7B0`、AD10x は `0xC9B0`。TU106 は NVDEC 0〜2、TU102 は 0 だけが表にある。
- method の offset は C5B0・C6B0・C7B0・C9B0 で同じ（`ogd/classes/video/clc*b0.h` を比べた）: SET_APPLICATION_ID 0x200（H264 = 3）、SET_WATCHDOG_TIMER 0x204、SEMAPHORE_A〜C 0x240〜0x248、EXECUTE 0x300、SEMAPHORE_D 0x304、SET_CONTROL_PARAMS 0x400（codec 3:0 = 3、GPTIMER_ON 4、RET_ERROR 5、ERR_CONCEAL_ON 6、ERROR_FRM_IDX 12:7、MBTIMER_ON 13）、SET_DRV_PIC_SETUP_OFFSET 0x404、SET_IN_BUF_BASE_OFFSET 0x408、SET_PICTURE_INDEX 0x40C、SET_SLICE_OFFSETS_BUF_OFFSET 0x410、SET_COLOC_DATA_OFFSET 0x414、SET_HISTORY_OFFSET 0x418、SET_NVDEC_STATUS_OFFSET 0x424、SET_PICTURE_LUMA_OFFSET0〜16 0x430〜0x470、SET_PICTURE_CHROMA_OFFSET0〜16 0x474〜0x4B4、H264_SET_MBHIST_BUF_OFFSET 0x500。address の method は GPU の仮想 address を 8 bit 右へずらした値（256 byte 整列が要る）。
- clc4b0.h（ogkm）には class の番号しか無い。method は C5B0 の物を使う（NVK も C5B0 の method を全世代に使う）。
- RM の engine type: GRAPHICS 1、COPY0 9、NVDEC0 = BSP = 0x13（`ogkm/.../class/cl2080_notification.h` 283・292・302）。nvrtx の devinfo の表（`objects.c` の `nvrtx_devinfo_pick`）の entry の +8 が RM の engine type、+12 が runlist、+0 が engine の記述子（ENG_DESC、nouveau `r535/nvrm/fifo.h` の ENGINE_INFO_TYPE の順: ENG_DESC 0・FIFO_TAG 1・RM_ENGINE_TYPE 2・RUNLIST 3）。
- NVDEC の object の確保の parameter（`ogkm/.../nvos.h` 2550〜2555 `NV_BSP_ALLOCATION_PARAMETERS`）: size・prohibitMultipleInstances・engineInstance の 3 語（12 byte）。nouveau は size と engineInstance を書く。
- **NVDEC の firmware は GSP-RM が持つ**: nouveau は GSP の時 `tu102_nvdec_new` で自前の NVDEC を作らず（`engine/nvdec/tu102.c`）、RM に object を確保させるだけ（`rm/r535/nvdec.c`）。linux-firmware に Turing の NVDEC の ucode は無い（あるのは scrubber）。driver が firmware を load する仕事は無い見込み。
- **engine の context の buffer が要る**（falcon の engine）: 大きさは `NV2080_CTRL_CMD_GPU_GET_CONSTRUCTED_FALCON_INFO`（0x208001b0、570 の形、internal の subdevice に出す。params は numConstructedFalcons と 64 個の {engDesc, ctxAttr, ctxBufferSize, addrSpaceList, registerBase}、`ogkm/.../ctrl2080gpu.h` 4381〜4397、nouveau `r570/fifo.c` 118〜145）で、devinfo の ENG_DESC と engDesc を照らして ctxBufferSize を取る。VRAM に確保し、channel の仮想 address の空間に 4 KiB の page で map し、`NV2080_CTRL_CMD_GPU_PROMOTE_CTX`（0x2080012b、params は engineType・hClient・ChID・hChanClient・hObject・hVirtMemory・virtAddress・size・entryCount（0）…、`ctrl2080gpu.h` 959〜971）で知らせる。nouveau の順（`r535/fifo.c` 150〜320）: channel の確保 → BIND（engineType = 0x13）→ SCHEDULE → context の buffer の promote → NVDEC の object の確保。

### picture の parameter（`nvdec_h264_pic_s`、`ogd/classes/video/nvdec_drv.h` 857〜962、host で測った 764 byte）

主な位置（byte・bit）: eos[16] 52、explicitEOSPresentFlag 68、stream_len 72、slice_count 76、mbhist_buffer_size 80、gptimer_timeout_value 84、log2_max_pic_order_cnt_lsb_minus4 88、delta_pic_order_always_zero_flag 92、frame_mbs_only_flag 96、PicWidthInMbs 100、FrameHeightInMbs 104、tileFormat 108 bit 0〜1（0 TBL = Tegra、**1 KBL = desktop の block linear**、2 16x16）、gob_height 108 bit 2〜4（0 = GOB 2 段）、entropy_coding_mode_flag 112、pic_order_present_flag 116、num_ref_idx_l0/l1 120・124、deblocking 128、redundant 132、transform_8x8 136、pitch_luma 140、pitch_chroma 144、luma_top/bot/frame 148・152・156、chroma_top/bot/frame 160・164・168、HistBufferSize 172（256 byte 単位）、flags の語 176（Mbaff 0、direct_8x8 1、weighted_pred 2、constrained_intra 3、ref_pic 4、field_pic 5、bottom_field 6、second_field 7、log2_max_frame_num_minus4 8〜11、chroma_format_idc 12〜13、poc_type 14〜15、pic_init_qp_minus26 16〜21、chroma_qp_index_offset 22〜26、second_chroma_qp 27〜31）、語 180（weighted_bipred 0〜1、CurrPicIdx 2〜8、CurrColIdx 9〜13、frame_num 14〜29、frame_surfaces 30、output_memory_layout 31（0 NV12））、CurrFieldOrderCnt[2] 184、dpb[16] 192（1 entry 16 byte: 語 0 に index 0〜6・col_idx 7〜11・state 12〜13・is_long_term 14・not_existing 15・is_field 16・top_marking 17〜20・bottom_marking 21〜24・output_memory_layout 25、FieldOrderCnt[2] +4、FrameIdx +12）、WeightScale[6][4][4] 448（**raster の順**、zig-zag ではない）、WeightScale8x8[2][8][8] 544（同じ）、MVC の欄 672〜719（0）、語 720（lossless_ipred8x8 0、qpprime_y_zero_transform_bypass 1）、displayPara 724（28 byte）、ssm 752（12 byte）。status（`nvdec_status_s`）: mbs_correctly_decoded 0、mbs_in_error 4、cycle_count 8、error_status 12、全体 56 byte。

### NVK（Mesa、MIT）から得た値（データとして、手順は自分で書く）

- session の memory 3 本（256 byte 整列）: coloc = align(align(h_mb, 2) × w_mb × 64 − 63, 256) × (maxActiveReferencePictures + 1)、mbhist = align(w_mb × 104, 256)、history = align(w_mb × 0x300, 0x200)（`nvk_video_session.c` の create）。
- capabilities: minBitstreamBufferOffset/SizeAlignment 256、pictureAccessGranularity 16×16、minCodedExtent 48×16、maxCodedExtent 4096×4096、maxDpbSlots 17、maxActiveReferencePictures 16、maxLevelIdc 5.2、decode の flags は DPB_AND_OUTPUT_COINCIDE、profile 66・77・100、8 bit、4:2:0。
- 1 回の decode の method の並び（`h264.rs` の decode）: APPLICATION_ID H264 → CONTROL_PARAMS（codec 3、gptimer 1、err_conceal 1、mbtimer 1、error_frm_idx = 通し番号 mod max_num_ref_frames）→ PIC_SETUP → IN_BUF_BASE（bitstream の address）→ PICTURE_INDEX（通し番号）→ SLICE_OFFSETS → COLOC → HISTORY → STATUS → LUMA_OFFSET 0〜16 → CHROMA_OFFSET 0〜16 → MBHIST → EXECUTE（notify なし）。SET_OBJECT は subchannel 4。
- pic の値: explicitEOSPresentFlag 1 と eos の 16 byte（`00 00 01 0b 00 00 00 00 00 00 01 0b 00 00 00 00`）、stream_len = srcBufferRange + 16、gptimer_timeout_value 81000000、tileFormat 1、gob_height 0、luma_bot_offset = PicWidthInMbs × 16、chroma_bot_offset = pitch_chroma / 2、HistBufferSize = history >> 8、slice の offset の表は 256 語で sliceCount 個の先頭と最後に srcBufferRange、CurrPicIdx = CurrColIdx = 絵の番号（0〜16、参照の slot の番号を優先）、dpb の FieldOrderCnt は INT32_MAX の時もう片方。
- NVK は scaling の行列を常に flat（16）にしている。zedBSD は SPS・PPS の scaling list と規格の fall-back から raster の順で埋める（i915 の WS083 と同じ考え、自分で書く）。
- NV12 の image（NIL）: block linear、GOB は 64 byte × 8 行（512 byte、Fermi の color の GOB の中の 16 byte の行の並びは `nil/copy.rs` の表）、video の image は GOB の段を 2（block = 64 byte × 16 行）に固定、plane 0 は R8、plane 1 は R8G8、block の大きさは両方の plane で同じ。NVK は Turing で非圧縮の block linear の page に kind GENERIC_MEMORY を使う（`nil/image.rs` の choose_pte_kind）。

## 設計の案（まだ文書にしていない、design.md で決める）

- **channel**: NVDEC の channel を、GR の work channel と**同じ仮想 address の空間**（`NVRTX_HANDLE_WORK_SPACE`）に作る。app の memory（`nvrtx_work_map_*` で map 済み）がそのまま同じ address で NVDEC から見える。ring・push・semaphore は system memory を `nvrtx_work_map_system` で、instance・USERD・context の buffer は VRAM の allocator から。fence は今の host の semaphore の release（`nvrtx_push_fence`）。channel の番号は他と重ならない値（例 16）。boot の parameter `nvrtx.video=1`（既定 off）の時だけ work channel の後に作る（新しい段 P9v）。
- **queue family**: Pi（WS236）と同じく family 0 に VIDEO_DECODE を足す（libmedia は decode と transfer が同じ family でよい）。executor は同期で走るので、decode の前に GR の push を流して待ち、decode を NVDEC に出して待つ（順序はそれで保たれる）。
- **出力と DPB**: NV12 の block linear（KBL、GOB 2 段）、COINCIDE。`vkCmdCopyImageToBuffer`（PLANE_0・PLANE_1）は p003 ではまず CPU で de-tile（正しさ優先）。**危険**: VkDeviceMemory は全て VRAM で CPU は BAR1（UC）越しに読むので、1080p の 3 MiB の読みが遅く実時間に足りない見込み。readback の buffer を host の cached な system memory に置く memory type（executor 全体の変更、WS037・WS241 の範囲）が**計画に無い依存**になりうる → design.md に判断の項目として書き、Q1 に相談する。GPU の de-tile（BL の R8・R8G8 の texture を quad で）と、NVDEC の display 出力（displayPara・SET_DISPLAY_BUF_*）で pitch を直接書く案は後の選択肢。
- **progressive だけ**（frame_mbs_only_flag = 1）。field・MBAFF の絵は飛ばす。status の query は最初は出さない（queryResultStatusSupport = 0）。
- **既存の file への hook（最小）**: `vk-dispatch.c`（route 1 行）、`nvrtx-vk-objects.h`（image に plane・block linear の欄、route の宣言）、`vk-record.c`（video の 4 opcode の記録）、`vk-execute.c`（decode の前の flush と実行、NV12 の copy-out）、`runtime.c`（video がある時 capset 176 byte、tag 0x5a4e4154、bit 0）、`vk-device.c`（family の flag）、`gsp.c`（P9v の start・stop・release）、`nvrtx-gpu.h`（state の欄）、`platform/amd64/vmunix.mk`（新しい file）。新しい file: `nvdec.c`（channel）、`nvdec-records.c`（RM の params・push・pic の組み立て、host 試験）、`nvrtx-nvdec.h`、`vk-video.c`（wire と object）、`vk-video-h264.c`（parameter の保管・scaling・DPB の slot・NV12 の配置と de-tile、host 試験）。
- **再利用**: wire の形は libvulkan の video.c（i915・Pi と同じ）。i915 と Pi の video の reader は各 executor の static な関数で共通の module が無いので、nvrtx は自分で書く。共通の module（`src/drivers/gpu/` の video の wire・H.264 の表）にまとめるのは Future Work の候補。H.264 の zig-zag・既定の行列の表は規格の表なので nvrtx に置く（i915 の `video-h264-tables.c` は i915 の header を引くので link しない）。
- **30/40（WS242）**: class の番号だけが違い（C7B0・C9B0）、method と pic の struct は同じ。channel の番号・doorbell の違いは WS242 の channel の一般化に従う。
- **試験**: host で pic の組み立て（既知の SPS・PPS・picture info から上の byte の位置に値が入る）、scaling の fall-back と raster の並び、DPB の slot の遷移、BL の de-tile を NIL の GOB の表と照らす。実機は p004（WS241 の後）、vkvideo-probe は `--sampled`（copy の道）で hash を照らす（image を直接 map する道は Intel と linear しか知らない）。

## 記録

- 2026-10-16 P6: 規則・手本（nvrtx の channel・work channel・executor、i915・Pi の Vulkan Video、libmedia の vkvideo-runtime、vkvideo-probe）と出典（open-gpu-doc、open-gpu-kernel-modules 570.144、Mesa NVK、nouveau v6.19）を読んだ。上の事実を集めた所で、ユーザーの指示で WS243 を一時停止（Q1 のラップアップの依頼）。code・試験・build は未実施。
