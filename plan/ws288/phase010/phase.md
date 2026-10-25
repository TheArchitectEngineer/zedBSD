<!-- awesome-plan project=zedbsd record=ws288-p010 -->

# ws288-p010 compiler の backend（共有の IR → GSI1）

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §6.6（IR の op の対応表）、§3.4.4（binary）、§8 の p010 の行、§16（p030: U50・U51 で compiler に足した物）、§15（p027 の fragment の入出力 r6〜r9・exp 0x105）。
- 状態: cleared（2026-10-25 07:12 Q1。vmunix への link は GHCI の driver の config が要るので p006 へ移した）
- 依存: p008（GSI1）、p009（texture の命令の全部）、p027（MSAA の入出力、main に merge 済み）、p030（format・MRT・point/line などの GHCI の口。RTL は並行で実装中、compiler は GHCI の draft の仕様に合わせる）。
- 承認: 2026-10-25 ユーザー「SAはfuture workに回します。p009, msaaを優先して実装してください。そのあと、p010,p018,p019,p006,p011を実装します。…」「ゴールまで推しで進んでよいです。判断に迷ったら、Vulkan 1.0の原則論で進めてください。…N=3で、P3を立ててOKです。」

## 範囲

- zedBSD の kernel の共有の compiler（`src/drivers/gpu/compiler/`: `ir.h`・`spirv.c`・`optimize.c`）の IR から GSI1 への backend（design §6.6 の `isa/gsi1/`、置き場所は既存の backend（nvrtx・amdrdna・powervr）の形に合わせる）。§6.6 の対応表の全部の op、register の割り当て（r0〜r62、r63 は rq の予約）、EXEC の制御の流れ（loop・skip・kill）、texture の命令の全部（`.c`・`.3`・offset・gather・query・`texsz.l/.d/.s`）、vertex の出力と fragment の入力（lda、MSAA の r6〜r9、exp 0x105 の終わりの規則）、MRT の export（COLOR k = 0x100+16k+c、p030 の GHCI §12.12）。
- **`gsi1_isa.h` の binary の header と interface の表の確定**（C1、p018 が依存）: `docs/ghci.md` §12.10 の「未確定」の 4 語目の配置を決め、amaterrace-gpu の GHCI の draft も直す（amaterrace-gpu の worktree で）。
- p030 の U50（RTL の外の core の欠け: storage image・texel buffer・FragDepth・SampleMask の SPIR-V の front end）と U51（input attachment は memory の読み `lw`）の compiler の側。
- host の試験: 符号化の往復（amaterrace-gpu の `tools/gsi1dis.py` と同じ結果）、model で実行して CPU の参照と比べる（amaterrace-gpu の `tools/gsi1model.py`）、interface の表の検査。

## 受け入れ

host の試験が PASS（件数を記録）、zedBSD の vmunix の build が warning 0（名指しの target）、plan/coding-style.md の全文に合う。QEMU は使わない（p011 で Verilator の上の端から端）。

## 記録

- 2026-10-25 05:56 Q1: 新設、P2 に（q1564）。
- 2026-10-25 06:58 P2 → Q1: 段 1（GSI1 の backend の本体）zedBSD agent/p2-p010 6fd59cc1f、amaterrace p2-p010 e609dc7。`src/drivers/gpu/ghci/compiler/`（gsi1_isa.h の写し、encode・compile・io・memory・texture・image）、共有の front end に feature の bit つきの `drv_gpu_shader_parse_features`（既存の `drv_gpu_shader_parse` は features 0 で従来どおり）、SampleMask・FragDepth・Centroid・QuerySamples・storage image・texel buffer・input attachment を GHCI だけに開放。host 試験 `plan/ws288/tests/gsi1-host-test.sh` 26/26 PASS（P2 の申告）。P2 の判断: skip の block は分岐しない（SELECT の parser の約束のため）、noperspective・FragCoord.w は vertex が clip w を余分の varying に出す、VertexIndex・InstanceIndex は interface kind 13。
- 2026-10-25 07:10 P2（q1564-i01、zedBSD worktree p2-p010 基点 6eea7a2b0 → 8437d0b9f、amaterrace-gpu worktree p2-p010 基点 94c5ee2 → 6b21098）: 実装と host 試験。

## 結果（2026-10-25 P2）

### 作った物

- zedBSD `src/drivers/gpu/ghci/compiler/`（置き場所は amdrdna の `compiler/` の形）: `gsi1_isa.h`（amaterrace-gpu `include/gsi1_isa.h` の写し。著作権の行だけ zedBSD、本文は同一を diff で確認）、`gsi1-encode.c/.h`（64 bit 命令の符号化）、`gsi1-compile.c/.h`（公開 API `ghci_gsi1_compile`・`_compile_ir`・`_program_free`、値の分類・寿命・register の割り当て・算術・比較・制御・binary）、`gsi1-io.c`（vertex/fragment の入出力、prologue、export）、`gsi1-memory.c`（constant bank の配置、push・UBO・storage・atomic・shared・同期）、`gsi1-texture.c`（sampler の全 message）、`gsi1-image.c`（storage image・texel buffer・input attachment を lw/sw と format の変換で）、`gsi1-private.h`。
- 共有の front end（他の backend の動作は不変）: `drv_gpu_shader_parse_features(…, features, …)` を足し、`drv_gpu_shader_parse` は features 0 で従来どおり。GHCI の bit（`DRV_GPU_SPIRV_FEATURES_GHCI`）だけが開く物: BuiltIn SampleMask（入力・出力）・FragDepth、Centroid（`drv_gpu_shader_ir_io.centroid`）、OpImageQuerySamples（TEXTURE の message 11）、storage image・storage texel buffer・input attachment（uniform kind 4〜6、IR の `IMAGE_READ`/`IMAGE_WRITE`、`spirv-image.inc`）、R32 の image atomic（OpImageTexelPointer、ATOMIC の `DRV_GPU_IR_IMAGE_TEXEL`）、texel buffer の size、MS array の fetch、compute の image。
- amaterrace-gpu: `include/gsi1_isa.h`（正本: header・flags・interface の kind と kind-specific の語・operand・field・opcode・export target）、`docs/ghci.md` §12.10 の確定（flags [5]〜[10]、kind 1〜13 の語の配置、varying の語の規則と clip w の varying、constant bank の約束、fragment の終わりの規則）、`tools/gsi1model.py` に `--prims`（fragment warp の lda）。

### 自分で決めた事（Vulkan 1.0 の原則で）

1. skippable block は分岐しない（全 lane で走らせる）: parser の「block の中の値は predicate の選択でだけ見える」が block の中の SELECT 自身には成り立たず、bnone で飛ぶと結果を誤った（float.comp・loop.comp で再現）。i915 のような region の検査を入れるまで飛ばない。
2. noperspective の入力と FragCoord.w: PT の position の語は primitive table に写されない（atg_rx_frag）ので、vertex が clip の w を余分の varying 語に出し（option `has_w_word`/`w_word`、interface の kind 2/3 binding 0xFFFFFFFF）、fragment が s_i = b_i w_i / Σ b_j w_j と 1/Σ を prologue で一度作る。
3. VertexIndex・InstanceIndex は GSI1 に特殊 operand が無いので interface kind 13（system input、register を名指し）で firmware に届けてもらう（p018 の仕事: 恒等の buffer の属性など）。
4. 定数は register を持たない: 特殊 operand（0・1・1.0・0.5・2.0・-1.0）か、使う所で scratch に入れる（parser の共有の定数が長く生きて 32 本を潰さないため）。小さい定数は addi・andi 等の即値、2 の冪の積は slli。
5. 出力は各 channel を最後の store の値で終わりに export（1 回だけ・loop の外なら値の register から直接）。fragment は最後に必ず SAMPLE_MASK（program の値か全 1、`addi rT, zero, -1`）。FragCoord.z は `fadd.s.sat`（4x と DEPTH_CLAMP[1] で r3 が clamp されないため）。
6. storage buffer の load は、robustBufferAccess の時と、graphics の段で parser が predicate を付けない load で、offset を `minu(off, bytes-4)` に clamp（範囲の外の lane が aperture の外を読まないため）。store・atomic は robust の時だけ clamp。
7. 大きな UBO は bank に入らなければ kind 12（lw の道、U50）。storage image の範囲外は読みが 0、書きは EXEC から外す。
8. fragmentStoresAndAtomics・vertexPipelineStoresAndAtomics は compiler として可（U49 の残り、p019 で 1 を報告してよい）。
9. GSI1 v1 に無いので拒む物: shared memory の atomic（Vulkan 1.0 core の要求: **規格に合わない点**、ISA の追加が要る）、cube array（imageCubeArray は任意）、dual-source、gl_Layer、geometry。

### 確認（自分で実行）

- host 試験 `plan/ws288/tests/gsi1-host-test.sh`（`AMATERRACE_GPU=<amaterrace-gpu>`）: **29 passed, 0 failed**（数えた値。符号化の既知の 54 語が gsi1asm の結果と一致し gsi1dis で往復、全 28 module の code が gsi1dis → gsi1asm で同じ語に戻る（`.inst` 無し）、compute 14・fragment 8・vertex 2 を gsi1model で走らせ IR の参照（`gsi1-reference.c`）と一致、texture 系 4 は往復と命令の確認。model に TU が無いので texture の実行は未実施）。
- 他の backend の host 試験（front end の変更の回帰）: `plan/ws039/tests/amdrdna-compiler-host-test.sh` PASS（PASS 行 46、FAIL 0）、`plan/ws037/tests/nvrtx-sm75-host-test.sh` 287 checks 0 failures。bcm2711（`plan/ws141/tests/shader-host-test.sh`）は `plan/ws141/temp/mesa` が worktree に無く**未実施**、i915 の compiler の試験も**未実施**。
- kernel の flag での build（名指しの target、warning 0）: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/kern64/src/drivers/gpu/ghci/compiler/gsi1-*.o …/compiler/spirv.o optimize.o`、同じく `config-zybo.mk` の `build/zybo/kernel/…`: どちらも exit 0、warning 0。GHCI の driver の config が無いので vmunix への link は p006 で（**vmunix の link は未実施**）。
- QEMU・Verilator・板: 使っていない（p011 で）。

### 残件

- 性能: 長い命令の結果を遠ざける scheduling、FNEG/FABS の修飾への畳み込み、texture の parameter の重複の copy の削除は未実装（正しさは scoreboard が守る）。ZYBO の 32 本で texture.frag・cubegrad.frag は register が足りず拒む（64 本では通る）。
- shared memory の atomic（上の 9）、atomic の load/store の image texel は拒む。
- model（gsi1model.py）に TU が無いので texture の命令の実行の確かめは p011（Verilator）。
- §6.6 の対応表の「拒む」の行（offset・gather・compare）は p009 で入った物に置き換わった（実装は全部の形を出す）。design の表の更新は Q1 に。
- 2026-10-25 07:12 Q1: zedBSD の agent/p2-p010 を main に merge（衝突 1: この phase.md、両方を残した）。P2 が commit し忘れた試験の shader 2 つ（msarray.frag・texelbuffer.comp、host 試験が使う）を Q1 が branch に commit（0c7d5d3f2）。amaterrace-gpu の p2-p010 6b21098 を main に（afdd3f7、衝突なし）。merge の後 `AMATERRACE_GPU=~/amaterrace-gpu plan/ws288/tests/gsi1-host-test.sh` 29 passed 0 failed（Q1 が流した）。P2 の build の残り（build/p010、build/amd64、build/zybo、build/tmp の一時、build/llvm の symlink）を Q1 が消した。
  - Q1 の判定: cleared。受け入れの「vmunix の build が warning 0」は GHCI の driver の config が無いので名指しの object の build（amd64・zybo の config、warning 0）で代え、vmunix への link は p006 へ移す。texture の命令の実行は model に TU が無いので p011（Verilator）で確かめる。bcm2711（ws141 の temp の mesa が要る）と i915 の compiler の host 試験は未実施 → T に回す（i915）。
  - **Vulkan 1.0 core の食い違い**: shared memory の atomic（Workgroup の OpAtomic*）が GSI1 v1 に無く compiler が拒む → p030 に段 S15（LDS の atomic: GSI1 の命令、atg_su_lds、compiler）を足した。
  - 残件: latency の scheduling・FNEG/FABS の修飾への畳み込み・texture の parameter の二重の写し（速さ）、32 register の構成で texture.frag・cubegrad.frag が溢れる（KC705 は 64）、design §6.6 の表の古い行（Q1 が直した）。
- 2026-10-25 08:07 Q1: S13 の残件 (a)（64 component を全部書く vertex program を register 不足で拒む）を p010 の続きとして P1 に（q1567）。出力の export を最後の書き込みの直後に移す形を先に、足りなければ spill。
- 2026-10-25 08:20 P1（q1567-i01、zedBSD worktree p1-p030 基点 6c7a5e918 → c178c2063）: vertex program の export を各 channel の最後の store の直後に移した（`gsi1-io.c`: `gsi1_io_plan_outputs` が vertex では channel ごとの最後の store を `output_last` に記録、`lower_vertex_output` がその store で値の operand から直接 `exp`（position の w は w の varying にも）、それより前の store は何も出さない、終わりの export は fragment だけ）。loop の中の最後の store は、その pass の EXEC の下で loop の中で export する（lane は pass の終わりでだけ抜け、最後に走る pass は必ず最後の store を通るので、終わりに export するのと同じ値が残る）。fragment は変えていない（discard は export の前、SAMPLE_MASK は最後の規則のため）。spill は入れていない（下の確認で不要）。
  - 試験: 新 `tests/gsi1/varying64all.vert`（location 0〜15 の 64 component と position を全部書く）、新 `tests/gsi1/varyingloop.vert`（lane ごとに長さの違う loop の中で varying を書き、loop の後でも書く）を `gsi1-host-test.py` の CASES に。harness（`gsi1-host-test.c` の `run_vertex`）が PT の期待値を location 0〜14 しか作っていなかった（15 が比べられていなかった）のを 0〜15 に直した。
  - 確認（P1 が実行）: `AMATERRACE_GPU=/home/awe/amaterrace-worktrees/p1-p030b plan/ws288/tests/gsi1-host-test.sh` **33 passed, 0 failed**（変更前は新 2 case を足して 31 passed・1 failed: varying64all.vert が「the program needs more registers than a lane has」）。varying64all.vert は 32 register の構成で nregs 13（269 命令）、varying64.vert は nregs 21 → 13、basic.vert 12 → 10。compute・fragment の全 module（texture・cubegrad を含む）と image.vert の `.bin` は変更前と byte で同一（cmp）。mutation: 最初の store で export するように変えると varyingloop.vert が FAIL（192 differences）、戻して元と同一を cmp で確認。名指しの object の build `make ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/kern64/src/drivers/gpu/ghci/compiler/gsi1-{compile,io,encode,memory,texture,image}.o`: exit 0、warning 0。QEMU・Verilator・板は使っていない。
  - 残件: 値を全部先に計算してから 64 component を store する vertex program（例: 16 個の vec4 の一時を作った後で出力に書く）は、export の位置では足りず、32・64 register のどちらでも register 不足で拒む（P1 が `build/tmp/p1-late/late.vert` で確かめた、commit していない）。Vulkan 1.0 の原則では pipeline の作成は register の都合で失敗できないので spill が要るが、GSI1 の LDS は SU に 1 つで warp ごとの base が無く（compute の workgroup と並行の graphics warp が衝突する）、spill の置き場は firmware が warp ごとに割り当てる scratch の memory（interface の新しい kind、GHCI §12.10 と p018 の firmware）が要る。compiler だけでは閉じないので Q1 の判断に回す（代わりの軽い手: IR の純粋な命令を使う所へ沈める scheduling で、この形の多くは spill 無しで通る）。
- 2026-10-25 08:17 Q1: P1 の q1567（agent/p1-p030 06efbb375）を main に merge。vertex の export を出力の最後の store の直後に（64 component の varying を全部書く program が 32 register で通る、spill 無し）。P1 の申告: gsi1-host-test 33/0、compute・fragment の全 module の binary は byte で不変、MUT で FAIL、object の build warning 0。P1 の build の残りを Q1 が消した。
  - 残件（Q1 の判断）: 値を先に全部計算してから store する program は register が足りない → **spill**（Vulkan 1.0 の原則: 正しい shader を register の都合で拒まない）。LDS は SU に 1 つで warp ごとの base が無いので、置き場は firmware が warp ごとに割り当てる scratch の memory（global の lw・sw、base は constant bank、warp の番号は warp-info の operand）。先に IR の純粋な命令をその使用の近くへ沈める scheduling を入れ、残りを spill で。GHCI §12.10 に interface の kind、firmware の割り当ては p018（P2）。q1568 で P1 に。
- 2026-10-25 09:03 P1（q1568-i01、zedBSD worktree p1-p030 基点 053e135cf、amaterrace-gpu worktree p1-p030b 基点 08ffd1a → 2fb1541（ISA・RTL）→ aa3b670（gsi1model の --scratch））: register が足りない program を拒まない（Vulkan 1.0 の原則）。
  - compiler の 3 段: (1) 今のまま（既存の program の binary は不変）、(2) register が足りなければ純粋な命令を最初の読み手の直前へ沈める（新 `gsi1-schedule.c`。対象は算術・比較・SELECT・LOAD_PUSH・fragment の LOAD_INPUT、一度だけ定義される値だけ。loop・skip の block の始め・終わりを越えない（読み手が後の block の中なら block の直前まで、自分の block の終わりで止まる）、source の定義（loop 変数の MOVE）を越えない）、(3) それでも足りなければ (2) に spill（新 `gsi1-spill.c`）。
  - spill: 値を warp の scratch memory（GHCI §12.10 kind 14）に置く。prologue で lane の address を専用の register に作る（movi 語数（最後に埋める）、mul warp、add lane、slli 2、add bank の base）。register が無い時、今の命令が読む・定義する値以外で次に読まれるのが最も遅い値（連続の run なら値の数が最少、同数なら次の読みが遅い run）を spill。store は値の各定義の直後に後から挿入（その定義の EXEC の下なので memory は register と同じ値、loop 変数は各 MOVE の後）、読みは命令ごとに scratch へ lw、spill 後の再定義（loop 変数の MOVE）は scratch に作って直後に store。store は sw.x（helper・killed lane も書く）。scratch の語は、前の値の命の終わり（最後の読み・書き）が次の値の最初の定義より前なら共有。開いている loop の中で、その loop より前に作られ loop の本体ですでに読まれた値は spill しない（次の pass がその register を読み直すため、spill.comp で再現した誤りの直し）。入力の register・EXEC の退避・fragment の出力 register は spill しない。
  - ISA（Q1 の指示、amaterrace-gpu 2fb1541）: `sw.x`（STORE の sub 1、EXEC の全 lane が書く、helper・killed lane も）、sub 2〜31 は ISA fault。RTL は atg_su_dec_tab（STORE の def）と atg_su_lsu（sw の sub[0] で EXEC の mask）。KC705 の atg_top は SU 4 つなので `warp` operand を GPU で一意に（atg_su_opd の WARP_BASE = SU_INDEX × NWARPS、1 SU では今と同じ）。GHCI §12.2・§12.3・§12.5・§12.6・§12.8・§12.10（kind 14: set 0、binding = address の bank word、specific = bytes per warp（128 の倍数）。firmware が program ごとに bytes per warp × GPU の resident warps を確保し、draw・dispatch の前に bank word に書く）、gsi1_isa.h（GSI1_SW_X、GSI1_KIND_SCRATCH、zedBSD の写しも同一）、gsi1asm・gsi1model（sw.x、`--scratch ADDR,BYTES` で warp が自分の part の外に lw・sw すると止まる）、spec の atg_su_dec・atg_su_lsu・atg_su_opd。
  - 試験（P1 が実行）: `AMATERRACE_GPU=/home/awe/amaterrace-worktrees/p1-p030b plan/ws288/tests/gsi1-host-test.sh` **37 passed, 0 failed**（数えた値、新 4 case: late.vert（16 個の vec4 を先に全部作ってから 64 component を書く、沈めるだけで通り spill しない: 10 register）、spill.vert（16 個の vec4 の鎖が全部同時に生きる: 32 register、sw.x 36・lw 36）、spill.comp（40 語を先に読み lane ごとに長さの違う loop、64 lane = 2 warp、2 workgroup: sw.x 31、model の `--scratch` の検査つき）、spill.frag（40 語を先に読み、quad の差分、cover に helper lane: sw.x 30）、encode に sw.x の既知の語を 1 つ）。既存の 32 module の `.bin` は変更前（053e135cf の export）と cmp で全部同一。mutation: sw.x を sw にすると spill.frag の export が 3 語違う（helper lane の読み戻し）、loop の検査を外すと spill.comp が 92 語違う、warp を掛けないと model が「warp 1 reaches scratch address … outside its part」で止まる、沈めないと late.vert が spill する（どれも戻して PASS）。名指しの object の build `make ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/kern64/src/drivers/gpu/ghci/compiler/gsi1-{compile,io,encode,memory,texture,image,schedule,spill}.o`: exit 0、warning 0。amaterrace-gpu: `sim/run.sh atg_su_opd atg_su_dec atg_su_lsu atg_su_gfx atg_su:+quick` 全部 PASS（atg_su_lsu の store に sw.x を混ぜ、atg_su_gfx の gfx.s に helper・killed lane の sw.x → lw の往復、tools/su-tests.sh で作り直して変わったのは gfx.* だけ、dec_table.hex を作り直し）、make lint clean、tools/gsi1dis-test.py PASS。RTL の mutation（LSU が sub を見ない）: atg_su_lsu FAIL、atg_su_gfx FAIL 21 行（helper lane の r5）、戻して PASS。QEMU・Verilator の top・板は使っていない。
  - 未実施（T に）: amaterrace-gpu の atg_top_cs・atg_top_tex・atg_top_shade・atg_su（full）など長い sim、Verilator。
  - 残件: 新しい file `gsi1-schedule.c`・`gsi1-spill.c` を GHCI の driver の build（p006）の object の一覧に足す。入力の register（vertex の attribute）は spill しないので、attribute の component が register の数に近い vertex program は拒む（VF が register に直接書く GHCI kind 1 の形の制限）。firmware（p018）の kind 14 の確保と bank word の書き込み。spill は命令ごとに読み直す単純な形で、速さの工夫は無い。
