<!-- awesome-plan project=zedbsd record=ws288-p010 -->

# ws288-p010 compiler の backend（共有の IR → GSI1）

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §6.6（IR の op の対応表）、§3.4.4（binary）、§8 の p010 の行、§16（p030: U50・U51 で compiler に足した物）、§15（p027 の fragment の入出力 r6〜r9・exp 0x105）。
- 状態: in-progress（2026-10-25 05:56 q1564 P2。07:10 実装と host 試験を終え、Q1 の判定待ち）
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
