<!-- awesome-plan project=zedbsd record=ws290-p008 -->

# ws290-p008 pipeline と descriptor

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §18 の p008（版 4.5、ghci.md cb937e6）。
- 状態: in-progress（2026-10-28 P3 の手伝い q1644-i01: 実装と試験は済み、P3 の review 待ち）
- 依存: p007

## 範囲

pipeline と descriptor: vk-pipeline・vk-layout・layout.h

詳細は design の該当の節（§4〜§16）と §14 の p008 の場面。

## 受け入れ

- p008 の場面 PASS
- 試験は host 試験と amd64・riscv64 の build（warning 0）。実機は p014 だけ。
- 見積り: 20 LW。

## 仕様の仮の扱い（ghci-review の回答で変われば直す）

design §19: N78（属性の register の累積の base_i と header の 4 × loc + comp の食い違い）は ghci.md の累積で作る。p006 の compiler の取り決め（design §11.4 の「p006 で決めた取り決め（版 4.10）」: STORAGE の image は linear・set 3 の配置・texel buffer は slot と entry・属性の表は常に渡す・VS を先に compile・dispatch と draw の間の table の置き直し・c24〜c31 に LOAD_CONST しない・fragment の無い pipeline の FS）を守る。

## 設計の決定（p008 の helper、P3 の判断を頼む）

design §11.4・§14・§18 の p008 の範囲で、design に値の無い所を次のように決めた。p009 が使う取り決めを含む。

- **heap と配置**: executor heap の block は全部 `GHCI_VM_FLAG_EXEC` で map する（`vk-memory.c` の `heap_grow`）。pipeline は heap の 1 つの run: [PSO 128 byte（128 揃え）][VS（か CS）の code（64 揃え）][FS の code（64 揃え）][set 3 の driver の表 32 byte（spill する program が有る時だけ）]。scratch は別の run で、stage ごとに `RESIDENT_WARPS[15:0]` × program の bytes per warp（64 揃え）。表の 0x00・0x08・0x10 に VS・FS・CS の scratch の番地（`pipeline->table_va`、無ければ 0。p009 は 0 でなければ set 3 に bind する）。`RESIDENT_WARPS[15:0]` = 0 の device で spill する pipeline は断る。
- **fragment stage の無い pipeline**: compiler に `ghci_gsi1_empty_fragment()`（`gsi1-compile.c`）を足した。命令は `addi r0, zero, -1`・`exp r0, 0x105`・`end` の 3 つ、used_regs 7（rasterizer が書く r0〜r6）。仕様（§12.10.3）は「movi rT, -1 と exp rT, 0x105 の 2 命令」と書くが、compiler は -1 を addi で作り、どの program も end で終わるので、compiler 自身の終わり方に合わせた。
- **texsz.s**: compiler の program の flag に `GHCI_GSI1_FLAG_SAMPLE_QUERY`（bit 3、PSO の flag ではない）を足し（`gsi1-texture.c` の `emit_texsz`）、CAP_MSAA の無い device では pipeline を断る（p006 の review の L4）。
- **断り方（U8）**: pipeline の作成で分かる不足は全部 `VK_ERROR_OUT_OF_DEVICE_MEMORY` と log（「pipeline refused: 理由」、device の最初の 64 件）: VS と FS の slot の和 > 16・資源の表 > 128 entry/4096 byte・属性 > 60 register（compiler の拒否）、色の attachment > MAX_MRT、SRC1 の blend factor で CAP_DUAL_SOURCE 無し、logicOp で FEATURE[11] 無し、attachment ごとに違う blend で MRT_BLEND 無し、LDS > LDS_KB、workgroup > {256,256,64} か 256、texsz.s で CAP_MSAA 無し、sample 数が 1 でも（CAP_MSAA の）4 でもない、sampleShading・alphaToOne・depthBounds（feature が false）、adjacency・patch の topology、geometry・tessellation の stage、16 を超える vertex binding・属性、dynamic buffer の配列の element > 0 を使う program（下）。sampler の anisotropy は `VK_ERROR_FEATURE_NOT_PRESENT`。view: format が表に無い・texel の大きさが image と違う → `VK_ERROR_FORMAT_NOT_SUPPORTED`、cube array → `VK_ERROR_FEATURE_NOT_PRESENT`、depth/stencil の format の stencil だけの view → `VK_ERROR_FORMAT_NOT_SUPPORTED`。session に無い object・bind されていない image/buffer・型と payload の合わない write は EINVAL（frame を止める、p007 と同じ「形の違う物」の扱い）。
- **一度に複数の pipeline**: pipeline ごとに作り、失敗した物の identity は 0、結果は最初の失敗の VkResult（libvulkan の `pipeline_batch` は部分の成功を受ける）。
- **specialization**: 共有の frontend は spec constant を知らないので、executor が SPIR-V の写しを書き換える: OpSpecConstantTrue/False/OpSpecConstant を SpecId の map の値（無ければ既定値）の OpConstantTrue/False/OpConstant に、OpSpecConstantComposite を OpConstantComposite に、SpecId の OpDecorate を OpNop 4 語に。OpSpecConstantOp は断る。
- **render pass（p009 まで）**: `GHCI_VK_RENDER_PASS` の kind と `struct ghci_vk_render_pass`・`struct ghci_vk_subpass`（色の数と format、depth の format、sample 数、input attachment の format と sample 数）を vk.h に置いた。pipeline は render pass の object が有ればその subpass を、無ければ自分の state（色の数 = blend の attachmentCount、sample 数 = rasterizationSamples、input attachment 無し）を使う。p008 では render pass の object を作る op が無いので、input attachment を読む FS は compiler が format 無しで断る。render pass の release は今は `vk-pipeline.c`（p009 で `vk-pass.c` に移してよい）。
- **descriptor の byte**: sampler・view の descriptor は create の時に作り、set は write の時に descriptor の byte を持つ（draw の時は写すだけ）。番地は memory の mapping から引く（image・buffer は bind 済みが要る）。image view は 2 つ: sampled（view の level 全部、view の最初の level・layer の番地、2D array・cube の depth は layer 数・6、slice pitch は layer の stride、3D は level の depth と plane、swizzle、sRGB の flag）と single（同じ番地で mip_levels 1、swizzle は恒等、cube は 2D の 6 layer。storage image と input attachment の set 2 の写し、design 版 4.10 の取り決め）。uniform texel buffer の view: texture は幅 min(256, 要素数)・高さ ceil(要素数 / 256)・行 256 × texel の byte の linear 2D、buffer descriptor は stride = texel の byte・format。set の uniform buffer・uniform texel buffer の buffer descriptor は flags[0] ReadOnly、storage は 0。
- **dynamic offset**: pipeline layout は set 順・binding 順・element 順に descriptor 1 つに bank の 1 語（c40 + i）。compiler の `dynamic[]` には element ごとに同じ (set, binding) を並べる（compiler は最初の 1 つを使う）ので、dynamic buffer の配列の element > 0 を読む program は作成で断る。
- **draw・dispatch の表**（p009 が呼ぶ）: `ghci_vk_layout_tables(session, pipeline, sets[4], dynamic_offsets, count, push, push_bytes, tables)` が set 0（texture の slot 順に 32 byte）・set 1（sampler の slot 順に 16 byte）・set 2（entry の offset に 16 か 32 byte）・bank（c0〜c23 = push 0〜95、c32〜c39 = push 96〜127、c40〜 = dynamic offset、c24〜c31 は 0 で LOAD しない、`bank_count` = 40 + dynamic の数）を作る。slot・entry の set が bind されていない・binding が無いと ENOENT（p009 は U8 で飛ばして log）、dynamic の数が layout と違うと EINVAL。
- **pool**: type ごとの数と maxSets を数え、超えれば `VK_ERROR_OUT_OF_POOL_MEMORY`（wire は 1.1）。free は FREE_DESCRIPTOR_SET_BIT を見ない。set layout は参照を数え（set と pipeline layout が hold）、layout の destroy の後も set は使える。copy は binding を越えて続き、immutable sampler の binding は宛先の sampler を保つ。
- **pipeline cache**: data は 32 byte の header（headerSize 32、version 1、PCI の ID、`GHCI_VK_CACHE_UUID`）だけ。capacity 0 は大きさ、32 未満は 0 byte で `VK_INCOMPLETE`。初期 data は使わず、merge は確かめるだけ。
- **PSO に無い静的な状態（R07）**: pipeline の `state` に dynamic の印（VkDynamicState の bit）、viewport（float の bit 6 語）と scissor、line width（wideLines が false なので常に 1.0）、depth bias（SET_DYNAMIC_STATE 0x01 の順: constant・slope・clamp）、blend constant、stencil の各面の 1 語（compare mask・write mask << 8・reference << 16、0x03 の形）、MRT_BLEND の device だけ attachment ごとの blend の語（0x07 + k、dw11 と同じ形で `ghci_pso_encode` から取る）、rasterizerDiscard。PSO の dw9 の line width は 16（1.0）、dw12 は binding 0〜3 の stride が 255 以下の時だけ（p009 は `pipeline->strides` で bind する）。triangle fan は dw8 = 3 と `pipeline->fan`。
- **vertex 属性**: PSO の attribute slot は location の昇順に詰める。compiler には location ごとの format の成分の数と整数の印（`ghci_vk_format_traits`、`vk-format.c` に足した）を渡す。

## 記録

- 2026-10-28 P3（q1638-i01）: design §18 から立てた。
- 2026-10-28 10:50 P3 の手伝い（q1644-i01、ユーザー 2026-10-28「P3は仮の扱いで実装を行ってよいです」）: 実装と試験。commit は P3。
  - **code**: 新しい `vk/vk-layout.c`（sampler・image view・buffer view の descriptor、set layout・pipeline layout・pool・set・update・copy、draw・dispatch の HW の表と bank の `ghci_vk_layout_tables`）と `vk/vk-pipeline.c`（shader module、pipeline cache と GET_PIPELINE_CACHE_DATA・MERGE、graphics・compute pipeline: specialization、VS → FS の順の compile、heap に PSO と code と set 3、U8 の拒否、R07 の静的な状態）。`vk.h`（新しい kind 11 個・型・宣言、`GHCI_VK_CACHE_UUID`）、`vk-dispatch.c`（route 2 つ）、`vk-object.c`（release）、`vk-format.c`（`ghci_vk_format_traits`: 成分の数と整数・sRGB・depth・block）、`vk-memory.c`（heap を EXEC で map）、`vk-device.c`（UUID の macro）。compiler: `gsi1-compile.c`・`.h`（`ghci_gsi1_empty_fragment`、`GHCI_GSI1_FLAG_SAMPLE_QUERY`）、`gsi1-texture.c`（texsz.s で flag）。`platform/amd64/vmunix.mk`・`platform/riscv64/vmunix.mk` の GHCI の一覧に 2 file。p007 の表（R21）で「p008 で記録する」の op は全部 route が付いた。
  - **試験**: 新しい `plan/ws290/tests/ghci-pipeline-host-test.c` と shader（`plan/ws290/tests/pipeline/` の 10 個、gsi1 の attrib60.vert・spill.comp を glslc で）。`ghci-host-test.sh` の vk の節は compiler・frontend・isa.c・vk-layout・vk-pipeline も link するようにし、その後に pipeline の節を足した（末尾の判定の前）。場面: 三角形（PSO の 128 byte を §8.2 から手で、heap の配置と EXEC の map、静的な状態、fan、点・時計回り、fragment の無い pipeline の 3 命令、destroy）、blend（alpha blend の dw11、logic op の dw8、MRT_BLEND の attachment ごとの語）、depth・stencil（dw10、別の面、mask と reference の語、dynamic の印）、MSAA（4 sample・mask 0x5・alpha to coverage の dw8、CAP_MSAA 無しで 4 sample と texsz.s を断る）、compute（dw4〜5・dw13・dw14・dw15、dynamic storage buffer の set 2 の byte、c40・c0・c33、ENOENT・EINVAL、512 invocation と LDS_KB 超えを断る、specialization）、descriptor（sampler 16 byte・image view 32 byte・buffer view 16/32 byte を §8.3 から手で、slot の表、set 0・1・2 の byte、immutable sampler、pool の maxSets、copy、型の違う write で EINVAL、free・reset、layout の destroy の後の set）、拒否（dual source・logic op・independent blend・MAX_MRT・VS 8 + FS 9 の 17 slot・64 属性 register、flag の有る時は作れる、52 register は作れて slot は location 順）、cache（header・VK_INCOMPLETE・merge）、scratch（lane 32 register で spill、set 3 の 0x10 が scratch）。拒否の理由は log で目的の物と確かめた（一度 quiet を外して）。
  - **結果**: `sh plan/ws290/tests/ghci-host-test.sh` rc=0: words 141/0、ref 56/0、model 254/0、driver 587/0、old-names PASS（36 sources）、ghci-isa PASS、gsi1 50/0、vk 383/0、**pipeline 126/0** → PASS ghci290-host-test。build（各 timeout 1800）: `config-amd64-ghci.mk`（build/ws290-p008-amd64）・`config-visionfive2-ghci.mk`（build/ws290-p008-vf2）は最後の変更の後に build し rc=0・warning 0（vk-layout.o・vk-pipeline.o が入った）。`config/ci/config-amd64.mk`・`config-visionfive2.mk`（build/ws290-p008-*-ci）は rc=0・warning 0（GHCI は入らない）。QEMU・実機は無し。`git diff --check` は空。
  - **未決・残り**: (1) 共有の frontend（`spirv.h` の `DRV_GPU_SPIRV_MAX_GROUP_INVOCATIONS` 128）が 129〜256 invocation の workgroup を断る。device は `maxComputeWorkGroupInvocations` 256 と答える（p007）ので食い違う。frontend は範囲の外（Q1・P3 の判断）。(2) render pass の object は p009 が作る（`struct ghci_vk_render_pass` を埋める）。それまで input attachment を読む FS は作れない。(3) p009 の executor がする事: BIND_PIPELINE の度に `state` を SET_DYNAMIC_STATE・SET_VIEWPORT・SET_SCISSOR で置く、`table_va` が 0 でなければ set 3 を bind、`ghci_vk_layout_tables` の表を submit の一時の memory に置き c24〜c31 を除いて LOAD_CONST、fan の index、`strides` で vertex buffer を bind。(4) pipeline の destroy は heap の run をすぐ返す（GPU が使っている間の destroy は Vulkan が禁じるので、p009 で submit が hold するかは P3 の判断）。(5) 規約の全文の見直しは p015。
