<!-- awesome-plan project=zedbsd record=ws269-design-p010 -->

# WS269 設計の追補: p010（graphics）の分割

- 版: 1（2026-10-20、P1、q1362 / q1362-i01）。親の設計は [design.md](design.md) 改訂 3.1（§5 の案 A、§6 の p010、§7 の判定）。コードは書いていない。**design-reviewer のレビュー待ち。**
- 起点（2026-10-20 07:24 ユーザー）: 「PowerVRのコードは完了を目指して作業をお願いします。あとでUATにおいて、どこまで実行できたかわかるログが出れば、デバッグしやすいと思います。」
- ユーザーは不在。判断の要る点は推奨を採って先へ進める前提で §9 に D-P1〜 として列挙する（採否はユーザー）。
- 印は design.md と同じ: **事実**（出典あり）、**推測**、**未確認**。出典の規則は Guardrail の「MIT・GPL の出典のデータの引用」（値は `Value(s) referenced:`、手順は `Procedure referenced:`、code・構造・名前は写さない）。

## 0. 出典と、この追補で確かめていない事

| # | 資料 | 使い道 |
| --- | --- | --- |
| M1 | Mesa 26.2.5（`ddaf85e353442d9d7f192fdcf1823e652e3f2ee3`、p002 で固定、MIT）の `src/imagination/csbgen/rogue/` の XML（`cr.xml`・`ipf.xml`・`vdm.xml`・`ppp.xml`・`pbestate.xml`・`texstate.xml`・`lls.xml`・`cdm.xml`・`pds.xml`・`kmd_stream.xml`。TA の XML は無い。`volcanic/` は使わない）。blob SHA は [phase010b](phase010b/phase.md) | 制御の stream・state の word の bit の位置（Value(s) referenced） |
| M2 | 同 `src/imagination/vulkan/` の arch 別の file（`pvr_arch_job_render.c`・`pvr_arch_job_transfer.c`・`pvr_arch_job_context.c`・`pvr_arch_job_compute.c`・`pvr_arch_job_common.c`・`pvr_arch_cmd_buffer.c`・`pvr_arch_image.c`・`pvr_arch_formats.c`・`pvr_arch_tex_state.c`・`pvr_arch_descriptor_set.c`・`pvr_arch_pass.c`・`pvr_arch_hw_pass.c`・`pvr_arch_pipeline.c`・`pvr_arch_spm.c`）、共通の `pvr_image.c`・`pvr_formats.c`・`pvr_descriptor_set.c`・`pvr_spm.c`、`rogue/pvr_clear.c`、`pds/pvr_pipeline_pds.c`、kernel への提出の `winsys/powervr/pvr_drm_job_{render,transfer}.c`。`pvr_cmd_buffer.c`・`pvr_job_render.c`・`pvr_job_transfer.c`・`pvr_pass.c`・`pvr_pipeline.c`・`pvr_tex_state.c`・`pvr_clear.c`（vulkan 直下）は 26.2.5 に無い（`.h` だけ）。本文の旧い名前は左の `pvr_arch_*` を指す。blob SHA は [phase010b](phase010b/phase.md) | 手順（Procedure referenced）と表の値 |
| M3 | Linux mainline v7.3（`c03c1a14507dfaf8b856c2608f9ac216021fc222`）の `drivers/gpu/drm/imagination/`（`pvr_rogue_fwif.h`: fwrendercontext・fwtransfercontext・rta_ctl・freelist・hwrtdata(_common)、`pvr_rogue_fwif_client.h`: geom_regs・cmd_geom・frag_regs・cmd_frag・transfer_regs・cmd_transfer、`pvr_rogue_fwif_shared.h`: static_rendercontext_state、`pvr_job.c`・`pvr_context.c`・`pvr_free_list.c`・`pvr_hwrt.c`）と `include/uapi/drm/pvr_drm.h`、GPL-2.0 OR MIT。blob SHA は [phase010b](phase010b/phase.md) | FW の command（geometry・fragment・transfer）、free list・HWRT data set の FW の object、context の作り方（値と手順） |
| L1 | zedBSD の `userland/desktop/libvulkan/`（`wsi-display.c`・`wsi-swapchain.c`・`wsi-image.c`） | WSI の 2 つの道（§6） |
| L2 | `userland/tests/vkdemo`・`userland/tests/venus-frame`・`userland/tests/vkcompute` | 受け入れの program が呼ぶ Vulkan の命令（§7） |

- **確定（p010b、2026-10-20）**: M1〜M3 の path と blob SHA は GitLab API・gh api で確かめた（[phase010b](phase010b/phase.md)）。26.x で Mesa は `pvr_arch_*` に分かれていた。**確定（p010b の q1366、2026-10-20）**: BXE（36.50.54.182）の feature の分岐（TLA なし、PBE の linestride の単位 2 pixel、twiddled の 2 の冪の padding、texture・sampler の word の大きさ）は file の中身で確かめた（値は [phase010b](phase010b/phase.md) の表）。blend・pack の置き場所（§5.3 の 2・D-P3）は Mesa では compiler の側で、設計の変更が要る（phase010b、版 2 で扱う）。
- 事実（L1、2026-10-20 に読んだ）: libvulkan は scanout の image を `VK_IMAGE_TILING_LINEAR`、usage TRANSFER_SRC|TRANSFER_DST|SAMPLED、external（DMABUF か OPAQUE）で作る（`wsi-image.c:60-80`）。import の前に `stride` を `stride_alignment` で検査し、外れたら `VK_ERROR_FORMAT_NOT_SUPPORTED`（`wsi-display.c:1290-1293`）、その時は readback の道に落ちる（`wsi-swapchain.c:1095-1120`）。readback の道は `vkCmdCopyImageToBuffer`（`wsi-swapchain.c:2791-2831`）、shared の道は `present_copy_shared` が `vkCmdClearColorImage`・`vkCmdBlitImage`（NEAREST）・`vkCmdCopyImage` を使う（`wsi-swapchain.c:3211-3333`）。
- 事実（L2）: vkdemo は `vkCreateRenderPass`・`vkCreateFramebuffer`・`vkCreateGraphicsPipelines`・`vkCmdBeginRenderPass`・`vkCmdBindVertexBuffers`・`vkCmdDraw`・`vkCmdPushConstants`・`vkCmdClearColorImage`・`vkCmdCopyBufferToImage`・`vkCmdCopyImageToBuffer`・`vkCreateSampler`・combined の descriptor を使い、format は `R8G8B8A8_UNORM`（optimal の COLOR_ATTACHMENT と SAMPLED）、`D32_SFLOAT`、`B8G8R8A8_UNORM`（display）を照会する（`renderer.c:170-183`、`display.c:675-700`）。dynamic rendering は使わない。

## 1. 範囲

p010 の受け入れ（design.md §6）: 実機で `vkdemo`・`venus-frame` の画をユーザーが目で見る。加えてこの追補で、UAT の進み具合の画面の行（§8）を p010 の最初に作る。

範囲外: SPIR-V から USC への compiler（q1363、P2。境界だけ §5 で決める）、Keiland が使う残りの機能（p011）、MSAA（sample count 1 のみ）、tessellation・geometry shader、query、secondary の command buffer、dynamic rendering（p011 で要れば）、TFBC・FBCDC の圧縮。

Keiland の起動の待ち（design.md §3.3 の最後）は p005（q1356）の `hw.gpu.attaching` で済んでいる。p010 では実機で確かめるだけ（p010m の確かめに入れる）。

## 2. 全体の形

kernel の powervr の Venus の executor（p009、`vk-*.c`）が、Mesa の userland の driver と mainline の kernel の driver の両方の役をする:

```
libvulkan ──Venus──▶ vk-*.c（decode、object）
                       ├─ image・view・sampler・descriptor（memory の中の word）
                       ├─ render pass・pipeline（compiler の出力 + driver が組む PDS）
                       └─ command buffer の記録 → submit の時に
                            ├─ compute: CDM の stream（p008・p009）
                            ├─ render: VDM の stream（TA）+ 3D の register 群（ISP・PBE・ZLS）
                            └─ transfer: transfer context の 3D の job（driver の固定の program）
render-queue.c ──FW の CCB──▶ FW（geometry・fragment・compute・transfer の context）
```

- 1 回の vkQueueSubmit の中の command buffer を順に「job の列」に変える。job は compute・render（geometry と fragment の対）・transfer の 3 種。job の間の順は FW の CCB の UFO（fence の値の待ちと書き）で守る（p006 の fence と同じ道）。barrier は job の境で全部を待つ（細かい stage の重なりはしない。推測: 性能は p011 以降の課題）。
- submit は p009 と同じく ioctl の中で組み、最後の job の fence を上限付きで待つ（今は 2 秒）。graphics は 1 frame が長くなり得るので、上限を 2 秒のまま、超えたら p009 と同じ時間切れの道（abandoned、`drv_gpu_report_session_error(EIO)`）を通す。非同期の submit（ioctl から先に戻る）は p011 の候補（D-P4）。

## 3. image

### 3.1 tiling と layout

| tiling | 使う所 | layout |
| --- | --- | --- |
| LINEAR | libvulkan の scanout・readback の image、`VK_IMAGE_TILING_LINEAR` の全部 | 行の順。`rowPitch` は **32 byte の倍数かつ 2 pixel の倍数**の最小（WS277 の R6 (1)）。mip は LINEAR では 1 段だけ受ける（他は `VK_ERROR_FORMAT_NOT_SUPPORTED`、image の format の照会でも出さない）。array は 1 層 |
| twiddled | `VK_IMAGE_TILING_OPTIMAL` の 2D の color・depth | Morton の順。幅・高さを 2 の冪に切り上げる（事実、p010b。Value(s) referenced: M2 `pvr_image.c` の physical extent、twiddled と mip 2 段以上の時に幅・高さ・深さを next power of two、BXE の feature の分岐なし）。mip の各段と層を順に並べ、各段の始まりを揃える |

- strided の 3D の image、cube・3D の image は p011（vkdemo・venus-frame は 2D だけ）。
- `vkGetImageSubresourceLayout` は LINEAR だけ答える（offset 0、rowPitch 上記、size = rowPitch·height）。
- memory の requirements: alignment は 256 byte（p009 の `POWERVR_VK_RESOURCE_ALIGN`）と PBE・TPU の要る値の大きい方（p010b: Mesa は image の alignment を winsys の page の大きさにする。事実）。memory type は p009 の 2 つ（D7 (a)）のどちらでも可。
- depth: `D32_SFLOAT`・`D16_UNORM` を optimal で。depth は ISP の内部の buffer で持ち、load・store は ZLS（§4.4）。stencil の format（`D24S8`・`S8`）は p011。

### 3.2 format の表

最小の表（p010c）。各行に PBE の pack の format、TPU の texture の format、swizzle を持つ（Value(s) referenced: M2 `pvr_formats.c`・`pvr_arch_formats.c`、M1 `csbgen/rogue/texstate.xml`・`pbestate.xml`）:

| Vulkan の format | optimal の features | linear の features |
| --- | --- | --- |
| R8G8B8A8_UNORM・B8G8R8A8_UNORM | COLOR_ATTACHMENT・BLEND・SAMPLED・SAMPLED_FILTER_LINEAR・TRANSFER_SRC/DST・BLIT_SRC/DST | 同じ（R7 の「linear の SAMPLED」を満たす） |
| R8G8B8A8_SRGB・B8G8R8A8_SRGB | 同上 | 同上 |
| R8_UNORM・R8G8_UNORM | SAMPLED・TRANSFER・BLIT | TRANSFER |
| D32_SFLOAT・D16_UNORM | DEPTH_STENCIL_ATTACHMENT・TRANSFER_SRC/DST | なし |

buffer の features: vertex の `R32G32(B32A32)_SFLOAT`・`R8G8B8A8_UNORM`。表に無い format は features 0 で、image の作成は `VK_ERROR_FORMAT_NOT_SUPPORTED`。

### 3.3 image view・sampler

- image view: texture state の word（事実、p010b: TEXSTATE の IMAGE_WORD0・1 の 2 × 64 bit = 128 bit、M1 `texstate.xml`）を view の作成の時に組んで持つ（descriptor の update で写す）。PBE の state は render pass の begin の時に組む。
- sampler: sampler の word（事実、p010b: SAMPLER_WORD0・1 の 2 × 64 bit、M1 `texstate.xml`）を作成の時に組む。filter NEAREST・LINEAR、mipmap mode、address mode REPEAT・CLAMP_TO_EDGE・MIRRORED_REPEAT、border は透明の黒だけ、anisotropy なし、compare なし。

## 4. render（TA と 3D）

### 4.1 FW の object（mainline の kernel の役）

Procedure・Value(s) referenced: M3（`pvr_context.c`・`pvr_free_list.c`・`pvr_hwrt.c`・`pvr_rogue_fwif.h`）。

- render context: session ごとに 1 つ。geometry と fragment の FW の context（CCB を各 1 つ）。p006 の compute の context の作り方（FW の memory、CCCB、priority）と同じ形で 2 本。
- free list: session ごとに 1 つ、**固定の大きさ（D-P1、推奨 16 MiB）、grow しない**。FW の free list の object と、page の表（parameter buffer の page の物理の列）を kernel が作る。FW の「free list が尽きた」の通知（grow の要求）には応えず、その render を失敗にし `fail(free list)` の行を出す（§8）。partial render（SPM: 尽きた時の途中の store と load の program）は p011（D-P1）。
- HWRT data set: framebuffer の大きさと sample の数ごとに 1 つ。session の中で「幅・高さ・layer」を key に最大 4 個を cache（vkdemo・WSI の窓の大きさは 1〜2 種）。macrotile・tile の設定・TE・ISP の screen の値は M2 `pvr_arch_job_render.c`（rt dataset の作成）の手順で。tile は 16×16（p002 の feature）。

### 4.2 1 つの render の job

- 1 つの render pass の instance（subpass 1 つ、§4.3）= 1 つの render job = geometry の kick（VDM の control stream）1 つ + fragment の kick 1 つ。fragment は geometry の完了を FW の中で待つ（mainline の geometry→fragment の依存の形、M3 `pvr_job.c`）。
- draw の 0 個の render pass（clear だけ）も geometry の空の stream（終わりの word だけ）と fragment で出す（M2 の「empty geometry」の手順、未確認）。
- geometry の command: VDM の control stream の address、TA の state（PPP の screen・TE の値）。fragment の command: ISP の background object（clear）、PBE の state（attachment の store）、ZLS（depth の load・store）、tile の数。値の組み方は M2 `pvr_arch_job_render.c`（geometry・fragment の job の作成）、FW の struct は M3 `rogue_fwif_cmd_geom`・`rogue_fwif_cmd_frag` の field の順に kernel が自分の型で書く。

### 4.3 render pass・attachment・clear・load/store

- `vkCreateRenderPass`（と 2）: attachment ≤ 4 color + 1 depth、subpass は 1 つだけ（2 つ以上は `VK_ERROR_FEATURE_NOT_PRESENT`、p011 で要れば。vkdemo は 1 つ）。subpass の dependency は記録しない（job の境で全部を待つ）。
- framebuffer: image view の列と幅・高さ。HWRT data set（§4.1）を選ぶ key。
- load op: CLEAR は ISP の background object の clear の値（PDS の program が USC の固定の「色を出す」program を走らせる、M2 `rogue/pvr_clear.c` の手順）。LOAD は color なら「attachment を texture として読み tile に出す」background の program（driver の固定の USC、§5.4）、depth なら ZLS の load。DONT_CARE は何もしない。
- store op: STORE は PBE（color、LINEAR・twiddled の書き出し、R6 (2)）と ZLS（depth）。DONT_CARE は PBE を出さない。
- `vkCmdClearAttachments`（render pass の中の clear）: 全面なら load op の CLEAR と同じ、部分なら固定の program で矩形を描く。p010g で全面だけ、部分は p011。
- render area: 全面だけを受け、部分は scissor で同じ結果にする（推測: tile の数は全面で数える）。

### 4.4 graphics の pipeline と draw

- pipeline の作成: compiler（§5）の vertex と fragment の出力を受け、driver が PDS の program（vertex: 属性の DMA と USC の起動、fragment: varying の補間（coefficient の DOUTI）と texture の state・shared register の DMA と USC の起動）を組む（PDS の encoder は p008）。
- 固定の state: input assembly は TRIANGLE_LIST・TRIANGLE_STRIP、primitive restart なし。rasterization: cull・front face・polygon mode FILL だけ、depth bias なし、line width 1。depth: test・write・compare op。blend: attachment ごと、一般の factor と op（ISP・PBE か USC の program の中の blend、M2 で BXE の分岐を p010b で確かめる。推測: Rogue は blend を fragment の USC の末尾で行う → compiler に「blend の末尾」を足す依頼が要る、§5.3）。viewport・scissor は pipeline の固定か dynamic（`VK_DYNAMIC_STATE_VIEWPORT`・`SCISSOR`、Keiland が使うので p010i で両方）。
- draw: `vkCmdDraw`（index なし）と `vkCmdDrawIndexed`（16・32 bit の index）。VDM の control stream に state の update（PPP の state の block、vertex の PDS、viewport・scissor の表）と index list の word を書く（M1 `csbgen/rogue/vdm.xml`・`ppp.xml`、M2 `pvr_arch_cmd_buffer.c` の draw の手順）。instance の数 1 だけ（vkdemo は 1）、多いのは p011。
- push constant: 128 byte まで。記録の時に command buffer の memory に写し、PDS がその address を shared register に DMA する（§5.2）。

## 5. compiler と driver の境（q1363 の P2 と共有）

### 5.1 compiler の出力（1 つの stage ごと）

compiler は SPIR-V の module と entry point と stage を受け、次を返す。driver はこれ以外の compiler の内部を見ない。型の置き場所は `src/drivers/gpu/powervr/shader.h`（新規、p010h で P1・P2 が合意して作る。それまで本節が契約）。

| 項目 | 中身 |
| --- | --- |
| stage | compute・vertex・fragment |
| USC の code | bytes と長さ（p008 の `powervr_usc_program_encode` の出力の形）。始まりの offset は 0 |
| temps | 使う temp の数（PDS の task の word の temps の field、p008 の 2 の単位） |
| shared register の要求の表 | 下の 5.2 |
| vertex の入力 | location ごとの (format、shader が読む vertex の入力 register の番号) |
| vertex の出力・fragment の入力 | varying ごとの (location、成分の数、補間（smooth・flat）、出力の register の番号)。vertex は position の register の番号。fragment は「iterated の coefficient の番号」 |
| fragment の出力 | color の attachment ごとの出力の register（PBE に渡す形、pack は compiler か PBE か §5.3） |
| 性質 | discard の有無、depth の書き、workgroup の大きさ（compute）、shared memory の大きさ |

### 5.2 descriptor と shared register の約束

- descriptor set は GPU の memory の中の 1 つの buffer。binding の並びは set layout の作成で driver が決め、各 binding の offset と 1 つの descriptor の大きさを固定する:
  - storage・uniform buffer: 16 byte（address 64 bit、range 32 bit、予備 32 bit）
  - sampled image: image view の texture state の word（§3.3。事実、p010b: 硬の word は 16 byte。Mesa は compiler が読む meta の 4 × 32 bit を足して 32 byte）
  - sampler: sampler の word（事実、p010b: 硬の word は 16 byte。Mesa は meta 16 byte と gather 用の word 16 byte を足して 48 byte）
  - combined image sampler: image の word の後に sampler の word
- compiler は shader が使う資源を「shared register の要求の表」で頼み、driver の PDS の data の program がその値を shared register に入れてから USC を起動する。要求の種類:
  1. `SET_BASE(set)`: set の buffer の GPU の address（64 bit、2 register）
  2. `DESCRIPTOR(set, binding, array_index, words)`: その descriptor の word を set の buffer から DMA（texture・sampler の word を USC の sample の命令が shared register から読むため）
  3. `PUSH_CONSTANTS(offset, size)`: push constant の範囲
  4. `LITERAL(value)`: 定数
  5. `WORKGROUP_ID`・`NUM_WORKGROUPS`（compute、p009 の program の形を広げる）
  各要求は compiler が決めた shared register の番号（先頭から詰める）を持つ。buffer の中身の読み（storage・uniform）は compiler が `SET_BASE` か `DESCRIPTOR` の address から USC の load で読む。
- dynamic offset（`UNIFORM_BUFFER_DYNAMIC`）は p011（Keiland が使うか p011 で数える）。

### 5.3 compiler への依頼（P2 へ、Q1 経由）

1. 5.1・5.2 の出力の形（特に shared register の要求の表と varying の表）。
2. fragment の color の出力の pack の位置（USC で pack して PBE に raw を渡すか、PBE が pack するか）と blend: M2 で BXE の道を p010b で確かめた後に決める。それまで「compiler は float の RGBA を出力の register に置く、pack と blend は driver が末尾の命令を足す」を仮の約束にする（D-P3）。
3. driver が持つ固定の program（§5.4）は compiler を通さず p008 の encoder で直に書く。compiler の完成を p010d〜g は待たない。

### 5.4 driver の固定の USC・PDS の program

compiler の無い段で要る物（p008 の encoder で直に書く、p009 の `compute-program.c` と同じ置き方で `render-program.c`・`transfer-program.c`）:

- clear の色を出す fragment（background object）
- attachment の load（texture の 1 対 1 の読み）
- transfer の copy・blit・clear（§6）
- 試験の三角形（単色の vertex・fragment、kernel の中の試験 §8.3 と p010e）

## 6. transfer（TQ）

- FW の transfer context（M3 の transfer の command）で 3D の job として行う（Procedure referenced: M2 `pvr_arch_job_transfer.c`）。BXE に TLA（2D の core）は無い（事実、p010b: `bxe-4-32.h` に `has_tla` が無く、26.2.5 の `pvr_device_info.h` にも TLA の feature が無い）ので全部 3D の道。
- 受ける命令: `vkCmdCopyBuffer`、`vkCmdCopyBufferToImage`・`vkCmdCopyImageToBuffer`（image の 1 段・1 層ずつ）、`vkCmdCopyImage`（同じ format の大きさ）、`vkCmdBlitImage`（NEAREST・LINEAR、拡大縮小、同じ format か表の中の 8 bit の RGBA の間）、`vkCmdClearColorImage`、`vkCmdFillBuffer`・`vkCmdUpdateBuffer`（compute の固定の program でもよい、p010f で選ぶ）。
- 書き出しは PBE で、LINEAR の image には `MEMLAYOUT_LINEAR`（R6 (2)）、buffer にも LINEAR。読みは TPU（texture state を transfer の source に組む）。buffer↔buffer の copy は compute の固定の program（p008 の load・store）でもよい（推奨: compute、簡単）。
- 1 つの transfer の命令 = 1 つの transfer job（矩形が tile の上限を超えたら分ける）。

## 7. WSI と display（libvulkan の 2 つの道）

| 道 | 要る物 | Phase |
| --- | --- | --- |
| readback（copy） | linear の image の作成・bind、`vkCmdCopyImageToBuffer`、host visible の buffer の map、semaphore・fence。libvulkan が CPU で display の node に present する | p010k |
| shared（zero copy に近い） | image の作成の external の chain（`EXTERNAL_MEMORY_IMAGE_CREATE_INFO`）、`EXPORT_MEMORY_ALLOCATE_INFO`（p009 で ENOTSUP）、`GPU_OP_RECORD_IMPORT_RESOURCE`（同）、`GET_PHYSICAL_DEVICE_PROPERTIES2` の identity、`present_copy_shared` の clear・blit・copy を LINEAR の PBE で、display の node の `scanout` の import（p007、p006 の lookup で retain、uncached の view から CPU の copy）。WS277 の R6・R7 を満たした後に WS277 が `stride_alignment = 32` を出す | p010l |

- 順: readback を先に（vkdemo が画面に出る最短の道）、shared は後。readback は `stride_alignment` に依らない。
- capset は p009 の 1.3.269・flags 7 のまま（WS277 R7 の文面の食い違いは p009 で Q1 に報告済み）。

## 8. UAT の進み具合の行（ユーザーの要望）

### 8.1 kernel の段の行

- 形: `powervr: <段>: ok` か `powervr: <段>: fail(<理由>)`。画面の console（`kern_logf`、framebuffer の console に出る既存の道）に 1 行。
- 段（この順で出る）:

| 段 | ok を出す時 | fail の理由の例 |
| --- | --- | --- |
| `firmware` | FW の起動と health の kick の応答（p005） | `enoent`・`timeout`・`bad image` |
| `render node` | render の node の登録（p006） | `ebusy`・`coherency` |
| `compute` | 起動後に最初の compute の job の fence が届いた時 | `timeout`・`fault`・`spirv subset` |
| `ta` | 最初の geometry の kick の完了 | `timeout`・`fault`・`free list` |
| `3d` | 最初の fragment の kick の完了 | `timeout`・`fault` |
| `transfer` | 最初の transfer の job の完了 | `timeout`・`fault`・`format` |
| `present` | display の node の最初の present（copy か import） | `import`・`stride`・`claim` |

- 各段の ok は起動ごとに 1 回だけ。fail は段ごとに最初の 3 回まで出し、4 回目に `powervr: <段>: further failures not shown` を 1 回。理由は短い英語の小文字の語（理由の表は `stage.c` の中の enum に固定、値は errno ではなく意味）。
- 既存の詳しい行（`BXE ... ALIAS OK`、coherency の行、`compute kernel: OK` 等）は残す。段の行は「どこまで来たか」の要約。
- 同じ内容を sysctl `hw.gpu.powervr.stages`（読むだけの文字列、例 `firmware=ok render-node=ok compute=ok ta=- 3d=- transfer=- present=-`、fail は `ta=fail:timeout`）でも出す。gpu-selftest がこれを読む。sysctl の追加は driver の中で HAL の API ではない。
- 置き場所: `src/drivers/gpu/powervr/stage.c`（新規）と `powervr-private.h` に `powervr_stage_ok(stage)`・`powervr_stage_fail(stage, reason)`。各段の既存の成功・失敗の道から呼ぶ（呼ぶ所は p010a の phase.md に列挙）。

### 8.2 userland の試験 program `gpu-selftest`

- 置き場所: `userland/tests/gpu-selftest/`。VF2 の試験の image（`plan/ws269/tests/p010/config-visionfive2-selftest.mk`）に入れる。通常の image には入れない（D-P2）。
- 出力: 1 段 1 行、`gpu-selftest: <段>: ok` / `fail(<理由>)` / `skip(<前の段>)`、最後に `gpu-selftest: N ok, M fail, K skip`。
- 段（順に、各段は fork した子で、上限 10 秒（present は 30 秒）。子が時間内に終わらなければ kill して `fail(timeout)`。GPU の hang・子の crash でも報告が続く）:
  1. `kernel`: `hw.gpu.powervr.stages` を読み、その中身を 1 行で写す
  2. `node`: render の node を開ける
  3. `instance`・`device`: vkCreateInstance・device の作成（PowerVR の physical device を選ぶ）
  4. `compute`: vkcompute と同じ fill を in-process で（`vkcompute` の shader を共有）、読み返して比べる
  5. `image`: LINEAR と OPTIMAL の image の作成、LINEAR の `rowPitch % 32 == 0`
  6. `clear`: `vkCmdClearColorImage` → `vkCmdCopyImageToBuffer` → 読み返して比べる（transfer）
  7. `copy`: buffer → image → buffer の往復（transfer）
  8. `draw`: offscreen の render pass で clear と 1 つの三角形、中心と角の pixel を比べる（TA・3D）
  9. `sample`: texture を貼った四角、中心の pixel（descriptor・sampler）
  10. `depth`: 2 つの重なる三角形の depth の順（D32）
  11. `present`: `vkdemo --frames 120` を exec し、exit の値（display・WSI）。ユーザーは画面で cuboid を目で見る
  12. `frame`: `venus-frame` を exec し、exit の値
- 依存の段が fail なら後の段は `skip(<段>)`（例 `device` が fail なら全部 skip）。独立の段（`clear` が fail でも `draw`）は続ける。
- shader の SPIR-V は vkdemo と同じく glslc で作って `.spv` を tree に入れ、`regenerate.py` の流儀で出所を書く。
- 段を増やすのは各 p010 の Phase（その Phase が作る機能の段を足す）。最初の p010a は 1〜4 と、まだの段を `skip(not built)` で出す形。

### 8.3 kernel の中の試験（Venus を通さない、p008 の流儀）

- `CONFIG_POWERVR_RENDER_CHECK`（試験の image だけ）: 起動の後に kernel が自分で 1 つの render（固定の program の単色の三角形、64×64 の LINEAR の buffer に PBE で）を出して読み返し、`powervr: render check: OK|FAILED ...` の行を出す。userland と libvulkan に依らずに TA・3D・PBE を切り分ける。p010e で作る。
- 同じく transfer の check（clear と copy）を p010f で足す。

## 9. ユーザーの判断（推奨を採って進める）

| D | 問 | 案 | 推奨と理由 |
| --- | --- | --- | --- |
| D-P1 | free list の大きさと partial render | (a) 固定 16 MiB、grow なし、尽きたら fail（SPM は p011）/ (b) grow と SPM を p010 で | (a)。vkdemo・venus-frame・desktop の小さい場面には足りる（推測）。SPM は大きい（M2 `pvr_spm.c`） |
| D-P2 | gpu-selftest の置き方 | (a) VF2 の試験の image だけに入れ、起動の script が自動で流す / (b) 通常の image にも入れる / (c) 自動では流さない | (a)。UAT で画面に段が並ぶ。通常の image の起動を遅くしない |
| D-P3 | blend と color の pack の場所 | (a) driver が fragment の末尾に足す（compiler は float の RGBA）/ (b) compiler が全部 | (a) を仮に、p010b の M2 の確かめで変わり得る |
| D-P4 | submit の待ち | (a) p009 と同じく ioctl の中で待つ（上限 2 秒）/ (b) 非同期 | (a)。p010 は正しさを先に。fps は p011・WS277 p007 で測る |
| D-P5 | 段の行の言葉 | (a) 英語の短い語（`ok`・`fail(timeout)`）/ (b) 日本語 | (a)。console の font と既存の行に合わせる |

## 10. Phase の分割

LW は粗い（8 LW ≈ 実時間 2〜3 h）。合計 80 LW 前後（design.md の 60〜100 の中）。確かめの「T1」は QEMU の他の platform の回帰と build（VF2 の GPU は QEMU に無い）、「実機」は VF2 の画面をユーザーが見る（design.md §7、ユーザーが選べる試験の一覧へ）。

| Phase | 中身 | 依存 | LW | 確かめ |
| --- | --- | --- | --- | --- |
| p010a | **UAT の段の行**（§8.1 の `stage.c`、既存の firmware・render node・compute の道から呼ぶ、sysctl）と `gpu-selftest` の骨（§8.2 の段 1〜4、残りは `skip(not built)`）、VF2 の試験の image の config | p009 | 4〜6 | host 試験（理由の文字列・回数の上限・sysctl の文字列）、build warning 0（VF2・riscv64-virt・amd64）。実機: 画面に `firmware`・`render node`・`compute` の 3 行と gpu-selftest の 1〜4 |
| p010b | 資料の固定（コードなし）: M1・M2・M3 の path と blob SHA、BXE の feature の分岐（TLA・blend・twiddled の padding・PBE の linear の stride の単位・descriptor の word の大きさ）、§0 の未確認と §5.3 の 2 を確定 | p002 | 4 | 文書 |
| p010c | image・format・view・sampler: §3 の layout（LINEAR の rowPitch 32、twiddled）、format の表、texture state・sampler・PBE の word の組み立て（host でも build）、Venus の image・view・sampler・memory requirements・subresource layout・format の照会 | p010b | 6〜8 | host 試験（layout の数、word の golden を M1 の値から、Venus の frame を通す）、build |
| p010d | render の FW の object: render context（geometry・fragment）、free list（固定、D-P1）、HWRT data set、geometry と fragment の kick と依存、空の render（終わりだけの VDM、clear の background） | p010b、p006 | 8 | host 試験（FW の struct の byte の位置、HWRT の値の計算）、build。実機: kernel の check の `ta`・`3d` の段の行 |
| p010e | PBE の store と固定の program: clear の色の background、LINEAR・twiddled への store、kernel の中の render check（§8.3、単色の三角形） | p010c、p010d | 6〜8 | host 試験（VDM・PPP の stream の golden、固定の USC の bytes）、build。実機: `render check: OK` |
| p010f | transfer: transfer context、固定の program、§6 の命令の Venus の decode、kernel の transfer check | p010c、p010d | 8 | host 試験（job の組み立ての golden）、build。実機: `transfer` の段、gpu-selftest の `clear`・`copy` |
| p010g | render pass・framebuffer・begin/end・load/store・全面の clear attachments・barrier、render の job の列（§2） | p010e | 6 | host 試験（render pass の decode と job の列）、build |
| p010h | descriptor と shader の境: `shader.h`（§5.1、P2 と合意）、set layout の memory の並び、update、push constant、PDS の data の program（§5.2 の要求の表から） | p010c、q1363 の境の合意 | 6〜8 | host 試験（要求の表から PDS の program と set の bytes）、build |
| p010i | graphics の pipeline と draw: vertex・fragment の PDS、固定の state、viewport・scissor（dynamic を含む）、VDM の draw（index あり・なし）、compiler の出力を使う | p010g、p010h、q1363 の vertex・fragment の出力 | 8 | host 試験（vkdemo の shader を compiler に通した pipeline と draw の stream の golden）、build。実機: gpu-selftest の `draw`・`sample` |
| p010j | depth（D32・D16、ZLS の load・store、depth の test）と vkdemo の offscreen の通し（cuboid） | p010i | 6 | host 試験、build。実機: gpu-selftest の `depth` |
| p010k | WSI の readback の道（§7）: semaphore、`vkCmdCopyImageToBuffer` の LINEAR、host の map、`present` の段の行。vkdemo が画面に出る | p010f、p010j、p007 | 4〜6 | build。実機: gpu-selftest の `present` と vkdemo の画をユーザーが見る |
| p010l | WSI の shared の道（§7）: export・import の chain、identity、`present_copy_shared` の clear・blit・copy を LINEAR の PBE で、p007 の import の試験（design.md §6 p007 の「import の試験は p010」）。WS277 R6 の完了を Q1 へ | p010k | 6〜8 | host 試験（import・export の frame）、build。実機: vkdemo が shared の道で出る（gpu-selftest が libvulkan の選んだ道を出す、手段は p010l で決める） |
| p010m | venus-frame の道と仕上げ: venus-frame の命令の不足を埋める、gpu-selftest の `frame`、Keiland の起動の待ち（§1）の実機の確かめ、T1 に他の platform の回帰（riscv64-virt・amd64 の `boot-test.sh`）を 1 回まとめて依頼 | p010k（p010l は任意） | 4〜6 | T1: 回帰。実機: gpu-selftest の全部の段と venus-frame・vkdemo の画をユーザーが見る（p010 の受け入れ） |

順の要: p010a は p009 の後すぐ（UAT の行を先に）。p010b は p010c〜 の前。p010c と p010d は並行できる。p010h・p010i は q1363（P2）の境の合意と vertex・fragment の出力に依る。compiler が遅れても p010d〜g・p010f は固定の program で進む。p010k が最短の「画が出る」点。

各 Phase の phase.md の必須項目:
- 出典の行（`Value(s) referenced:` と M の番号・file・blob SHA）を各 source file の頭に。
- 段の行（§8.1）を、その Phase が作る段の成功・失敗の道に入れる。gpu-selftest の段をその Phase で足す。
- 実機の文面は `plan/ws269/tests/p010/` の試験の image で、ユーザーが選べる試験の一覧へ（今の試験を頼まない）。

## 11. 危険

- BXE は Mesa で unsupported（design.md S5）。BXM-4-64 の道との違い（特に TPU の並列・ISP の tile の数）で、M2 の値が BXE で違う所がありうる。p010b で feature の分岐を全部記録し、実機で外れたら kernel の check（§8.3）で切り分ける。
- FW の fragment・geometry の command の struct は FW の版（p002 で固定）に依る。mainline v7.3 の header と FW の版の組を p010b で確かめる。
- compiler（q1363）が vertex・fragment に届くまで p010i 以降は進まない。その間に固定の program で TA・3D・transfer・present（readback で clear の色だけ）を先に実機で通す（p010e・p010f・p010k の一部）。
