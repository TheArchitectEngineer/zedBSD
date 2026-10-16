<!-- awesome-plan project=zedbsd record=ws039-p008 -->

# ws039-p008: Vulkan の executor（host の部分: nvrtx の写しと PM4 の組み立て）

Status: in-progress（2026-10-16 q1219 / q1219-i01 で P5 が executor を kernel に組み込み、実機 10.0.10.3 で A9 ok と vkquad の pixel の一致。Q1 の merge と clearance の判定待ち）。以前: q1214-i01 で host の部分
Disposition: normal
Parent: [WS039](../ws.md)
Queue: q1219 / q1219-i01（段 A9: executor の組み込みと実機の描画、承認 2026-10-16 ユーザー「p008に進みます」）。以前 q1214 / q1214-i01（Q1 の割当 2026-10-16、P5: 「ws039-p007 の残り」と「p008 の host の部分」）
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

## 8. q1219-i01: 組み込みと実機（2026-10-16 P5）

### 8.1 したこと

1. main（6307ecd3f、P3 の p011 を含む）を branch に merge（2e8bbc78f、衝突なし）。
2. **PM4 の定義を driver の側に寄せた**（Q1 の決定、5 節の 2）: 型 3 の header の欄・全部の opcode（driver と executor の和）・register の空間の先頭（dword の index）・index 付きの setter の位置・wait の比較と memory の poll・acquire の大きさと poll・release の data の欄を `amdrdna-hw.h` に（出典 Linux v6.19 `nvd.h`・`gfx_v10_0.c`、Mesa 25.0.7 `radv_cs.h`）。`command-format.c` は同じ名前を使い、`command_header` を公開の `amdrdna_pm4_header` に。executor の writer は `amdrdna_vk_pm4_*`（struct `amdrdna_vk_pm4`）に改名し（driver の `amdrdna_pm4_dispatch`・`fetcher_sync` と名前がぶつかった）、driver も書く packet は driver の builder で作る（register の書き込み・context の loads と clear state・memory の wait・PFP_SYNC_ME・dispatch・filler）。executor だけの物（draw・instances・event・release・acquire の cache の bit・index 付きの setter）は `amdrdna-vk-gfx10.h` に残した。packet の語は host 試験で前と同じ（170 項目）。
3. compiler の options の引数の変更に、P3 の試験の呼び手 2 つ（`amdrdna-dispatch-gen.c`・`amdrdna-engines-test.c`）を合わせた（NULL）。
4. **継ぎ目**（P3 の file の最小の変更、Q1 了承 2026-10-16）:
   - `node.c`: `amdrdna_node_create` で `amdrdna.vk=1`（既定 0）の時だけ `amdrdna_vk_node_bind`（登録の前）。`node_open` で `amdrdna_vk_node_open`、`node_close` で `amdrdna_vk_node_close`（EBUSY なら session を残す）。`node_blob_create` で blob_id があり renderer なら `amdrdna_vk_node_blob_attach`。**`node_resource_map` の blob の大きさを backing（64 KiB の単位）から頼まれた page 数に**（libvulkan は map の bytes が自分の page 数と同じでないと DEVICE_LOST にする。実機で見つけた、8.3）。
   - `vk-node.c`: `amdrdna_vk_node_bind`（`amdrdna_runtime_bind(node, execute, timelines)`、execute は `amdrdna_vk_stream_execute(session->vk, …, amdrdna_vk_dispatch)`、失敗した frame は拒んだ command を印に出す）、`_open`・`_close`・`_blob_attach`（session の space に map → node の lock で `amdrdna_vk_memory_attach` → backing の参照）、段 A9 の `amdrdna_a9_draw`。
   - `amdrdna.c`: 最後の段を A9 に。A6〜A8 は `amdrdna.disp.own=0` なら `Ax skip` の印で通る（=1 は p006 まで ENOTSUP）。A9 は `amdrdna_a9_draw`。`amdrdna-private.h`・`amdrdna-node.h` に宣言。`runtime.c` は変えていない。
5. **段 A9**: driver 自身の session で executor の session を開き、`amdrdna_vk_execute_trial`（`vk-execute.c`）: 64x64 の BGRA の画像 2 つ、1 つ目に一色の quad（青）と中の正方形 16〜48（赤）、barrier の後に 1 つ目を unnormalized の nearest で sample して 2 つ目へ写す quad、run して待ち、全 pixel を照合。印 `A9 ok draw pixels=4096 copy=4096 quads=3 words=N renderer bound`。
6. `platform/amd64/vmunix.mk`: `AMD64_AMDRDNA_VK_SOURCES`（vk/ の 20 file、codec は i915・nvrtx が build しない時だけ）。

capset が renderer を名乗る（CAPSET・COMMAND・NOTIFICATION・JOB・JOB_CAPACITY）のは bind の時だけ（runtime.c の bind が足す）。`amdrdna.vk` の既定は 0（merge の後の既定を上げるかは Q1・ユーザーの判断）。

### 8.2 host 試験と build

- `amdrdna-vk-host-test.sh` PASS（170 checks、0 failures、定義を寄せた後と命名の後）。
- `AMDRDNA_LINUX=<P3 の temp/linux> amdrdna-host-test.sh` PASS（PASS の行 308、audit-symbols 45 file PASS）。firmware と dump の項目は worktree に無く SKIP。
- `make -j16 ZEDBSD_CONFIG=plan/ws039/tests/config.mk BUILD=build/p5/usb build/p5/usb/vmunix` rc 0、warning 0、kernel include check PASS、amd64 vmunix check PASS。`build/p5/usb/bin/amdrdnactl`・`vkquad` も。
- `audit-vk.sh`（Linux の tree 無し）PASS（24 file）。**Linux の tree を渡すと FAIL 90 件**（`depth`・`view`・`handled`・`memory`・`push` など一般語、nvrtx から写した file と hardware の部分。p008 の前の 175405996 でも同じ 90 件で、今回の変更で増えていない）。allow list に足すかは Q1 の判断（driver の側は 29 語を足した前例）。

### 8.3 実機（10.0.10.3 の RX 5600 XT、USB の zedBSD は DHCP 10.0.30.5）

手順: Debian（awe@10.0.10.3）で USB の sda2 を mount し、`vmunix` を差し替え（前の物は Debian の `~/ws039/vmunix.before-p8`）、`zedbsd.cfg` に `amdrdna.vk=1` の行を足した（前の物は `~/ws039/zedbsd.cfg.before-p8`）→ `hw-run.sh boot 0001` → zedBSD に新しい `amdrdnactl`・`vkquad`・`libvulkan.so` を `/tmp` へ → `/tmp/amdrdnactl advance A9`（A2〜A9 が続けて走る）→ 状態 → `LD_LIBRARY_PATH=/tmp /tmp/vkquad` → dmesg → `hw-run.sh debian`。電源は全部 warm（Debian 経由）。証拠 `build/tmp/amdrdna-hw-p8-{1..5}/`（worktree の中、commit しない）。

| 回 | kernel（vmunix の SHA-256 の先頭） | 結果 |
| --- | --- | --- |
| 1 | 715d148c（7746e04e4） | **A9 ok draw pixels=4096 copy=4096 quads=3 words=665**、`/dev/gpu0`、vkquad は `fail instance -4` |
| 2 | 10a970a7（frame の失敗の印を足した） | A9 ok、vkquad `fail instance -4`、frame の失敗は 0。印を足した libvulkan（`build/tmp/vkdbg`、commit しない）で `context.c:674`: `GPU_RESOURCE_MAP` の bytes が頼んだ page 数と違う（node が backing の 64 KiB を返していた）→ map の大きさを直した |
| 3 | b53c4120（0d1df6e2f） | A9 ok、**`VKQUAD ok device=zedBSD amdrdna (AMD RDNA1) pixels=4096`** を 6 回（今回の libvulkan 4 回・image の libvulkan 2 回）、`amdrdnactl gpu` ok、status `sessions spaces=0 held=1 device=0x3`（REGISTERED・RENDERER）、`vk: a frame failed` 0 |
| 4 | 同上 | A9 ok、vkquad ok 2 回 |
| 5 | 同上（09573e859。命名だけの変更で vmunix は同じ hash） | A9 ok、vkquad ok 3 回、`amdrdnactl gpu` ok、spaces=0 |

vkquad（compositor の textured quad を Vulkan 1.0 で offscreen に描いて読み戻す）が確かめる物: render pass の clear（青）、buffer から 32x32 の R8G8B8A8 の texture への upload、compositor の quad.vert/quad.frag の pipeline と blend で 64x64 の B8G8R8A8 の target の中央に描く、image の copy、image の clear、target から buffer への download、全 4096 pixel の照合。

### 8.4 6 節の「実機で確かめる事」

- preamble と CLEAR_STATE の後の既定・binner off・SCAN_MODE: draw が正しい pixel を出したので、少なくとも 64x64 の target では成り立つ（個々の register の値を読んではいない）。
- barrier の列（RELEASE_MEM の GL2 の writeback と invalidate、WAIT_REG_MEM、ACQUIRE_MEM）: A9 の「描く → barrier → sample」と vkquad の copy・download が一致 → 確かめた。TCC は 16（2 の冪、A0 の `l2=16`）で、2 の冪でない時の CB の後の GL2 は対象外（この chip では起きない）。
- linear の target と T# の pitch（256 byte）、BGRA の COMP_SWAP と T# の DST_SEL: A9 と vkquad の B8G8R8A8 の target と R8G8B8A8 の texture で一致 → 確かめた。
- 32_ABGR の export を 8 bit の target に（SX_PS_DOWNCONVERT 0）、DUAL_QUAD_DISABLE: 一致 → 確かめた（色の精度は 8 bit の完全一致の範囲）。
- unnormalized の sampler（quad の copy）: A9 の copy が全 pixel 一致 → 確かめた。MEM_ORDERED の下の counter の待ち: 落ちず一致（個別の観察はしていない）。
- 頂点の V#（stride 0 の record 数）: vkquad の頂点の取り方で一致（stride 0 の場合そのものは未確認）。
- 未確認: cold power cycle の直後、compositor（keiland）の desktop（p009）、長い時間・多数の session、compute pipeline（拒んだまま）、大きな target・複数の pass の性能。

commit: 7746e04e4（組み込み）、0d1df6e2f（map の大きさ・frame の印）、09573e859（命名）。

### 8.5 再開点

1. Q1: merge と clearance の判定。`amdrdna.vk` の既定（今は 0）。audit-vk の 90 件の一般語の扱い。
2. p009: compositor が amdrdna で desktop を描く（WSI・swapchain・display は p006 の後）。
3. compute pipeline と vkCmdDispatch（7 節の 2）。
