<!-- awesome-plan project=zedbsd record=ws269-design-p010 -->

# WS269 設計の追補: p010（graphics）の分割

- 版: 2.2（2026-10-20、§6 の transfer の道を Mesa に合わせた注記、Q1 採用、P2 q1381）。版 2.1（2026-10-20、P1、q1370 / q1370-i01）。版 2（q1367、7541d5612）に [design-p010-review-2.md](design-p010-review-2.md)（Blocker なし、M-1〜M-6・Minor 1〜11）を入れた。対応の一覧は末尾の §13（版 2 の対応は §12）。版 1（q1362）は [design-p010-review-1.md](design-p010-review-1.md) と [phase010b](phase010b/phase.md) で版 2 にした。親の設計は [design.md](design.md) 改訂 3.1（§5 の案 A、§6 の p010、§7 の判定）。コードは書いていない。review 2 の結論 2・3 のとおり、**design-reviewer の再レビューは要らず、Q1 が差分を見て、P2 と §5.4 の O-ENC の一覧を合意すれば** p010d・p010n・p010e・p010f を始めてよい。
- 起点（2026-10-20 07:24 ユーザー）: 「PowerVRのコードは完了を目指して作業をお願いします。あとでUATにおいて、どこまで実行できたかわかるログが出れば、デバッグしやすいと思います。」
- ユーザーは不在。判断の要る点は推奨を採って先へ進める前提で §9 に D-P1〜D-P7 として列挙する（採否はユーザー）。
- 印は design.md と同じ: **事実**（出典あり）、**推測**、**未確認**。
- 出典の書き方（Guardrail「MIT・GPL の出典のデータの引用」、`plan/guardrail.md:120`）: source では、**使う block の直前の複数行の comment** に `Value(s) referenced: <版> <path> (<表・define・関数の名前>)` か `Procedure referenced: <版> <path> (<関数の名前>)` を書く。file の先頭には書かない。本文の M1〜M3 の番号は plan の中の略号で、**source の comment には書かない**（版と path を書く）。code・comment・構造・symbol の名前は写さない。
- FW の中の未確認の挙動（p010b の残り 3 点: FRAG_PR が無い時の OOM、context switch の state が 0 の時、TRANSFER_FRAG の base）は確かめずに、**Mesa・mainline と同じ形を採って避ける**（Q1 の判断、2026-10-20、queue.md の q1367）。

## 0. 出典と、この追補で確かめていない事

| # | 資料 | 使い道 |
| --- | --- | --- |
| M1 | Mesa 26.2.5（`ddaf85e353442d9d7f192fdcf1823e652e3f2ee3`、p002 で固定、MIT）の `src/imagination/csbgen/rogue/` の XML（`cr.xml`・`ipf.xml`・`vdm.xml`・`ppp.xml`・`pbestate.xml`・`texstate.xml`・`lls.xml`・`cdm.xml`・`pds.xml`・`kmd_stream.xml`。接頭辞 `rogue_` なし、TA の XML は無い。`volcanic/` は使わない）と `csbgen/gen_pack_header.py`（§10 の照合だけに使う）。blob SHA は [phase010b](phase010b/phase.md) | 制御の stream・state の word の bit の位置（Value(s) referenced） |
| M2 | 同 `src/imagination/vulkan/` の `pvr_arch_job_render.c`・`pvr_arch_job_transfer.c`・`pvr_arch_job_context.c`・`pvr_arch_job_compute.c`・`pvr_arch_job_common.c`・`pvr_arch_cmd_buffer.c`・`pvr_arch_image.c`・`pvr_arch_formats.c`・`pvr_arch_tex_state.c`・`pvr_arch_descriptor_set.c`・`pvr_arch_pass.c`・`pvr_arch_hw_pass.c`・`pvr_arch_pipeline.c`・`pvr_arch_spm.c`、共通の `pvr_image.c`・`pvr_formats.c`・`pvr_descriptor_set.c`・`pvr_spm.c`・`pvr_rt_dataset.c`・`pvr_free_list.c`・`pvr_usc.c`（EOT・transfer・load op・clear attachment の program）・`pvr_common.h`、`rogue/pvr_clear.c`、`winsys/pvr_winsys_helper.c`（static data area）、`winsys/powervr/pvr_drm.c`（heap と RGNHDR）、`pco/usclib/common.cl`（SR・VDM sync の nop の program）、`pds/pvr_pipeline_pds.c`、`winsys/powervr/pvr_drm_job_{render,transfer}.c`、compiler の `pco/pco_data.h`・`pco_nir.c`・`pco_nir_pvfio.c`、`common/device_info/bxe-4-32.h`。`pvr_job_render.c` 等の vulkan 直下の旧い名前は 26.2.5 に無い。blob SHA は [phase010b](phase010b/phase.md) | 手順（Procedure referenced）と表の値 |
| M3 | Linux mainline v7.3（`c03c1a14507dfaf8b856c2608f9ac216021fc222`、GPL-2.0 OR MIT）の `drivers/gpu/drm/imagination/`（`pvr_rogue_fwif.h`・`pvr_rogue_fwif_client.h`・`pvr_rogue_fwif_client_check.h`・`pvr_rogue_fwif_shared.h`・`pvr_rogue_fwif_check.h`・`pvr_rogue_heap_config.h`・`pvr_job.c`・`pvr_context.c`・`pvr_free_list.c`・`pvr_hwrt.c`・`pvr_ccb.c`・`pvr_fw.c`・`pvr_vm.c`・`pvr_queue.c`・`pvr_cccb.c`）と `include/uapi/drm/pvr_drm.h`。blob SHA は [phase010b](phase010b/phase.md)（`_client_check.h`・`_check.h`・`pvr_ccb.c`・`pvr_fw.c`・heap_config・`pvr_vm.c`・`pvr_queue.c`・`pvr_cccb.c` の SHA は **p010d の必須項目**で一覧に足す、review 2 の Minor 11） | FW の command・object・heap・context の作り方（値と手順） |
| L1 | zedBSD の `userland/desktop/libvulkan/`（`wsi-display.c`・`wsi-swapchain.c`・`wsi-image.c`） | WSI の 2 つの道（§7） |
| L2 | `userland/tests/vkdemo`・`userland/tests/vkcompute`・`userland/tests/gpu-selftest` | 受け入れの program が呼ぶ Vulkan の命令（§8） |

- **事実（p010b）**: M1〜M3 の path と blob SHA、BXE（36.50.54.182）の feature の分岐（TLA なし、PBE の linestride の単位 2 pixel、twiddled の 2 の冪の padding、texture・sampler の word の大きさ、descriptor の大きさ）、blend・pack は compiler の側、free list の GROW・RECONSTRUCTION の応答、Mesa の FRAG_PR、HWRT の local・global の free list と rt_data の 2 組、heap の base、render context の static state の必須（[phase010b](phase010b/phase.md) の表）。
- **FW の版との組**: 既存の `interface.h` は StarFive の v6.12 の clone を出典にしている。v7.3 との差は client の header には無く、`reset_reason` の型と enum の追加だけ（review 1 の Minor 1、diff で確認）。p010 で新しく引く FW の struct は **v7.3 で揃え**、`interface.h` の既存の出典の comment は p010d で v7.3 に直す（値の変わらない所は comment だけ）。
- 事実（L1、2026-10-20 に読んだ）: アプリが描く swapchain の image は `VK_IMAGE_TILING_OPTIMAL`（`wsi-swapchain.c:1522`）。LINEAR の image は shared の道の scanout だけ（`wsi-image.c:60-80`、usage TRANSFER_SRC|TRANSFER_DST|SAMPLED、external（DMABUF か OPAQUE））。libvulkan は linear の SAMPLED が出ていると shared の道を先に試し（`wsi-image.c:56-58`、`wsi-swapchain.c:1095-1110`）、`VK_ERROR_FORMAT_NOT_SUPPORTED`・`VK_ERROR_FEATURE_NOT_PRESENT` の時だけ readback の道に落ちる。それ以外の失敗では swapchain の作成が失敗する。executor の errno は ENOTSUP が `FEATURE_NOT_PRESENT`、ENOMEM・EINVAL はそれ以外（`vk-object.c:315-330`）。import の前に `stride` を `stride_alignment` で検査し、外れたら `FORMAT_NOT_SUPPORTED`（`wsi-display.c:1290-1293`）。readback の道は OPTIMAL の swapchain の image から buffer への `vkCmdCopyImageToBuffer`（`wsi-swapchain.c:2791-2831`）、shared の道は `present_copy_shared` が `vkCmdClearColorImage`・`vkCmdBlitImage`（NEAREST）・`vkCmdCopyImage` を使う（`wsi-swapchain.c:3211-3333`）。
- 事実（L2）: vkdemo は `vkCreateRenderPass`・`vkCreateFramebuffer`・`vkCreateGraphicsPipelines`・`vkCmdBeginRenderPass`・`vkCmdBindVertexBuffers`・`vkCmdDraw`・`vkCmdPushConstants`・`vkCmdClearColorImage`・`vkCmdCopyBufferToImage`・`vkCmdCopyImageToBuffer`・`vkCreateSampler`・combined の descriptor を使い、format は `R8G8B8A8_UNORM`（optimal の COLOR_ATTACHMENT と SAMPLED）、`D32_SFLOAT`、`B8G8R8A8_UNORM`（display）を照会する（`renderer.c:170-183`、`display.c:675-700`）。vertex の属性は `R32G32B32_SFLOAT`（`renderer.c:1424`）。dynamic rendering は使わない。引数は `--duration=N`（`--frames` は無い、`main.c:79`）。
- **未確認（避け方を決めた）**: FRAG_PR が無い時の OOM の FW の挙動 → 常に積む（§4.2）。context switch の state が 0 の時 → Mesa の形で static state と SR・PT の program を作る（§4.1）。TRANSFER_FRAG の heap の base を誰が設定するか → Mesa と同じく heap の base からの相対で書き、kernel は heap を mainline の位置に置く（§6）。RGNHDR の heap → BXE（BRN63142 なし）では mainline が size 0 で返し、Mesa は region header を GENERAL の heap に置く（事実、review 2 の Minor 1: M3 `pvr_vm.c` の heap の size、M2 `pvr_drm.c`）。同じ形で GENERAL に置く（§4.1）。

## 1. 範囲

p010 の受け入れ（**design.md §6 の文面を変える。D-P7**）: 実機で **`vkdemo` の画をユーザーが目で見る**ことと、**`gpu-selftest` の `clear`・`copy`・`draw` が ok** であること。版 1 の「`venus-frame` の画」は外す（review 1 の M5: venus-frame は 1 つの fd で Venus と `GPU_RESOURCE_CREATE`・`GPU_PRESENT` を使い既定は `/dev/gpu0` だが、VF2 では gpu0 が display の node（Venus なし）、gpu1 が render の node（display の ops も `GPU_CAP_DISPLAY` も無い、`render.c:153-167`）。shader も graphics の pipeline も使わない）。venus-frame を 2 つの node に対応させる作業は p010 に入れない（要れば p011 か Future Work、D-P7 の結果で Q1 が決める）。加えて UAT の進み具合の画面の行（§8）を p010 の最初に作る（p010a、実装済み・実機待ち）。

範囲外: Keiland が使う残りの機能（p011）、MSAA（sample count 1 のみ）、tessellation・geometry shader、query、secondary の command buffer、dynamic rendering（p011 で要れば）、TFBC・FBCDC の圧縮、partial render の SPM（D-P1）、depth の STORE を伴う部分の renderArea（§4.3）、stencil。compiler（SPIR-V から USC）は p013・p014（§5、P2）で、p010 の Phase ではないが受け入れに要る。

Keiland の起動の待ち（design.md §3.3 の最後）は p005（q1356）の `hw.gpu.attaching` で済んでいる。p010 では実機で確かめるだけ（p010m）。

## 2. 全体の形

kernel の powervr の Venus の executor（p009、`vk-*.c`）が、Mesa の userland の driver と mainline の kernel の driver の両方の役をする:

```
libvulkan ──Venus──▶ vk-*.c（decode、object）
                       ├─ image・view・sampler・descriptor（memory の中の word）
                       ├─ render pass・pipeline（compiler の出力 + driver が組む PDS）
                       └─ command buffer の記録 → submit の時に
                            ├─ compute: CDM の stream（p008・p009）
                            ├─ render: VDM の stream（geometry）+ FRAG_PR + fragment（ISP・PBE・ZLS）
                            └─ transfer: transfer context の job（IPF の stream、FAST_2D）
render-queue.c ──FW の CCB──▶ FW（compute・geometry・fragment・transfer の 4 つの context）
```

- 1 回の vkQueueSubmit の中の command buffer を順に「job の列」に変える。job は compute・render（geometry・FRAG_PR・fragment の 3 つの kick）・transfer の 3 種。job の間の順は FW の CCB の UFO で守る（p006 の fence と同じ道）。barrier は job の境で全部を待つ（推測: 性能は p011 以降の課題）。
- **kick ごとの sync の word**（review 1 の M7）: 各 kick は自分の sync の word を UPDATE する（完了の値）。submit の待ちが時間切れになった時、kernel は各 kick の word を読んで「どの kick まで終わったか」を知り、段の行の `compute`・`ta`・`3d`・`transfer` のどれが止まったかを出す（§8.1）。
- submit は p009 と同じく ioctl の中で組み、最後の job の fence を上限付きで待つ（2 秒、core の lock の外、`vk-queue.c:898-912`）。超えたら p009 と同じ時間切れの道（abandoned、`drv_gpu_report_session_error(EIO)`）を通し、**その session の HWRT data set・free list は executor の memory と同じく core に残す**（abandoned の session に次の render は無い。isolate・device の reset の後に返す。§4.1、review 2 の M-1）。非同期の submit は p011 の候補（D-P4）。

## 3. image

### 3.1 tiling と layout

| tiling | 使う所 | layout |
| --- | --- | --- |
| LINEAR | libvulkan の shared の道の scanout、`VK_IMAGE_TILING_LINEAR` の全部 | 行の順。`rowPitch` は **32 byte の倍数かつ 2 pixel の倍数**の最小（2 pixel は PBE の linestride の単位、事実、p010b、M1 `pbestate.xml` の PBESTATE_REG_WORD0 の LINESTRIDE。32 byte は WS277 の R6 (1) の条件）。mip は 1 段、array は 1 層だけ受ける（他は `VK_ERROR_FORMAT_NOT_SUPPORTED`、format の照会でも出さない） |
| twiddled | `VK_IMAGE_TILING_OPTIMAL` の 2D の color・depth | Morton の順。幅・高さを 2 の冪に切り上げる（事実、p010b、M2 `pvr_image.c` の physical extent）。mip が 2 段以上の時、layer の大きさは 1×1 までの**全段**で数える（mip の数が少なくても。M2 `pvr_image.c` の layer size）。各段の始まりを揃え、層を順に並べる |

- **CPU は OPTIMAL の image に触らない**（review 1 の Minor 11）: 読み書きは全部 GPU（transfer・PBE・TPU）。kernel に CPU 側の Morton の式は置かず、host 試験は大きさ（2 の冪への切り上げ、全段の和）だけを見る。
- strided の 3D の image、cube・3D の image は p011（vkdemo は 2D だけ）。
- `vkGetImageSubresourceLayout` は LINEAR だけ答える（offset 0、rowPitch 上記、size = rowPitch·height）。
- memory の requirements: alignment は max(256 byte（p009 の `POWERVR_VK_RESOURCE_ALIGN`）、GPU の MMU の page の大きさ)（事実、p010b: Mesa は winsys の page）。memory type は p009 の 2 つ（D7 (a)）のどちらでも可。
- depth: `D32_SFLOAT`・`D16_UNORM` を optimal で。depth は ISP の内部の buffer で持ち、load・store は ZLS（§4.3）。stencil の format は p011。
- **memory の見積もり**（review 1 の Minor 12）: 1080p の OPTIMAL の swapchain の image は 2048×2048×4 = 16 MiB／枚で、session の resource は全部 4 GiB の下の物理で連続の確保（`render-private.h:35-48`）。p010 の vkdemo（320×240 → 512×256×4 = 512 KiB）では小さい。p010d で FW の heap（16 MiB、`mips-table.h:41`）に入る物（4 つの context・free list の object・HWRT の FW の部分）を数えて phase.md に表を作る。1080p の表は p011 の前（p011 の phase.md の必須項目）。

### 3.2 format の表

最小の表（p010c）。各行に PBE の pack の format、TPU の texture の format、swizzle を持つ（Value(s) referenced: M2 `pvr_formats.c`・`pvr_arch_formats.c`、M1 `texstate.xml`・`pbestate.xml`）:

| Vulkan の format | optimal の features | linear の features |
| --- | --- | --- |
| R8G8B8A8_UNORM・B8G8R8A8_UNORM | COLOR_ATTACHMENT・BLEND・SAMPLED・SAMPLED_FILTER_LINEAR・TRANSFER_SRC/DST・BLIT_SRC/DST | 同じ（WS277 R7 の「linear の SAMPLED」）。**ただし p010l の前は linear の SAMPLED（と SAMPLED_FILTER_LINEAR）だけを出さない**。COLOR_ATTACHMENT・TRANSFER・BLIT は出す（§7、M8、review 2 の Minor 2: libvulkan が見るのは linear の SAMPLED だけで、全部 0 だと gpu-selftest の段 5 の LINEAR の image が作れない） |
| R8G8B8A8_SRGB・B8G8R8A8_SRGB | 同上 | 同上 |
| R8_UNORM・R8G8_UNORM | SAMPLED・TRANSFER・BLIT | TRANSFER（p010f から） |
| D32_SFLOAT・D16_UNORM | DEPTH_STENCIL_ATTACHMENT・TRANSFER_SRC/DST | なし |

buffer の features（vertex の属性）: `R32G32B32_SFLOAT`（vkdemo）・`R32G32_SFLOAT`・`R32G32B32A32_SFLOAT`・`R8G8B8A8_UNORM`（review 1 の Minor 3）。表に無い format は features 0 で、image の作成は `VK_ERROR_FORMAT_NOT_SUPPORTED`。

### 3.3 image view・sampler

- image view: texture state の word（事実、p010b: IMAGE_WORD0・1 の 2 × 64 bit、M1 `texstate.xml`）を view の作成の時に組んで持つ（descriptor の update で写す）。**LINEAR の image の view は `STRIDE_IMAGE_WORD1` の形**（stride を持つ、M1 `texstate.xml`、review 1 の Minor 4）、twiddled は IMAGE_WORD1。PBE の state は render pass の begin の時に組む。
- sampler: sampler の word（事実、p010b: SAMPLER_WORD0・1 の 2 × 64 bit）を作成の時に組む。filter NEAREST・LINEAR、mipmap mode、address mode REPEAT・CLAMP_TO_EDGE・MIRRORED_REPEAT。**CLAMP_TO_BORDER**（Vulkan の core の値）は作成を成功させ、border の色は透明の黒に固定する（他の border の色は無視して kernel の log に 1 回、Keiland が使えば p011）。anisotropy なし、compare なし。

## 4. render（TA と 3D）

### 4.1 FW の object（mainline の kernel の役）

Procedure・Value(s) referenced: M3（`pvr_context.c`・`pvr_free_list.c`・`pvr_hwrt.c`・`pvr_ccb.c`・`pvr_vm.c`・`pvr_queue.c`・`pvr_cccb.c`・`pvr_rogue_fwif.h`・`pvr_rogue_fwif_shared.h`・`pvr_rogue_heap_config.h`・`pvr_drm.h`）、M2（`pvr_arch_job_context.c`・`pvr_arch_job_render.c`・`pvr_rt_dataset.c`・`pvr_free_list.c`）。

**GPU の VA の heap**（review 1 の M2）: 今の session の VA は GENERAL の heap だけ（`render.c:587`）で、PDS・USC の program は executor の固定の場所（`vk-queue.c:56-67`）にある。p010d で mainline の heap の配置（事実、p010b: GENERAL 0x80_0000_0000、PDSCODEDATA 0xDA_0000_0000、RGNHDR 0xDB_F000_0000（256 MiB）、USCCODE 0xE0_0000_0000、TRANSFER_FRAG 0xE4_0000_0000。M3 `pvr_rogue_heap_config.h`）に合わせて session の VA を分ける:

- PDSCODEDATA・USCCODE の heap の中に、pipeline・固定の program ごとに置く**割り当ての仕組み**（session ごとの単純な bump と free の表）。PDS・USC の program は今すでに heap の base＋0x10000 にあり（`vk-queue.c:63-67`、`interface.h:105-106`）、sysinit の `pds_exec_base`・`usc_exec_base` も一致している（`firmware-boot.c:761-762`、review 2 の Minor 6、確かめ済み）。p009 の固定の場所はこの割り当ての最初の区画にする（p010d は p009 の program の置き場所を変えるので O-P009 に依る、Minor 7）。
- **static data area**（review 2 の M-4。Value(s) referenced: M3 `pvr_vm.c` の static data area の offset、`pvr_drm.h` の heap の注。Procedure referenced: M2 `winsys/pvr_winsys_helper.c` の static data の設定）: 各 VM の heap の先頭の固定の区画を mainline・Mesa と同じに作る。
  - GENERAL+0: MCU の fence（uapi の注で「valid な物理 memory を指すこと」）。GENERAL+128: YUV の係数（p010 では 0 で埋める。YUV は使わない）
  - PDSCODEDATA+0: VDM sync の PDS（DOUTU で下の USC を起こす）。PDSCODEDATA+128: EOT の static の pixel event の PDS
  - USCCODE+0: VDM sync の USC（Mesa の usclib の `vs_nop_common`、命令は nop と終わりだけ）
  - 各 heap の割り当ては、区画の次の page から始める（mainline の reserved size と同じ）。今の session の GENERAL の割り当ては base+0 から始まっている（`render.c:587`）ので、**p010d で次の page からに直す**。FW か硬が使うかは推測（Mesa の他の code は VDM sync を参照しない）。cache の flush の MCU の fence が session の最初の resource を上書きする危険があり、**p009 の実機の compute にも関わり得る**（§11、Q1 へ。修正は [BUG-342](../bugs/BUG-342.md) で別に進んでいる。p010d は heap の割り当ての側をこの節の形に揃え、BUG-342 の修正と食い違わないことを確かめる）。
- RGNHDR: BXE（BRN63142 なし）では mainline が RGNHDR の heap を size 0 で返し、範囲の検索からも外す。Mesa はその時、region header を **GENERAL の heap** に置く（事実、review 2 の Minor 1: M3 `pvr_vm.c`、M2 `winsys/powervr/pvr_drm.c`）。zedBSD も GENERAL に置き、RGNHDR の heap は作らない（版 2 の「無条件に使う」は誤り）。
- TRANSFER_FRAG の heap: transfer の IPF の stream を置く（§6）。

**FW の context**（review 1 の M3）: session の FW の context は compute（p006）・geometry・fragment・transfer の **4 つ**。

- render context（geometry と fragment の 2 本の CCB と FW の render context の object）は **session の最初の render で作る**（compute だけの session に払わせない）。transfer context は最初の transfer で作る。
- render context の **static state**（`static_rendercontext_state` の `geom_registers_caswitch`: VDM の context の state の base・resume、TA の context の state の buffer、store・resume の task 0〜4 × 2）は mainline の kernel が必須にする物（事実、p010b、長さ 0 は -EINVAL）。**Mesa の形で作る**: VDM・TA の context の state の buffer を確保し、Mesa の SR（store・resume）と PT の PDS・USC の program と同じ役の program を driver の固定の program（§5.4）として置いて address を書く（Procedure referenced: M2 `pvr_arch_job_context.c` の render context の作成）。0 のまま出して FW の context switch が address 0 を走らせる危険を避ける（Q1 の判断）。compute の context の CDM の cswitch の state（p006 は共通の部分だけ書いて残りは 0、`render-queue.c:1084-1093`）も、p010n で Mesa の形と比べて足りない物を足す。transfer context は static state なし（長さ 0 でないと mainline は -EINVAL、事実）。
- **identity は FW の context ごと**（4 つ。geometry と fragment は mainline と同じく render の context の 1 つの id で、`dm` で分ける。M3 `pvr_context.c`）。FW の通知の扱い（review 2 の M-5。Value(s) referenced: M3 `pvr_rogue_fwif.h` の FWCCB の command と `context_reset_data`）:
  - **105**（`ROGUE_FWIF_FWCCB_CMD_CONTEXT_RESET_NOTIFICATION`）の `context_reset_data` が context の GPU の fault と reset を運ぶ: `reset_reason`、`dm`（どの data master、段への割り当てに使う）、`flags`（`FLAG_PF` bit 0: `fault_address` が有効、`FLAG_ALL_CTXS` bit 1: 全部の context が対象）、`fault_address`。段の行の `fault <va>` は `FLAG_PF` の時の `fault_address` から、`reset <理由>` は `reset_reason` から取る。`FLAG_ALL_CTXS` の時は session の全部の context（と、他の session の context）を対象にする。
  - **112**（`ROGUE_FWIF_FWCCB_CMD_CONTEXT_FW_PF_NOTIFICATION`）は **FW 自身**の page fault（中身は `fw_fault_addr` だけ）で、context の fault ではない。今と同じく device lost として扱う（`render-queue.c:1506-1509`）。段の行は出さず、既存の device lost の行を残す。`interface.h` の名前 `POWERVR_NOTICE_PAGE_FAULT` は p010n で実体に合う名前に直す。
- recovery（design.md §3.5）: stop・stop_poll・isolate・cleanup は session の**全部の** FW の context を対象にする。HWRT data set と free list は FW の CLEANUP（`ROGUE_FWIF_CLEANUP_HWRTDATA`・`ROGUE_FWIF_CLEANUP_FREELIST`、Procedure referenced: M3 `pvr_hwrt.c`・`pvr_free_list.c`）で壊してから memory を返す。**busy の再試行には上限を付け**（回数と時間、値は p010d の phase.md）、超えたら isolate に倒して memory は core に残す（review 2 の M-1）。
- **kick の cleanup ctl**（review 2 の M-3 (d)。Procedure referenced: M3 `pvr_cccb.c` の command の header）: HWRT を使う geometry・fragment の kick は、`hwrtdata.cleanup_state` の FW の address を command の `cleanup_ctl_fw_addr` に付ける（FRAG_PR だけは付けない）。今の `POWERVR_KICK_CLEANUPS = 0`（`render-queue.c:1189`）は compute の道のまま残し、render の kick で数を付ける。付けないと CLEANUP が使用中の HWRT を「済み」と答えるか busy のまま止まる（推測）。p010d が HWRT の `cleanup_state` を作り、p010n が kick に付ける。

**free list**（B3・M2、D-P1 を直した形）:

- HWRT は **local と global の 2 つ**の free list を取る（事実、p010b、`PVR_DRM_HWRT_FREE_LIST_LOCAL`・`GLOBAL`）。global は session に 1 つ（最初の render で作る、固定 16 MiB、D-P1）、local は HWRT data set ごとに mainline の最小の page 数以上（`free_list_min_pages` は roguexe で BRN66011 が無ければ **25**、BXE の quirks に 66011 は無い。事実、review 2 の Minor 5、M3 `pvr_free_list.c`）。FW の free list の object と page の表は kernel が作る。
- **grow はしないが、FW の要求には必ず応える**: FW の `FREELIST_GROW` の通知には `KCCB_CMD_FREELIST_GROW_UPDATE` を `delta_pages = 0` で返す。値は mainline の形（review 2 の Minor 4、M3 `pvr_free_list.c` の grow の応答）: FW は ready の page を使い切った後なので `ready_pages = 0`、`new_pages` は今の current の page 数＋ready の page 数（増やさない）。HWR の後の `FREELISTS_RECONSTRUCTION` には各 free list を作り直して `FREELISTS_RECONSTRUCTION_UPDATE` を返し、HWRT の state を HWR の値にする（Procedure referenced: M3 `pvr_free_list.c`・`pvr_ccb.c`）。どちらも mainline と同じ形（事実）。
- 尽きた時: 0 の grow の後は FW が partial render（FRAG_PR、§4.2）を走らせる（Mesa・mainline の形）。SPM の load・store の program が無いので、depth は失われ得る（推測。color は下の `pds_pr_bgnd` で読み戻す）。kernel は GROW の通知を受けた事と、FWCCB の UPDATE_STATS（`NUM_PARTIAL_RENDERS`・`NUM_OUT_OF_MEMORY`、M3 `pvr_rogue_fwif.h`）の増えを印にし（grow 0・ready 0 では FW が GROW を送らずに PR に入る可能性がある、推測、review 2 の Minor 4）、その render の完了の後に段の行 `ta: fail(free list)` を出す（render 自体は完了させ、時間切れにしない）。単一 sample の color は `pds_pr_bgnd`（§4.2）で PR の後も正しい画になり得る（推測）。SPM は p011（D-P1）。

**HWRT data set**（M2）:

- framebuffer の大きさと sample の数ごとに 1 つ。session の中で「幅・高さ・layer」を key に最大 4 個を cache（vkdemo・WSI の窓は 1〜2 種）。
- 中身: **rt_data の 2 組**（double buffer）、各組に PM の MLIST・macrotile の配列・region header（GENERAL の heap に 2 組分まとめて（上の RGNHDR）、alignment は M1 `cr.xml` の TE の PSG region の base の alignment）。ほかに TPC・VHEAP の表・RTC、FW の hwrtdata(_common)・rta_ctl（Value(s) referenced: M3 `pvr_drm.h` の hwrt の create の args、`pvr_rogue_fwif.h`。Procedure referenced: M2 `pvr_arch_job_render.c`・`pvr_rt_dataset.c` の rt dataset の作成）。macrotile・tile の設定・TE・ISP の screen の値も同じ手順。tile は 16×16（p002 の feature）。
- **時間切れの後**（review 2 の M-1、版 2 の「作り直す」は誤り）: session は abandoned で以後の submit は全部 EIO（`vk-queue.c:285-286`）、次の render は無い。HWRT data set と free list は executor の memory と同じく core に残し（`vk-queue.c:22-25`・`208-216`）、isolate・device の reset の後に返す。止まった context に CLEANUP を送り続けない。
- **HWR の後**: mainline と同じく `FREELISTS_RECONSTRUCTION` の応答だけ（free list を作り直し、HWRT の state を `RTDATA_STATE_HWR` にする）。HWRT は作り直さない。

### 4.2 1 つの render の job

- 1 つの render pass の instance（subpass 1 つ、§4.3）= 1 つの render job = **geometry の kick + FRAG_PR の kick + fragment の kick**（Mesa と同じく **FRAG_PR を常に積む**。事実、p010b: Mesa `pvr_drm_job_render.c` の手順、kernel は FRAG_PR の有無を検査しない。FRAG_PR が無い時の FW は未確認なので避ける）。kick の組み方は mainline・Mesa と同じ形にする（review 2 の M-3。Procedure referenced: M3 `pvr_job.c`・`pvr_queue.c`・`pvr_cccb.c`、M2 `pvr_arch_job_render.c`。Value(s) referenced: M3 `pvr_rogue_fwif.h`・`pvr_rogue_fwif_client.h`）:
  - **combined kick**: geometry と FRAG_PR を対にし、KCCB の `ROGUE_FWIF_KCCB_CMD_COMBINED_GEOM_FRAG_KICK`（117）で 1 回で kick する。本番の fragment はその後の KICK（101）。今の zedBSD は KICK 101 だけ（`render-queue.c:1185-1189`）なので、p010n で 117 を足す。
  - **待ちは全部 `CCB_CMD_TYPE_FENCE_PR`（215）**で書き、本番の fragment は geometry の完了を FENCE_PR の最後に置く。
  - **`partial_render_geom_frag_fence`**（版 2 の「FRAG_PR と結ぶ」は誤り）: geometry の command のこの fence には、**geometry の queue の timeline の UFO** の address と「その geometry の seqno − 1」を入れる（mainline の形）。
  - **FRAG_PR の register**: Mesa の形で、本番の fragment と同じ値から、PR 用の pbe の word と pixel event だけを書き換える。SPM の program は無いので、**FRAG_PR の EOT（pixel event の PDS と USC）は本番と同じ物**を使う。
  - **`pds_pr_bgnd[3]`**（fragment の reg、M3 `pvr_rogue_fwif_client.h`）: 本番の fragment に必ず入れる。中身は §5.4 の「attachment の load」の background の PDS（PR の後の本番が tile の途中の色を読み戻す）。0 のままにすると、PR の後の本番が address 0 の PDS を走らせ fault になる（推測）。これで単一 sample の color は SPM なしでも正しい画になり得る（推測）。depth は PR で失われ得る（D-P1 のとおり `ta: fail(free list)` で分かる）。
  - fragment は geometry の完了を FW の中で待つ（M3 `pvr_job.c` の geom と frag の link）。
- draw の 0 個の render pass（clear だけ）も geometry の空の stream（終わりの word だけ）で出す（Mesa の empty geometry の手順、p010n で M2 から確かめる。未確認）。
- geometry の command: VDM の control stream の address、TA の state（PPP の screen・TE の値）。fragment の command: ISP の background object（clear）、`pds_pr_bgnd`（上）、**PBE の reg の word**（`pbe_word[8][3]`、clip を含む、§4.3）と **pixel event の PDS の info・data**（`event_pixel_pds_info/data`、EOT、§4.3）、ZLS（depth の load・store）、tile の数（Value(s) referenced: M3 `pvr_rogue_fwif_client.h` の frag_regs・cmd_frag。Procedure referenced: M2 `pvr_arch_job_render.c`）。FW の struct は kernel が自分の型で書き、**offset を mainline の `pvr_rogue_fwif_client_check.h`・`pvr_rogue_fwif_check.h` の値と static assert で照らす**（M9）。

### 4.3 render pass・attachment・clear・load/store・EOT

- `vkCreateRenderPass`（と 2）: attachment ≤ 4 color + 1 depth、subpass は 1 つだけ。2 つ以上は内部では「未対応」で、Vulkan の値は **`VK_ERROR_OUT_OF_HOST_MEMORY`**（仕様で `vkCreateRenderPass` が返せる失敗は OUT_OF_HOST・OUT_OF_DEVICE_MEMORY だけ。review 1 の Minor 9）を返し、kernel の log に 1 行（`powervr: render pass: 2 subpasses not supported`、段の行ではない）。vkdemo は 1 つ。subpass の dependency は記録しない（job の境で全部を待つ）。
- framebuffer: image view の列と幅・高さ。HWRT data set（§4.1）を選ぶ key。
- load op: CLEAR は ISP の background object の clear の値（PDS の program が USC の固定の「色を出す」program を走らせる、Procedure referenced: M2 `rogue/pvr_clear.c`）。LOAD は color なら「attachment を texture として読み tile に出す」background の program（§5.4）、depth なら ZLS の load。DONT_CARE は何もしない。
- store op と **EOT**（review 1 の M1）: color の STORE は tile の終わりに走る **pixel event の PDS の program** と **USC の EOT の program**（PBE の state の word を EMITPIX で出す）で行う。PBE の state は 2 つに分かれる: state の word は EOT の USC の program が出し、reg の word は fragment の command の `pbe_word` に入る（Procedure referenced: M2 `pvr_arch_cmd_buffer.c` の pixel event の program、`pvr_usc.c` の EOT、`pvr_arch_job_render.c`。Value(s) referenced: M1 `pbestate.xml`）。EOT の program は render pass の begin で attachment の列から組む（§5.4）。LINEAR・twiddled の書き出し（R6 (2)）。depth の STORE は ZLS。DONT_CARE は PBE を出さない。
- `vkCmdClearAttachments`（render pass の中の clear）: 全面なら load op の CLEAR と同じ、部分は p011（Mesa の `pvr_uscgen_clear_attach` の形、tiled の DMA が要る）。
- **PBE の clip は必須**（review 2 の M-2、版 2 の「clip は p011」は誤り）: PBE の reg の word の `minclip_x`（REG_WORD0）・`minclip_y`・`maxclip_x`・`maxclip_y`（REG_WORD1）に、常に「attachment の image の extent ∩ renderArea」を入れる（Value(s) referenced: M1 `pbestate.xml`。Procedure referenced: M2 `pvr_arch_cmd_buffer.c` の PBE の state）。0 のままでは pixel (0,0) しか書かれず、最大値では 16 の倍数でない高さ（1080 など）の LINEAR の image・buffer に tile の端まで書いて隣の memory を壊す。
- **部分の renderArea**: color の attachment なら受ける（clip の外は store されない）。断るのは **depth の STORE がある時だけ**（ZLS は tile 単位で外側を壊し得る）で、その render pass の instance を出さずに捨て、kernel の log に 1 行（`powervr: render pass: partial render area with depth store not supported`）。load op の CLEAR の background は tile 全体に効くが、clip の外は store されないので外側は壊れない。vkdemo・WSI は全面。

### 4.4 graphics の pipeline と draw

- pipeline の作成: driver は pipeline の state から **compile の入力**（§5.1: vertex の属性の format、color attachment の format、blend の設定、sample の数）を作って compiler に vertex と fragment を compile させ、出力を受けて PDS の program（vertex: 属性の DMA と USC の起動、fragment: varying の補間（coefficient の DOUTI）と texture の state・shared register の DMA と USC の起動）を組む（PDS の encoder は p008）。program は §4.1 の PDSCODEDATA・USCCODE の heap に割り当てる。
- 固定の state: input assembly は TRIANGLE_LIST・TRIANGLE_STRIP、primitive restart なし。rasterization: cull・front face・polygon mode FILL だけ、depth bias なし、line width 1。depth: test・write・compare op。**blend は compiler が fragment の末尾で行う**（D-P3 (b)、事実、p010b: Mesa の pco の `nir_lower_blend`）。driver は attachment ごとの factor・op・write mask を compile の入力に渡し、blend 定数は `BLEND_CONSTANTS` の要求（§5.2）で shared register に入れる（`VK_DYNAMIC_STATE_BLEND_CONSTANTS` は値の差し替え）。viewport・scissor は pipeline の固定か dynamic（`VK_DYNAMIC_STATE_VIEWPORT`・`SCISSOR`、p010i で両方）。
- draw: `vkCmdDraw`（index なし）と `vkCmdDrawIndexed`（16・32 bit の index）。VDM の control stream に state の update（PPP の state の block、vertex の PDS、viewport・scissor の表）と index list の word を書く（Value(s) referenced: M1 `vdm.xml`・`ppp.xml`。Procedure referenced: M2 `pvr_arch_cmd_buffer.c` の draw）。instance の数 1 だけ、多いのは p011。
- push constant: 128 byte まで。記録の時に command buffer の memory に写し、PDS がその address を shared register に DMA する（§5.2）。

## 5. compiler と driver の境（P2 の p013・p014 と共有）

**compiler の Phase**（review 1 の M6 (a)、Q1 の判断）: ws.md に次の 2 つを作る（担当 P2。今の q1363 はこの範囲の最初の試行にあたる。Phase と Queue の対応づけは Q1）。

| Phase | 中身 | 名前を付けた出力 |
| --- | --- | --- |
| p013 | USC の encoder（`usc-encode.c`）を §5.4 の表の命令に広げる（SMP・DITR・EMITPIX・WOP・PCK/UPCK・UVSW と SR の shared の burst の LD/ST）。host 試験。実体は P2 の [p010-compiler](phase010-compiler/phase.md) が smp・ditr・emitpix・upck を main に入れた | **O-ENC**: §5.4 の「O-ENC の中身（固定）」 |
| p014 | SPIR-V から USC の compiler（compute の一般、vertex・fragment）。§5.1 の compile の入力を受け、pack と blend を行う。host 試験は glslc の SPIR-V（vkdemo・gpu-selftest の shader） | **O-SHADER-ABI**（p014 の**最初の出力**として切り出す、O-ENC に依らない。review 2 の Minor 8）: `shader.h`（§5.1・§5.2 の型。今は P2 の `shader-compile.h` にあり、改名か移しは Q1・P1・P2 の合意）が main に merge 済み。**O-VSFS**: vkdemo・gpu-selftest の vertex・fragment が compile でき host 試験 PASS で main に merge 済み |

- `usc-encode.c` の持ち主は p013 の担当（P2）。p010 の固定の program（§5.4）が要る命令は p013 に依頼し、p010 の側は encoder を直接変えない（同時の編集を避ける）。
- p010 の Phase の依存は O-ENC・O-SHADER-ABI・O-VSFS で書く（§10）。LW は P2 が p013・p014 の phase.md で見積もる。

### 5.1 compile の入力と出力（1 つの stage ごと）

compiler は次を受ける:

| 入力 | 中身 |
| --- | --- |
| SPIR-V | module、entry point、stage（compute・vertex・fragment） |
| vertex の属性の format | location ごとの Vulkan の format（§3.2 の buffer の表）。format の変換は shader の中（Mesa の pco と同じ、事実、p010b） |
| color attachment の format | attachment ごとの format。compiler が出力を PBE の format へ **pack** する（事実、p010b: Mesa `pco_nir_pvfio.c`） |
| blend の設定 | attachment ごとの enable・src/dst の factor・op（color・alpha）・write mask、logic op なし。blend 定数は §5.2 の `BLEND_CONSTANTS` |
| sample の数 | 1 だけ（MSAA は範囲外）。契約には欄を置く |

compiler は次を返す。driver はこれ以外の compiler の内部を見ない。型の置き場所は `src/drivers/gpu/powervr/shader.h`（p014 で P1・P2 が合意して作る、O-SHADER-ABI。それまで本節が契約）。

| 項目 | 中身 |
| --- | --- |
| stage | compute・vertex・fragment |
| USC の code | bytes と長さ（p008 の `powervr_usc_program_encode` の出力の形）。始まりの offset は 0 |
| temps | 使う temp の数（PDS の task の word の temps の field、p008 の 2 の単位） |
| shared register の要求の表 | §5.2 |
| vertex の入力 | location ごとの (format、shader が読む vertex の入力 register の番号) |
| vertex の出力・fragment の入力 | varying ごとの (location、成分の数、補間（smooth・flat）、出力の register の番号)。vertex は position の register の番号。fragment は「iterated の coefficient の番号」 |
| fragment の出力 | color の attachment ごとの、**pack と blend の済んだ** raw の値の register（EOT の program が PBE に渡す） |
| 性質 | discard の有無、depth の書き、workgroup の大きさ（compute）、shared memory の大きさ |

### 5.2 descriptor と shared register の約束

- descriptor set は GPU の memory の中の 1 つの buffer。binding の並びは set layout の作成で driver が決め、各 binding の offset と 1 つの descriptor の大きさを固定する（事実、p010b。Value(s) referenced: M2 `pvr_common.h`、`pco_data.h`）:
  - storage・uniform buffer: 16 byte（address 64 bit、size 32 bit、offset 32 bit）
  - sampled image: 32 byte（texture state の word 16 byte + compiler が読む meta 4 × 32 bit）
  - sampler: 48 byte（sampler の word 16 byte + meta 16 byte + gather 用 16 byte）
  - combined image sampler: 80 byte（image の後に sampler）
- compiler は shader が使う資源を「shared register の要求の表」で頼み、driver の PDS の data の program がその値を shared register に入れてから USC を起動する。要求の種類:
  1. `SET_BASE(set)`: set の buffer の GPU の address（64 bit、2 register）
  2. `DESCRIPTOR(set, binding, array_index, words)`: その descriptor の word を set の buffer から DMA
  3. `PUSH_CONSTANTS(offset, size)`: push constant の範囲
  4. `LITERAL(value)`: 定数
  5. `WORKGROUP_ID`・`NUM_WORKGROUPS`（compute、p009 の program の形を広げる）
  6. **`BLEND_CONSTANTS`**: 4 つの float（fragment、blend が定数の factor を使う時。Mesa と同じく shared register、事実、p010b）
  各要求は compiler が決めた shared register の番号（先頭から詰める）を持つ。buffer の中身の読み（storage・uniform）は compiler が `SET_BASE` か `DESCRIPTOR` の address から USC の load で読む。
- dynamic offset（`UNIFORM_BUFFER_DYNAMIC`）は p011。

### 5.3 compiler への依頼（P2 へ、Q1 経由、2026-10-20 に伝え済み）

1. §5.1・§5.2 の入力と出力の形（compile の入力、shared register の要求の表と `BLEND_CONSTANTS`、varying の表）。
2. **D-P3 (b)**: fragment の color の pack と blend は compiler が行う。driver は USC の code に命令を足さない。
3. p013 の encoder の広げ: §5.4 の表で固定した（版 2.1）。P2 は smp・ditr・emitpix・upck を main に入れ済みで、残りは SR の shared の burst の `ld`・`st` の確かめだけ。一覧は各 Phase の phase.md から後で足さない（review 2 の M-6 (c)）。Q1 経由で P2 と合意する。

### 5.4 driver の固定の USC・PDS の program と O-ENC の中身

compiler を通さず、encoder で直に書く物（p009 の `compute-program.c` と同じ置き方で `render-program.c`・`transfer-program.c`）。**命令の種類は Mesa 26.2.5 の同じ役の program から 2026-10-20 に数えた**（review 2 の M-6 (c)。Procedure referenced: M2 `pvr_usc.c` の `pvr_usc_eot`・`pvr_uscgen_tq`（`_frag_coords`・`_frag_load`・`_frag_pack`）・`pvr_uscgen_loadop`、`rogue/pvr_clear.c`、`pvr_arch_job_context.c` の SR・PT、`winsys/pvr_winsys_helper.c` の static data、`pco/usclib/common.cl` の `vs_nop_common`・`load_store_sr`）。Mesa は NIR の intrinsic で書き pco で落とすので、下の列は pco の命令の種類に直した物。右の列は 2026-10-20 の main の `usc-encode.h`（P2 の [p010-compiler](phase010-compiler/phase.md) が smp・ditr・emitpix・upck を足した後）の関数。

| program（使う Phase） | Mesa の出所 | USC の命令の種類 | PDS | 今の encoder |
| --- | --- | --- | --- | --- |
| VDM sync（static data area、p010d） | `common.cl` `vs_nop_common`、`pvr_winsys_helper.c` | nop、終わり | DOUTU | `powervr_usc_nothing`・`empty`、PDS の `output` で足りる |
| SR の store・load（render context、p010n） | `common.cl` `load_store_sr`（store は 1024 語、load は 256 語の burst） | vertex input の読み（operand）、special register の読み（shared register の確保の大きさ、movs1）、`ld`・`st`（1 語）、**shared register を宛先・元にする `ld`・`st` の burst**、add6432（address の足し）、imad か shift（×16）、tst・cnd・br（loop） | DOUTU と data の DOUTW/DOUTD（SR の address） | 1 つを除き有る（`load`・`store`・`move`・`integer`・`bit_*`・`test`・`conditional`・`branch`）。**shared の bank を宛先・元にし burst を source から取る `ld`・`st`（256・1024 語）が今の `powervr_usc_load/store` で書けるかは未確認**（burst の即値は 16 まで、`burst_source` の形がそれか） |
| PT の store・resume（p010n） | `pvr_arch_job_context.c` の PT の PDS | なし（PDS だけ） | persistent temp の DMA（DOUTD・DOUTW） | PDS の `dma_*`・`output` |
| EOT（p010e。FRAG_PR も同じ物、§4.2） | `pvr_usc_eot` | state の 2 語を shared register から（operand）か即値（movi32）、`emitpix`（最後に free partition）、emit の間に `wop` | pixel event の PDS（DOUTU、state の語を DOUTW で shared へ）と static の EOT の PDS（PDSCODEDATA+128） | `emit_pixel`・`bit_immediate`・`empty`（wop） |
| background: clear の色（p010e） | `pvr_uscgen_loadop`（clear の値を shared register から） | shared register から pixel output register への `mov`、`nir_dummy_load_store_pco` の dummy の `ld`・`st` | DOUTW（色）、DOUTU | `move`・`load`・`store` |
| background: attachment の load と `pds_pr_bgnd`（p010e） | `pvr_uscgen_loadop`・`_frag_load` | `ditr`（座標）、`smp`（texture の state を shared から）、`mov`（pixout へ）、sample id の special register の読み | DOUTD（texture・sampler の state）、DOUTU | `iterate`・`sample`・`move` |
| transfer の copy・clear（p010f） | `pvr_uscgen_tq`、`_frag_coords`・`_frag_load`・`_frag_pack` | `ditr`、fmad（座標の scale・offset）、`smp`、`mov`（pixout）。同じ format の copy と clear は raw のまま（pack なし）。format の変換（p010o の blit の一部）は `pck`・`upck`、shift・and・or | DOUTD・DOUTW、DOUTU | `iterate`・`float_multiply_add`・`sample`・`move`・`pack`・`unpack`・`bit_*` |
| 試験の三角形（p010e、§8.3） | `common.cl` `vs_passthrough_common`（vertex） | vertex: vertex input の読み、`uvsw.write`、`uvsw.emit.endtask`。fragment: 即値を pixout へ（movi32・mov） | DOUTU、vertex の属性の DMA | `vertex_output`・`bit_immediate`・`move` |

- 範囲外（p011）: `pvr_uscgen_clear_attach`（部分の `vkCmdClearAttachments`、tiled の offset・savmsk・`dma_st_tiled`）、SPM の load・store、MSAA の resolve。
- **O-ENC の中身（固定）**: 上の表の「今の encoder」の列の関数が main に merge 済みで host 試験 PASS であること（2026-10-20 の main で SR の 1 点以外は満たしている、P2 の p010-compiler の encode の host 試験 91/0・group 20/0）、**加えて SR の shared register の burst の `ld`・`st` を host 試験で確かめる（書けなければ encoder に足す）**。これより広い命令を O-ENC の完了の条件に後から足さない。fitr・fitrp（backend の形）は BXE では ditr を使うので要らない（p010-compiler の残り 3）。
- **O-ENC の持ち主と暫定**: 持ち主は P2（`usc-encode.c`、p010-compiler の範囲。ws.md の p013 の行はこの範囲を指す。行の付け替えは Q1）。P2 と Q1 がこの一覧に合意するまで、p013 の O-ENC の完了の条件は「暫定」とする（review 2 の結論 5）。
- 今の encoder で書ける物（VDM sync・PT・EOT・clear・load・transfer・三角形）は O-ENC を待たずに書く。SR だけが上の 1 点を待つ。

## 6. transfer（TQ）

- FW の transfer context（M3 の transfer の command）の job で行う。BXE に TLA は無い（事実、p010b）ので全部 ISP・PBE の道。
- **形**（review 1 の M4、事実、p010b。Procedure referenced: M2 `pvr_arch_job_transfer.c`）:
  - IPF の control stream（region の配列と primitive の block、Value(s) referenced: M1 `ipf.xml`）を **TRANSFER_FRAG の heap** に置き、stream の address と `CR_ISP_MTILE_BASE` を **heap の base からの相対**で書く（Mesa と同じ。base を FW か硬が持つかは未確認なので、kernel は heap を mainline の位置 0xE4_0000_0000 に置き、相対の値だけを書く）。
  - `CR_ISP_RENDER` の mode は copy・clear で FAST_2D、拡大縮小の blit で FAST_SCALE。
  - PBE の word は transfer の register の `pbe_wordx_mrty[9]` に（M3 `pvr_rogue_fwif_client.h` の transfer_regs）。**clip は常に dst の矩形**（§4.3 と同じ理由、review 2 の M-2）。
  - **奇数の row length**（review 2 の Minor 3）: PBE の linestride は 2 pixel 単位で、`bufferRowLength` 0（詰め）の奇数幅は表せない（Mesa は `double_stride` で回避、M2 `pvr_arch_job_transfer.c`）。p010f は奇数の幅の buffer を **1 行ずつの job に分ける**（遅いが正しい）。`double_stride` の形は p011。p010 の readback は display の全画面（偶数）で当たらない。
  - **transfer の context の recovery と sync の word**（review 2 の M-6 (b)）: transfer の context を作る p010f が、その identity（§4.1）・recovery（stop・isolate・cleanup の対象に足す）・kick の sync の word（§2、`transfer: fail(timeout)` の元）も作る。p010n と同じ形（4 つの context の表）に p010f が transfer の行を足す。p010f は O-p010n に依る（形を共有するため）。
  - 読みは TPU（source の texture state を組む）、書き出しは PBE（LINEAR の image と buffer には `MEMLAYOUT_LINEAR`、R6 (2)）。program は §5.4 の固定の物。
- 受ける命令と Phase:
  - **p010f**（copy・clear）: `vkCmdCopyBufferToImage`・`vkCmdCopyImageToBuffer`（1 段・1 層ずつ。OPTIMAL の twiddled の読み書きを含む: readback の道の要、§7）、`vkCmdCopyImage`（同じ format）、`vkCmdClearColorImage`。`vkCmdCopyBuffer`・`vkCmdFillBuffer`・`vkCmdUpdateBuffer` は compute の固定の program（p008 の load・store）で。
  - **p010o**（blit）: `vkCmdBlitImage`（NEAREST・LINEAR、拡大縮小、同じ format か表の中の 8 bit の RGBA の間）。shared の道の `present_copy_shared` が使うので p010l の前。
- 1 つの transfer の命令 = 1 つの transfer job（矩形が上限を超えたら分ける）。

- **版 2.2 の注記（2026-10-20、Q1 が採用、P2 q1381）**: p010f の前半（[phase010f](phase010f/phase.md)）で Mesa 26.2.5 の `pvr_arch_job_transfer.c` を読んだ結果、上の「形」を次のとおり改める。
  - 普通の copy・clear（source 1 つ、矩形 1 つ、custom mapping なし）は **FAST_SCALE と ISP の背景の object**（`pvr_3d_copy_blit_core`）。copy は背景の object（`CR_PDS_BGRND*`、`ISP_BGOBJVALS.enablebgtag`）が転送の program を全 tile に走らせ、clear は object なしで `USC_CLEAR_REGISTER` の値を PBE が書く。blit（p010o）も同じ道で scale と filter を足す。
  - **IPF の control stream と FAST_2D は clip blit の道（`pvr_3d_clip_blit`）だけ**（custom mapping・`double_stride`・複数の mapping・DS merge）。p010f・p010o では使わない（`isp_mtile_base`・`isp_rgn` は 0、TRANSFER_FRAG の heap は今は使わない）。
  - 奇数の row length は上のとおり **1 行ずつの job**（`powervr_transfer_needs_rows`）。
  - EOT は transfer 専用の DOUTW の program でなく、**p010e の即値の EOT**（`powervr_render_eot_usc`）と fixed area の +128 の code を使う。

## 7. WSI と display（libvulkan の 2 つの道）

| 道 | 要る物 | Phase |
| --- | --- | --- |
| readback（copy） | **OPTIMAL の swapchain の image**（twiddled）への描画と、そこから host visible の buffer への `vkCmdCopyImageToBuffer`（transfer の twiddled の読み、p010f）、map、semaphore・fence。libvulkan が CPU で display の node に present する。LINEAR の image は使わない | p010k1（clear だけ）・p010k2（vkdemo） |
| shared（zero copy に近い） | image の作成の external の chain（`EXTERNAL_MEMORY_IMAGE_CREATE_INFO`）、`EXPORT_MEMORY_ALLOCATE_INFO`（p009 で ENOTSUP）、`GPU_OP_RECORD_IMPORT_RESOURCE`（同）、`GET_PHYSICAL_DEVICE_PROPERTIES2` の identity、linear の features、`present_copy_shared` の clear・blit・copy を LINEAR の PBE で、display の node の `scanout` の import（p007、p006 の lookup で retain、uncached の view から CPU の copy）。WS277 の R6・R7 を満たした後に WS277 が `stride_alignment = 32` を出す | p010l |

- **p010l までは shared の道に入らせない**（review 1 の M8）: p010c〜p010k2 の間は、(1) linear の SAMPLED だけを出さない（libvulkan は linear の SAMPLED が無いと shared の道を試さない、`wsi-image.c:51-58`。TRANSFER・COLOR_ATTACHMENT は出す、review 2 の Minor 2）、(2) external の chain・export・import は必ず ENOTSUP（→ `FEATURE_NOT_PRESENT`）で拒む。この 2 つを p010c の host 試験に入れる（Venus の frame を通して、返る VkResult が `FEATURE_NOT_PRESENT` か `FORMAT_NOT_SUPPORTED` であること）。p010l で両方を開ける。
- 順: readback を先に（vkdemo が画面に出る最短の道）。**p010k1** は compiler なしで通す: transfer の `vkCmdClearColorImage` を OPTIMAL の swapchain の image へ → readback → present で、単色の画面（gpu-selftest の `present-clear` の段、§8.2）。**p010k2** は vkdemo の画。
- **dc8200 の zero copy**（review 1 の Minor 13）: `dc8200.zerocopy=test` の道（`display.c:39-53`）は display の node が import した buffer を直に scanout する道。p010l で、export した image が SLC_BYPASS で確保されること（p006 の R3）と、libvulkan の loader が渡す pitch と image の rowPitch（§3.1）が一致することを host 試験に入れ、実機の文面に `dc8200.zerocopy=test` の場合を足す。
- p010l は p010 の受け入れに**必須**（review 1 の M6 (c): design.md §6 の p007「import の試験は p010」と WS277 R6）。
- capset は p009 の 1.3.269・flags 7 のまま（WS277 R7 の文面の食い違いは p009 で Q1 に報告済み）。

## 8. UAT の進み具合の行（ユーザーの要望）

### 8.1 kernel の段の行

- 形: `powervr: <段>: ok` か `powervr: <段>: fail(<理由>)`。画面の console（`kern_logf`）に 1 行。段の名前は console と userland への報告で同じ 1 語（`render-node`、review 1 の Minor 8。p010a の code を 2026-10-20 に揃えた）。
- 段（**出る順は決めない**: 各段は自分の最初の完了・失敗で出る。review 1 の M7）:

| 段 | ok を出す時 | fail の理由の例 |
| --- | --- | --- |
| `firmware` | FW の起動と health の kick の応答（p005） | `enoent`・`timeout`・`bad image` |
| `render-node` | render の node の登録（p006） | `ebusy`・`coherency` |
| `compute` | 起動後に最初の compute の job の fence が届いた時 | `timeout`・`fault <va>`・`reset <理由>`・`spirv subset` |
| `ta` | 最初の geometry の kick の完了 | `timeout`・`fault <va>`・`reset <理由>`・`free list` |
| `3d` | 最初の fragment の kick の完了 | `timeout`・`fault <va>`・`reset <理由>` |
| `transfer` | 最初の transfer の job の完了 | `timeout`・`fault <va>`・`reset <理由>`・`format` |
| `present` | display の node の最初の present（copy か import） | `import`・`stride`・`claim` |

- **どこで止まったか**（M7）: 時間切れの時は §2 の kick ごとの sync の word で、終わっていない最初の kick の段に `timeout` を付ける（geometry が終わって fragment が止まれば `ta` は ok、`3d: fail(timeout)`）。FW の通知は §4.1 の context ごとの identity で段に割り当て、理由に **FW の reset の理由**（FW の enum を短い語に、例 `reset lockup`）か **fault の VA**（page fault の通知の VA、例 `fault 0x80001234`）を付ける。serial を使わない UAT で唯一の手がかりなので、理由は enum の固定の語と 16 進の値に限る。p010a の理由の表（`stage.c`）に値付きの形を p010n で足す。
- 各段の ok は起動ごとに 1 回だけ。fail は段ごとに最初の 3 回まで出し、4 回目に `powervr: <段>: further failures not shown` を 1 回。
- **recovery の後**（Minor 8）: HWR・session の reset の後も ok は出し直さない（起動ごとに 1 回）。reset は fail の行（`reset <理由>`）で分かる。
- 既存の詳しい行（`BXE ... ALIAS OK`、coherency の行、`compute kernel: OK` 等）は残す。段の行は「どこまで来たか」の要約。
- **userland への報告**（B1、D-P6）: p010a で read-only の sysctl `hw.gpu.powervr.stages`（`include/uapi/sysctl.h` の HW_GPU_STAGES 11、`src/kern/sysctl.c`）が main に入った（例 `firmware=ok render-node=ok compute=ok ta=- 3d=- transfer=- present=-`、fail は `ta=fail:timeout`）。これは design.md §2「新しい UAPI は作らない」に反する新しい UAPI で、**D-P6 の結果待ち**。推奨は「試験の段の報告だけの読み取りの leaf として残す」。**外す時**は、gpu-selftest の段 1 は sysctl をやめて既存の `kern.msgbuf`（`sysctl.c:59`）から `powervr: ` の行を拾って写す（gpu-selftest の `-s FILE` の道で host 試験を同じに保てる）。HAL の変更ではない。
- 置き場所: `src/drivers/gpu/powervr/stage.c`・`stage-log.c`・`stage.h`（p010a、実装済み）。各段の既存の成功・失敗の道から呼ぶ（呼ぶ所は各 Phase の phase.md に列挙）。

### 8.2 userland の試験 program `gpu-selftest`

- 置き場所: `userland/tests/gpu-selftest/`。VF2 の試験の image（`plan/ws269/tests/p010/config-visionfive2-selftest.mk`）に入れる。通常の image には入れない（D-P2）。
- 出力: 1 段 1 行、`gpu-selftest: <段>: ok` / `fail(<理由>)` / `skip(<前の段>)`、最後に `gpu-selftest: N ok, M fail, K skip`。
- 各段は fork した子で、**子はそれぞれ自分の VkInstance・device を作る**（親は Vulkan を開かない。Minor 8）。上限 10 秒（present は 30 秒）、時間内に終わらなければ kill して `fail(timeout)`。
- 段（順に）:
  1. `kernel`: 段の報告を読み、その中身を 1 行で写す（D-P6 の結果で sysctl か `kern.msgbuf`）
  2. `node`: render の node を開ける
  3. `instance`・`device`: vkCreateInstance・device の作成（PowerVR の physical device を選ぶ）
  4. `compute`: vkcompute と同じ fill、読み返して比べる
  5. `image`: LINEAR と OPTIMAL の image の作成、LINEAR の `rowPitch % 32 == 0`、p010l の前は external の chain が `FEATURE_NOT_PRESENT`
  6. `clear`: `vkCmdClearColorImage`（OPTIMAL）→ `vkCmdCopyImageToBuffer` → 読み返して比べる（transfer）
  7. `copy`: buffer → OPTIMAL の image → buffer の往復（transfer）
  8. `draw`: offscreen の render pass で clear と 1 つの三角形、中心と角の pixel を比べる（TA・3D、compiler）
  9. `sample`: texture を貼った四角、中心の pixel（descriptor・sampler）
  10. `depth`: 2 つの重なる三角形の depth の順（D32）
  11. `present-clear`: 単色の clear を readback の道で 3 秒 present（p010k1、compiler なし）
  12. `present`: `vkdemo --duration=4` を exec し、exit の値（display・WSI）。ユーザーは画面で cuboid を目で見る（Minor 7）
  （版 1 の `frame`（venus-frame）は外した、§1・D-P7）
- 依存の段が fail なら後の段は `skip(<段>)`。独立の段（`clear` が fail でも `draw`）は続ける。
- **display の lease との重なり**（Minor 8）: 試験の image の起動の script は gpu-selftest を **Keiland の起動の前**に流し、終わってから Keiland を起こす（`present-clear`・`present` が display の node を claim するため）。試験の image の config で Keiland を起こさない形も選べる。lease の間に出た kernel の行が release の後に描き直されるか（`display.c:1387-1438`）は**未確認**で、p010k1 の実機の文面で「段の行が画面に残っているか」を見る項目にする。
- shader の SPIR-V は vkdemo と同じく glslc で作って `.spv` を tree に入れ、`regenerate.py` の流儀で出所を書く。
- 段を増やすのは各 p010 の Phase。p010a は 1〜4 と、まだの段を `skip(not built)` で出す形（実装済み）。

### 8.3 kernel の中の試験（Venus を通さない、p008 の流儀）

- `CONFIG_POWERVR_RENDER_CHECK`（試験の image だけ）: 起動の後に kernel が自分で 1 つの render（固定の program で clear の色の background と単色の三角形、64×64 の LINEAR の buffer に PBE で）を出して読み返し、`powervr: render check: OK|FAILED ...` の行と段の `ta`・`3d` の行を出す。userland と libvulkan に依らずに TA・3D・PBE を切り分ける。**clear と三角形の両方を p010e で**（review 2 の M-6 (a): fragment の kick は pixel event の PDS と EOT の USC（EMITPIX）が無いと走らず、clear の background には色を出す USC が要る。どれも p010e の範囲）。p010n は render check の code の骨と host 試験（kick の組み立て）だけを作り、実機の `ta`・`3d`・render check は p010e で確かめる。
- transfer の check（clear と copy）を p010f で足す。

## 9. ユーザーの判断（推奨を採って進める）

| D | 問 | 案 | 推奨と理由 |
| --- | --- | --- | --- |
| D-P1 | free list の大きさと partial render | (a) global 固定 16 MiB・local は最小、grow は delta 0 で応える（FW の要求には必ず応答）、FRAG_PR は積むが SPM の program なし、尽きたら `ta: fail(free list)` / (b) grow と SPM を p010 で | (a)。vkdemo と desktop の小さい場面には足りる（推測）。SPM は大きい（M2 `pvr_spm.c`）。**design.md §3.5 の「FW の grow の要求に応える」を「応答はするが delta 0」に置き換える**（review 1 の Minor 10。design.md の改訂は Q1） |
| D-P2 | gpu-selftest の置き方 | (a) VF2 の試験の image だけに入れ、起動の script が Keiland の前に流す / (b) 通常の image にも / (c) 自動では流さない | (a) |
| D-P3 | blend と color の pack の場所 | (a) driver が fragment の末尾に足す / (b) compiler が全部（compile の入力に attrib・output の format・blend の設定・sample の数、要求の表に `BLEND_CONSTANTS`） | **(b)**（Q1 の判断 2026-10-20、P2 に伝え済み）。Mesa の形と同じ（事実、p010b）。(a) は事実上もう 1 つの backend |
| D-P4 | submit の待ち | (a) ioctl の中で待つ（上限 2 秒）。時間切れの後は session が abandoned になり、HWRT・free list は core に残す（isolate・reset の後に返す）。HWR の後は RECONSTRUCTION の応答だけ（版 2.1、review 2 の M-1） / (b) 非同期 | (a) |
| D-P5 | 段の行の言葉 | (a) 英語の短い語 / (b) 日本語 | (a) |
| D-P6（新） | p010a の sysctl `hw.gpu.powervr.stages`（新しい UAPI、design.md §2 と食い違う） | (a) 試験の段の報告だけの読み取りの leaf として残す / (b) 外し、gpu-selftest は `kern.msgbuf` の `powervr: ` の行を読む | (a)。読み取りだけの文字列で、他の UAPI に影響しない。決まるまで main の物は残す（Q1、2026-10-20） |
| D-P7（新） | p010 の受け入れから venus-frame を外す（design.md §6 の文面の変更） | (a) 受け入れは vkdemo の画と gpu-selftest の `clear`・`copy`・`draw` / (b) venus-frame を 2 つの node に対応させて残す | (a)。venus-frame は VF2 で present できない（render の node に display が無い）、shader も使わない（review 1 の M5）。design.md §6 の改訂は Q1 |

## 10. Phase の分割

LW は粗い（8 LW ≈ 実時間 2〜3 h）。「T1」は QEMU の他の platform の回帰と build（VF2 の GPU は QEMU に無い）、「実機」は VF2 の画面をユーザーが見る（ユーザーが選べる試験の一覧へ、今の試験を頼まない）。

**依存の書き方**（M6 (b)）: p005〜p009 は uncleared（実機待ち）なので、Phase 全体ではなく**名前を付けた出力**に依る:

| 出力 | 中身 |
| --- | --- |
| O-P006 | p006 の MMU・resource・CCB・fence・recovery・render の node が main に merge 済みで host 試験 PASS |
| O-P007 | p007 の display の node と import が main に merge 済みで host 試験 PASS |
| O-P009 | p009 の executor（instance〜submit）と compute の道が main に merge 済みで host 試験 PASS |
| O-ENC・O-SHADER-ABI・O-VSFS | §5 の p013・p014 の出力 |
| O-p010x | その p010 の Phase の code が main に merge 済みで、phase.md の host 試験が PASS（実機の確かめは待たない） |

| Phase | 中身 | 依存（名前を付けた出力） | LW | 確かめ |
| --- | --- | --- | --- | --- |
| p010a | UAT の段の行と gpu-selftest の骨（実装済み、実機待ち） | O-P009 | （済） | 実機: 画面の段の行と gpu-selftest の 1〜4 |
| p010b | 資料の固定（cleared） | p002 | （済） | 文書 |
| p010c | image・format・view・sampler: §3（LINEAR の rowPitch、twiddled の大きさ、LINEAR の STRIDE_IMAGE_WORD1）、format の表（p010l まで linear の SAMPLED だけ出さない）、texture・sampler・PBE の word の組み立て、Venus の image・view・sampler・memory requirements・subresource layout・format の照会。**M8 の host 試験**（external・export・import が `FEATURE_NOT_PRESENT`）。**M9 の照合**（下） | p010b（cleared）、O-P009 | 8 | host 試験、M9 の照合、build |
| p010d | render の FW の object（前半）: heap（PDSCODEDATA・USCCODE の割り当て、TRANSFER_FRAG。region header は GENERAL）、**static data area**（§4.1、割り当ては次の page から、BUG-342 と揃える）、global・local（min 25 page）の free list、HWRT data set（rt_data 2 組、`cleanup_state`）、GROW_UPDATE（delta 0、ready 0）・RECONSTRUCTION の応答、CLEANUP（HWRTDATA・FREELIST、busy の上限で isolate）、時間切れの後は core に残す、FW の heap の見積もりの表、`interface.h` の出典を v7.3 に、p010b の SHA の一覧に `_client_check.h`・`_check.h`・`pvr_ccb.c`・`pvr_fw.c`・heap_config・`pvr_vm.c`・`pvr_queue.c`・`pvr_cccb.c` を足す（必須） | p010b（cleared）、O-P006、O-P009 | 10 | host 試験（FW の struct の offset を check の header の値と static assert、HWRT の値の計算、GROW・RECONSTRUCTION の応答の frame）、build |
| p010n | render の FW の object（後半）: p006 の session を 4 つの context に作り替え、render context の static state と SR・PT の program（Mesa の形）、identity と recovery（105 の `dm`・`FLAG_ALL_CTXS`、112 は device lost）、combined kick（117）と FENCE_PR、`partial_render_geom_frag_fence`、`pds_pr_bgnd`、cleanup ctl、kick ごとの sync の word、空の render（終わりだけの VDM）、段の行の `ta`・`3d` と `reset`・`fault` の理由、render check の骨（§8.3） | O-p010d、O-ENC（SR の 1 点だけ、§5.4） | 14 | host 試験（kick の組み立ての golden、sync の word から段の決まり方、105 の frame から段）、build。**実機は p010e で** |
| p010e | PBE の store と EOT: pixel event の PDS・EOT の USC（static の EOT の PDS を含む）、clear の色と attachment の load の background（`pds_pr_bgnd` の中身）、PBE の clip（extent ∩ renderArea）、LINEAR・twiddled への store、kernel の中の三角形（vertex の USC と PDS、VDM・PPP）、render check の clear と三角形 | O-p010c、O-p010n | 12 | host 試験（VDM・PPP の stream、固定の USC の bytes、PBE の word と clip は M9 の照合）、build。実機: `render check: OK`、`ta`・`3d` の段（p010n の分も） |
| p010f | transfer（copy・clear）: transfer context とその identity・recovery・sync の word（§6）、IPF の stream を TRANSFER_FRAG に、FAST_2D、PBE の clip（dst の矩形）、奇数の幅は 1 行ずつ、固定の program、`CopyBufferToImage`・`CopyImageToBuffer`（twiddled を含む）・`CopyImage`・`ClearColorImage`、buffer の命令は compute、kernel の transfer check、gpu-selftest の `clear`・`copy`、linear の TRANSFER の features | O-p010c、O-p010d、O-p010n | 10 | host 試験（IPF の stream・transfer の register の golden、M9 の照合を IPF に）、build。実機: `transfer` の段、gpu-selftest の `clear`・`copy` |
| p010o | transfer の blit（FAST_SCALE、NEAREST・LINEAR、format の間） | O-p010f | 6 | host 試験、build |
| p010g | render pass・framebuffer・begin/end・load/store・全面の clear attachments・barrier、render の job の列（§2）、部分の renderArea（color は受ける、depth の STORE がある時だけ拒否）、2 subpass の error の値 | O-p010e | 6 | host 試験（render pass の decode と job の列）、build |
| p010h | descriptor と shader の境（O-SHADER-ABI は p014 の最初の出力で、O-ENC を待たない）: `shader.h` の使用、set layout の memory の並び（§5.2 の大きさ）、update、push constant、PDS の data の program（要求の表から、`BLEND_CONSTANTS` を含む） | O-p010c、O-SHADER-ABI | 6〜8 | host 試験（要求の表から PDS の program と set の bytes）、build |
| p010i | graphics の pipeline と draw: compile の入力の組み立て、vertex・fragment の PDS、固定の state、viewport・scissor（dynamic を含む）、VDM の draw（index あり・なし）、gpu-selftest の `draw`・`sample` | O-p010g、O-p010h、O-VSFS | 8 | host 試験（vkdemo の shader の pipeline と draw の stream の golden）、build。実機: gpu-selftest の `draw`・`sample` |
| p010j | depth（D32・D16、ZLS の load・store、test）と vkdemo の offscreen の通し、gpu-selftest の `depth` | O-p010i | 6 | host 試験、build。実機: gpu-selftest の `depth` |
| p010k1 | WSI の readback の道（clear だけ）: semaphore、OPTIMAL の image から buffer、host の map、`present` の段の行、gpu-selftest の `present-clear`、試験の image の起動の順（Keiland の前） | O-p010f、O-P007 | 4 | build。実機: 単色の画面と段の行（lease の後の描き直しも見る） |
| p010k2 | readback の道で vkdemo の画、gpu-selftest の `present` | O-p010k1、O-p010j | 3 | build。実機: vkdemo の画をユーザーが見る |
| p010l | WSI の shared の道: linear の features、export・import の chain、identity、`present_copy_shared` の clear・blit・copy を LINEAR の PBE で、p007 の import の試験、dc8200 の zero copy（SLC_BYPASS と pitch）、WS277 R6 の完了を Q1 へ | O-p010k2、O-p010o | 8 | host 試験（import・export の frame、pitch の一致）、build。実機: vkdemo が shared の道で出る（`dc8200.zerocopy=test` を含む） |
| p010m | 仕上げ: Keiland の起動の待ちの実機の確かめ、T1 に他の platform の回帰（riscv64-virt・amd64 の `boot-test.sh`）を 1 回まとめて依頼、p010 の受け入れ（§1） | O-p010l | 4 | T1: 回帰。実機: gpu-selftest の全部の段と vkdemo の画（readback と shared） |

- 合計: p010c〜p010m で約 **105〜107 LW**（版 2 は 97〜99、版 2.1 で p010n 10→14・p010e 8→12。review 2 の M-6 (d)、推測。版 1 は 80）。これに compiler の p013・p014（P2 が見積もる。推測: 30〜50 LW）が加わる。design.md の p010 の 60〜100 LW は vertex・fragment の compiler を含んでいたので、**p010 は design.md の見積もりを超える**（Q1 へ）。
- 順の要: p010c・p010d は並行できる。p010d → p010n → p010e・p010f（p010e・p010f は並行できる）。p010f は p010n の後で、p010k1（最初に画が出る点、compiler なし）に続く。p010h・p010i は O-SHADER-ABI・O-VSFS を待つ。compiler が遅れても p010c〜p010g・p010k1 は進む。
- **M9 の照合**（一度だけの確かめ、p010c で）: Mesa 26.2.5 の tarball を自分の worktree の `build/p010c/` の中に取り（sha256 を phase.md に）、`csbgen/gen_pack_header.py` で pack の header を作り（取ってきた code の実行なので、専用の directory で `python3 -I` で走らせ、入力の path は引数で渡す。review 2 の Minor 10。後の Phase の照合も同じ）、zedBSD の word の組み立て（texture・sampler・PBE の word）と同じ field の値を詰めて比べる host の program を `build/p010c/` で走らせる。tree には入れず、比べた field の数と結果を phase.md に証拠として残す。新しい word を足す Phase（p010e の ISP・VDM・PPP、p010f の IPF、p010n の CR の値）は同じ照合を足す。FW の struct の offset は static assert（p010d）。
- 各 Phase の phase.md の必須項目:
  - 出典は **使う block の直前の複数行の comment** に `Value(s) referenced:`・`Procedure referenced:`（版と path と名前、M 番号は書かない）。file の先頭には書かない（Guardrail）。source に WS・Phase・plan の参照を書かない（Guardrail の「source の中の開発の管理情報」）。
  - 段の行（§8.1）を、その Phase が作る段の成功・失敗の道に入れる。gpu-selftest の段をその Phase で足す。
  - 実機の文面は `plan/ws269/tests/p010/` の試験の image で、ユーザーが選べる試験の一覧へ。

## 11. 危険

- BXE は Mesa で unsupported（design.md S5）。BXM-4-64 の道との違いで M2 の値が BXE で違う所がありうる。p010b で feature の分岐を記録した。実機で外れたら kernel の check（§8.3）と段の行の reset・fault の理由で切り分ける。
- FW の中の挙動の未確認 3 点（§0）は Mesa・mainline と同じ形で避けたが、Mesa は BXE を試していないので、同じ形でも FW の振る舞いが違う危険は残る（推測）。
- FW の command の struct は FW の版（p002 で固定）に依る。v7.3 の header と FW の版の組は §0 のとおり差が小さいが、static assert で offset を照らす（p010d）。
- **p005〜p009 の実機が外れる危険**（M6 (b)）: p010 は名前を付けた出力（host 試験と merge）に依って進むが、p005〜p009 の実機（FW の起動・MMU・coherency・compute）が外れると、その上の p010 の実機の確かめは全部止まり、p010 の code にも直しが波及しうる。p010 の実機の確かめは p009 の実機の後に並べる（ユーザーの試験の一覧の順）。
- compiler（p013・p014）が vertex・fragment に届くまで p010i 以降は進まない。その間に固定の program で TA・3D・transfer・present（p010k1、clear の色）を先に実機で通す。
- p010 の LW は design.md の見積もりを超える（§10）。
- **static data area が無い事**（§4.1、BUG-342）: 推測だが、MCU の fence が session の最初の resource を上書きする危険は p009 の実機の compute（vkcompute）にも関わり得る。p009 の実機の前に Q1 が知っておく危険として残す（review 2 の M-4）。
- 未確認の FW の挙動（review 2 の「確かめられなかった点」）: static data area を FW・硬が使うか、grow 0 の時に GROW が来るか、`pds_pr_bgnd` が 0 の時、cleanup ctl が無い時の CLEANUP、combined kick が必須か。どれも mainline・Mesa の形を採って避ける。

## 12. レビュー 1 への対応

| 指摘 | 対応 | 場所 |
| --- | --- | --- |
| B1 sysctl は新しい UAPI | p010a で main に入った（Q1 の判断で今は残す）。D-P6 としてユーザーに聞く（推奨: 読み取りの leaf として残す）。外す時は gpu-selftest は `kern.msgbuf` を読む | §8.1、§8.2、§9 D-P6 |
| B2 出典の書き方 | 「使う block の直前の複数行の comment、M 番号は書かない」に直した。p010a の code は出典の comment を持たないが、Guardrail の開発の管理情報（`ws269-p010a`・`design-p010.md §8.x`）を comment に持っていたので消した（cbb9bc09e） | 冒頭、§10 の必須項目 |
| B3 grow に応えないと FW が止まる | GROW_UPDATE を delta 0 で返す、RECONSTRUCTION に応える、FRAG_PR を常に積む、時間切れ・HWR の後は HWRT を作り直す、`fail(free list)` は GROW の通知から | §2、§4.1、§4.2、§9 D-P1 |
| B4 compile の入力と D-P3 | compile の入力（attrib・output の format、blend の設定、sample の数）、要求の表に `BLEND_CONSTANTS`、D-P3 を (b)。P2 に伝え済み（Q1） | §4.4、§5.1〜§5.3、§9 D-P3 |
| M1 EOT と encoder | EOT・pixel event の PDS を §4.3・§5.4・p010e に。encoder の広げは compiler の Phase p013（P2、O-ENC）、`usc-encode.c` の持ち主は p013 | §4.3、§5、§5.4、§10 |
| M2 HWRT・free list・heap | local・global の free list、rt_data 2 組、RGNHDR・TRANSFER_FRAG の heap、PDS・USC の割り当て、最初の render で作る | §4.1、p010d |
| M3 static state と recovery | static state と SR・PT を Mesa の形で、4 つの context の identity・recovery、CLEANUP | §4.1、p010n |
| M4 transfer の形と見積もり | IPF・TRANSFER_FRAG・相対の address・FAST_2D/FAST_SCALE、copy・clear（p010f 10 LW）と blit（p010o 6 LW）に分けた | §6、§10 |
| M5 venus-frame | p010 の受け入れから外し、vkdemo と gpu-selftest の clear・copy・draw に。design.md §6 の文面の変更として D-P7 | §1、§8.2、§9 D-P7 |
| M6 (a) compiler の Phase | p013（encoder、O-ENC）・p014（compiler、O-SHADER-ABI・O-VSFS）を ws.md に | §5、§10、ws.md |
| M6 (b) uncleared の依存 | 名前を付けた出力（O-P006・O-P007・O-P009・O-p010x）で書き、実機が外れる危険を §11 に | §10、§11 |
| M6 (c) p010l は任意 | p010l を受け入れに必須、p010m の依存に | §7、§10 |
| M6 (d) p010k が compiler に依る | p010k1（clear だけ、O-p010f と O-P007）と p010k2（vkdemo）に分けた | §7、§10 |
| M6 (e) p010d の実機の手段 | render check の clear だけの形を p010n に、実機の確かめも p010n に（EOT が O-ENC を要れば p010e に移す） | §8.3、§10 |
| M6 (f) LW | p010d 10・p010n 10・p010f 10・p010o 6 に。合計 97〜99 + compiler | §10 |
| M7 止まった所 | kick ごとの sync の word、context ごとの identity、理由に reset の理由・fault の VA、「順」を消した | §2、§4.1、§8.1 |
| M8 readback の事実と shared に落ちる危険 | readback は OPTIMAL の swapchain から。p010l まで linear の features 0 と ENOTSUP、p010c の host 試験 | §0、§3.2、§7、p010c |
| M9 golden | `gen_pack_header.py` の pack の header と一度だけ照合（`build/p010c/`、tree に入れない）、FW の struct は check の header と static assert | §10、p010d |
| Minor 1 path | §0 を直した（`pvr_rt_dataset.c`・`pvr_free_list.c`・`pvr_usc.c`・`pvr_rogue_fwif_client(_check).h`）。`interface.h` の出典は v7.3 に揃える | §0 |
| Minor 2 BXE の feature | linestride 2 pixel を事実に | §3.1 |
| Minor 3 vertex の format | `R32G32B32_SFLOAT` を明記 | §3.2 |
| Minor 4 border と LINEAR の texture | CLAMP_TO_BORDER は透明の黒に固定、LINEAR は STRIDE_IMAGE_WORD1 | §3.3 |
| Minor 5 部分の renderArea | 断る（log の 1 行）、PBE の clip は p011 | §4.3 |
| Minor 6 mip の layer | 全段で数える | §3.1 |
| Minor 7 vkdemo の引数 | `--duration=4` | §0、§8.2 |
| Minor 8 段の行と gpu-selftest | 名前を `render-node` に揃えた（1a5d6a3e9、host 試験 24 PASS）、recovery の後は ok を出し直さない、Keiland の前に流す、子が各自の instance、lease の後の描き直しは未確認で実機の文面に | §8.1、§8.2 |
| Minor 9 error の値 | `vkCreateRenderPass` は `OUT_OF_HOST_MEMORY`、内部の約束と分けた | §4.3 |
| Minor 10 design.md §3.5 | D-P1 で置き換えを記録（design.md の改訂は Q1） | §9 D-P1 |
| Minor 11 twiddle の式 | CPU は OPTIMAL に触らない、host 試験は大きさだけ | §3.1 |
| Minor 12 memory | 1080p の数と、p010d で FW の heap の表、p011 の前に 1080p の表 | §3.1 |
| Minor 13 dc8200 の zero copy | p010l に SLC_BYPASS と pitch の一致 | §7 |
| p010b の確かめ | §0 の事実、FW の未確認 3 点は Mesa・mainline の形で避ける（Q1 の判断） | §0、§4、§6 |

## 13. レビュー 2 への対応（版 2.1）

| 指摘 | 対応 | 場所 |
| --- | --- | --- |
| M-1 時間切れの後の作り直し | 時間切れの後は HWRT・free list を core に残す（isolate・reset の後に返す）、HWR の後は RECONSTRUCTION の応答だけ、CLEANUP の busy は上限で isolate。D-P4 の条件も直した | §2、§4.1、§9 D-P4、p010d |
| M-2 PBE の clip | 必須に。render は extent ∩ renderArea、transfer は dst の矩形。部分の renderArea は color なら受け、depth の STORE がある時だけ断る | §4.2、§4.3、§6、p010e・p010f・p010g、§1 |
| M-3 (a) `pds_pr_bgnd` | 本番の fragment に load の background を入れる。FRAG_PR の EOT は本番と同じ | §4.2、§5.4、p010n・p010e |
| M-3 (b) `partial_render_geom_frag_fence` | geometry の queue の timeline の UFO と seqno − 1 | §4.2 |
| M-3 (c) combined kick | KCCB 117 で geometry と FRAG_PR を 1 回、待ちは FENCE_PR（215） | §4.2、p010n |
| M-3 (d) cleanup ctl | HWRT を使う kick に `cleanup_ctl_fw_addr`（FRAG_PR は付けない）。p010d が `cleanup_state`、p010n が付ける | §4.1、p010d・p010n |
| M-4 static data area | 5 区画と、割り当てを次の page から、を p010d の heap の節に。修正は BUG-342 で別に進み、p010d は食い違わないことを確かめる。p009 の実機の危険として §11 | §4.1、§10、§11 |
| M-5 FW の通知 | 105 の `context_reset_data`（`reset_reason`・`dm`・`FLAG_PF` の `fault_address`・`FLAG_ALL_CTXS`）で段に割り当て、112 は FW 自身の fault で device lost のまま | §4.1、p010n |
| M-6 (a) p010n の実機 | p010n は host 試験だけ、`ta`・`3d`・render check の実機は p010e へ。p010n の O-ENC の条件は SR の 1 点だけに（SR 以外は今の encoder で書ける） | §8.3、§10 |
| M-6 (b) transfer の context | p010f が transfer の context の identity・recovery・sync の word を作り、O-p010n に依る | §6、§10 |
| M-6 (c) O-ENC の中身 | 固定の program ごとの命令の種類を Mesa（`pvr_usc.c`・`rogue/pvr_clear.c`・`pvr_arch_job_context.c`・`pvr_winsys_helper.c`・usclib の `common.cl`）から数え、P2 の main の encoder と照らした。残りは SR の shared の burst の `ld`・`st` の確かめだけ。P2 と合意するまで p013 の O-ENC は暫定 | §5、§5.3、§5.4 |
| M-6 (d) LW | p010n 14、p010e 12。合計 105〜107 | §10 |
| Minor 1 RGNHDR | BXE では GENERAL に置く（RGNHDR の heap は作らない） | §0、§4.1、p010d |
| Minor 2 linear の features | 外すのは linear の SAMPLED だけ、TRANSFER は出す（p010c は版 2 の後にこの形で実装済み） | §3.2、§7、p010c |
| Minor 3 奇数の row length | p010f は 1 行ずつの job に分ける、`double_stride` は p011 | §6 |
| Minor 4 free list の尽き | UPDATE_STATS も印に、GROW_UPDATE は ready 0・new = current＋ready | §4.1 |
| Minor 5 `free_list_min_pages` | 25（BRN66011 なし） | §4.1 |
| Minor 6 PDS・USC の base | 確かめ済みと書いた | §4.1 |
| Minor 7 p010d の依存 | O-P009 を足した | §4.1、§10 |
| Minor 8 O-SHADER-ABI | p014 の最初の出力として切り出し、O-ENC を待たない | §5、§10 p010h |
| Minor 9 ws.md の記録 | D-P7 の重複、D-P2 の文面、p010a の「render node」、p010 の行の依存を直した | ws.md |
| Minor 10 `gen_pack_header.py` | 専用の directory で `python3 -I`、path は引数 | §10 の M9 の照合 |
| Minor 11 p010b の SHA | p010d の必須項目に | §0、§10 |
| P2 の compiler（main） | §5.4 の「今の encoder」の列を `usc-encode.h`（smp・ditr・emitpix・upck を含む）と照らした。`shader-compile.h` は O-SHADER-ABI の候補 | §5、§5.4 |

### 13.1 版 2.2 の注記（2026-10-20）

| 点 | 対応 | 場所 |
| --- | --- | --- |
| transfer の道 | copy・clear・blit は FAST_SCALE と背景の object（Mesa の `pvr_3d_copy_blit_core`）。IPF・FAST_2D は clip blit の道だけ。奇数の row length は 1 行ずつ。EOT は p010e の即値の形。Q1 が 2026-10-20 に採用（p010f q1379 の phase.md の「設計からの変更」） | §6 |
