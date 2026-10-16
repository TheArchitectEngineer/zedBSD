<!-- awesome-plan project=zedbsd record=ws039-p008 -->

# ws039-p008: Vulkan の executor（host の部分: nvrtx の写しと PM4 の組み立て）

Status: in-progress（2026-10-16 q1214 / q1214-i01 で P5 が host の部分を書いた。実機前。GPU への投入（ring・fence）・renderer の node（RENDER の role、WSI、swapchain）は P3 の p005・p011 の口に依存し未着手）
Disposition: normal
Parent: [WS039](../ws.md)
Queue: q1214 / q1214-i01（Q1 の割当 2026-10-16、P5: 「ws039-p007 の残り」と「p008 の host の部分」）
承認: 2026-10-16 ユーザー「RDNAのコンパイラや、パイプラインやexecutorの組み立てなど、別なサブエージェントで先に進められることがあれば、進めてオーケーです」（Q1 経由）
依存: p007（compiler、P5）。GPU で走らせる確かめは p005（GFX の ring・fence）と p011（node・memory・session）に依存する（P3）。
実行者: P5（phase-runner）

## 1. 範囲（このPhase の今回の分）

design.md の判断 5 / 9.4 / 16 節の p008 の行のうち host で作って確かめられる部分:

1. nvrtx の executor の hardware に依らない file を `src/drivers/gpu/amdrdna/vk/` に写し、amdrdna の名前にする。
2. hardware の部分を PM4 の packet の組み立て（draw・dispatch・記述子・state）として書く。
3. host 試験: PM4 の列を期待と比べる、compiler の program と記述子・register の値が合う。
4. GPU への投入（ring・fence）は P3 の口の宣言だけ（`amdrdna-vk.h` の「provided by the node」の関数）。実機には触らない。

範囲外（再開点に残す）: node の側（`vk-node.c` に当たる物、renderer の node・WSI・swapchain）、kernel の build への組み込み、実機。

## 2. 写した file と切り分け（nvrtx → amdrdna）

| amdrdna の file | 元 | 切り分け |
| --- | --- | --- |
| `vk-object.c`・`vk-session.c`・`vk-stream.c`・`vk-dispatch.c`・`vk-reply.c`・`vk-memory.c`・`vk-queue.c`・`vk-record.c`・`vk-command.c`・`vk-target.c` | nvrtx の同名 | 名前だけ（`nvrtx_` → `amdrdna_`）。 |
| `vk-device.c` | nvrtx `vk-device.c` | PCI の vendor（`amdrdna-regs.h`）、device の名前「zedBSD amdrdna (AMD RDNA1)」。limits と features は nvrtx と同じ（executor の模型が同じ）。 |
| `vk-resource.c` | nvrtx `vk-resource.c` | pitch の整列 256 byte（GFX10 の linear の行）、sampler に compare（`compare_enable`・`compare_op`）。 |
| `vk-layout.c` | nvrtx `vk-layout.c` | uniform buffer と storage buffer の binding を受ける（`layout_slot_buffer`、slot に buffer・offset・range）。nvrtx は combined image sampler だけ。 |
| `amdrdna-vk.h`・`amdrdna-vk-objects.h` | nvrtx `nvrtx-vk.h`・`nvrtx-vk-objects.h` | program は compiler の結果（`struct amdrdna_gfx10_program`）・register の値・code の address。pipeline に compute の program の欄。 |
| `vk-pipeline.c` | nvrtx `vk-pipeline.c` | compile は `amdrdna_gfx10_compile`。fragment を先に compile し、primitive 番号を読むなら vertex に `export_primitive_id`。code を 256 byte 整列で写す。storage・uniform buffer を許す（set・binding が descriptor set の範囲、texture は 8 まで、push は 256 byte まで）。topology は point・line・line strip・triangle・strip・fan。compute pipeline は nvrtx と同じく拒む。 |
| `vk-execute.c` | nvrtx `vk-execute.c` を書き直し | 下の 3 節。 |
| 新: `amdrdna-vk-gfx10.h`・`vk-pm4.c` | — | PM4 の type-3 の packet と register の offset・欄（出典つき）、間接 buffer の writer（packet は全部か無し、溢れは `full`）。 |
| 新: `amdrdna-vk-exec.h`・`vk-state.c`・`vk-descriptor.c`・`vk-quad.c`・`vk-float.c` | `vk-quad.c` は nvrtx の draw-test の IR、`vk-float.c` は nvrtx の float の道具 | state の書き込み、T#・S#・頂点の format、executor 自身の quad の IR、kernel に浮動小数が無いので bit の上の固定小数の計算。 |

## 3. hardware の部分の形

- **run**（session ごと 2 MiB、`amdrdna_vk_gpu_alloc`）: 間接 buffer 1 MiB、draw ごとの資源の表 768 KiB、barrier の語、zero page、quad の角。
- **buffer の始め**（`amdrdna_vk_state_start`）: CONTEXT_CONTROL・CLEAR_STATE、GFX10 の graphics と compute の preamble（`ac_cmdbuf.c`・`radv_queue.c`）、CPU が書いた物を読み直す ACQUIRE_MEM（I$・K$・V$・GL1・GL2・metadata）。design 9.3 の「IB の先頭で毎回でもよい」の形。
- **draw**: program ごとに資源の表（compiler の配置: push → storage の V# → uniform の V# → T#＋S# → 頂点の V#）、target（CB_COLOR0_*）、viewport（XSCALE〜ZOFFSET・ZMIN/ZMAX・VPORT_SCISSOR、固定小数で計算）、scissor、blend（CB_BLEND0_CONTROL、alpha が違う時だけ separate、無い時は SX の shortcut を off）、culling、program（PGM_LO/HI・RSRC1/2・user SGPR: VS は表・base vertex・first instance・instance の mask、PS は表）、VS の出力・PS の入力・colour の export・DB_SHADER_CONTROL、VGT_SHADER_STAGES_EN（legacy VS、wave32）、PRIMITIVE_TYPE（index 1）・GE_CNTL、NUM_INSTANCES・DRAW_INDEX_AUTO。
- **barrier**（pass の終わり、transfer の前後、buffer の終わり）: PS・CS の partial flush、CB の metadata の flush、RELEASE_MEM（CACHE_FLUSH_AND_INV_TS、GL2 の writeback と invalidate、session の通し番号を書く）→ WAIT_REG_MEM → PFP_SYNC_ME → ACQUIRE_MEM（K$・V$・GL1）。
- **transfer**: copy・blit・clear・pass の clear は quad（textured か一色）。buffer ↔ image は buffer の始まりと行が 256 byte の時は buffer を linear の image（行の長さの幅）として draw、他は CPU が写す（その前に run を流して待つ）。
- **dispatch**（`amdrdna_vk_state_kernel` と `amdrdna_pm4_dispatch`）: COMPUTE_PGM・RSRC1/2/3・RESOURCE_LIMITS・START・NUM_THREAD・USER_DATA、DISPATCH_DIRECT（compute の bit、initiator 0x8045）。pipeline は compute を拒むので今は試験からだけ使う。
- **node の口**（P3 が node に実装する、`amdrdna-vk.h`）: `amdrdna_vk_blob`・`device_id`・`heap_bytes`・`memory_detach`・`memory_import`・`gpu_alloc`・`gpu_free`・`gpu_run`（IB の va と byte、完了まで待つ）・`gpu_copied`・`gpu_drawn`・`blob_read`。nvrtx の `vk-node.c` と同じ形。

## 4. 試験（host）

`plan/ws039/tests/amdrdna-vk-host-test.sh`（新）: libvulkan の wire の writer と codec で命令を書き、executor の本物の dispatcher に通す。node は偽物で、run の間接 buffer を packet ごとに decode して register の状態を持ち、draw ごとに register の写しを取る。各 draw を compiler の試験の GFX10 の interpreter（`gfx10-machine.c`）で走らせ直す: register が指す program と資源の表を偽の GPU memory から、頂点の wave の位置を viewport に通し、scissor の中の 8 個の 2x2 の pixel の wave を補間して走らせ、colour を期待と比べる。

確かめる事: memory・image・view・sampler・pipeline の object（nvrtx の試験の写し）、pipeline の code と register の値、submission の 2 run（間に CPU の copy）、各 run の始め、barrier 13 個（新しい値を書いて待つ）、draw 7 個の register（target・blend・viewport・scissor・program・PS の入力・export）、表の中身（push・角の V#・image 0x503 の T#・sampler の S#）、replay: pass の clear の色、compositor の quad.frag の bilinear の sample（独立の参照と ±0.002）、image の copy（1 対 1）、buffer からの 2 つの upload（blue first）、image から buffer への download、image の clear。単体: T#・S#・頂点の format、viewport の算術（上向き、半端な pixel）、blend、target、topology、writer の溢れと詰め物、compute の dispatch、compositor の 15 module が compile でき表が収まる。

結果（2026-10-16）:
- `amdrdna-vk-host-test.sh` PASS（170 checks、0 failures）。
- わざとの誤り 8 個で FAIL になることを確かめた（blue first の入れ替え忘れ、頂点の stride、viewport の offset、T# の位置、push の位置、PS の表の address、download の target の幅と source の位置）。最初の版では download（image → buffer）が試験に無く 1 個を見逃したので download の record を足した。
- kernel の flag（`-Wall -Wextra -Werror -mgeneral-regs-only` など vmunix と同じ）と `-Wdeclaration-after-statement` で `vk/` の 20 file とも warning 0（object だけ作る。node の関数が無いので link はしない）。
- `amdrdna-compiler-host-test.sh` PASS（MEM_ORDERED の追加と試験の texture の道具の拡張の後）。`make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk vmunix` exit 0、warning 0（compiler は build に入っている、`vk/` は入れていない）。
- `audit-vk.sh` PASS（`vk/` の 24 file）。出典の表は `plan/ws039/amdrdna-compiler-license-audit.md` の「executor の hardware の部分」。

commit: `3f0bda33c`（compiler の MEM_ORDERED）、`ce53cf96f`（`vk/` と試験）。

## 5. driver（P3）との口で合わせる事

1. `amdrdna-vk.h` の node の関数（2 節の最後）は P3 の p011 の node が実装する。`gpu_run` は IB を session の VMID で GFX の ring に積み、完了まで待つ（design 9.4 の模型）。
2. PM4 の opcode と register の offset を `vk/amdrdna-vk-gfx10.h` に置いた。**Q1 の決定（2026-10-16）**: driver の側（P3 の p005、`amdrdna-hw.h`・`command-format.c` の PACKET3 の定義）を正とする。p005 が main に入った後に、`vk/` をそれに合わせる（同じ定義を二か所に持たない）。それまでは今のままにする。
3. preamble を executor が IB の始めに毎回書く（design 9.3 が許す形）。queue の reset の後に kernel が出し直す preamble（11.3）とは別に持つ。
4. compiler の口: `amdrdna_gfx10_compile(..., options, ...)`、registers の `primitive_id_enable`・`interpolation_control`、RSRC1 の MEM_ORDERED。T#・S# は executor が作る（`vk-descriptor.c`）。SPI_PS_INPUT_ADDR = ENA。
5. driver 全体の `audit-symbols.sh` は `compiler/` だけを除くので、`vk/` を merge すると一般語で FAIL になる（`audit-allow-vk.txt` の 30 語）。**Q1 の決定（2026-10-16）**: driver の `audit-symbols.sh` は `vk/` と `compiler/` を歩かない形にする。`vk/` は `audit-vk.sh` が、`compiler/` は compiler の監査が受け持つ。直すのは merge の時に、Q1 が P3 に頼む。
6. 出典の版: design 9.2 は driver の側の出典を Mesa 25.3.6 とする。`vk/` は実際に読んだ 25.0.7 で引用し、block ごとに版を書いた。**Q1 の決定（2026-10-16）**: 実際に読んだ 25.0.7 でよい。design との違いはここと監査の表に書く。
7. main への統合: **Q1 の決定（2026-10-16）**: p008 の host の部分は、実機で確かめる（p011 の session の後）まで main に入れず、branch に置いておく。ws.md の p008 の行と Status は、Q1 が merge の時に直す。

## 6. 実機で確かめる事（未実施、実機前）

- preamble の値（CU mask・cache の policy・binner off の GFX10 の値・SCAN_MODE_1 の walk）と CLEAR_STATE の後の既定。
- barrier の列（RELEASE_MEM の GL2 の writeback と invalidate、WAIT_REG_MEM、ACQUIRE_MEM）で copy の後の読みが正しいか。TCC が 2 の冪でない時の CB の後の GL2 の writeback（design 9.3）。
- linear の target と T# の pitch が幅から決まる（256 byte）こと、BGRA の COMP_SWAP と T# の DST_SEL。
- 32_ABGR の export を 8 bit の target に書く変換（SX_PS_DOWNCONVERT 0）、DB_SHADER_CONTROL の DUAL_QUAD_DISABLE。
- unnormalized の sampler での sample（quad の copy）、MEM_ORDERED の下の counter の数の待ち（p007）。
- 頂点の V#（stride 0 の record 数 0xffffffff）。

## 7. 再開点

1. node の側（P3 の p011 の口が main に入ってから）: `vk-node.c` に当たる物（renderer の node、RENDER の role、WSI、swapchain）、`AMD64_AMDRDNA_VK_SOURCES` を vmunix.mk に別の変数で足す（node が無いと link できないので今は入れていない）。
2. compute pipeline（nvrtx と同じく拒んでいる）と vkCmdDispatch の record。
3. 実機（6 節）は p005・p011 の後。
