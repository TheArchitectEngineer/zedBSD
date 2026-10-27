<!-- awesome-plan project=zedbsd record=ws290-p006 -->

# ws290-p006 compiler

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §18 の p006（版 4.5、ghci.md cb937e6）。
- 状態: in-progress（実装と host 試験・build は済み。P3 の review と commit 待ち、2026-10-28 08:44）
- 依存: p002・p004

## 範囲

compiler: header・interface 表を捨て §12 の ABI（`gsi1-abi.h`）、資源の表、options、`compiler/gsi1_isa.h` を bea1ef9 の値（`GSI1_COLOR_ATTACHMENTS` 8、`GSI1_EXP_COLOR0_SRC1`、`GSI1_VS_REG_*`、lda の word 0〜3）に直す（R18）、FS の key に primitive の種類（R08）、compiler を build に戻す、gsi1 の試験と tools を移す

詳細は design の該当の節（§4〜§16）と §14 の p006 の場面。

## 受け入れ

- gsi1 の試験 PASS、build の warning 0。graphics の I/O は cf8be08 §12.10.3 で
- 試験は host 試験と amd64・riscv64 の build（warning 0）。実機は p014 だけ。
- 見積り: 22 LW。

## 仕様の仮の扱い（ghci-review の回答で変われば直す）

design §19 の表のうち、この Phase の code に効く物を着手の時にここへ写す。

- design §19: 仕様（d9252e4）の残りは無い。この Phase の code に効く決まった事は N66（operand 120・121、VS だけ、N77）、N62（sampler slot `w1[9:6]`）、N65・N74（texture と sampler の slot は VS と FS の和で 16）、N76（`lw`・`sw` は bits 1:0 を無視）。全部 code に入れた。

## 記録

- 2026-10-28 08:44 P3 の手伝い（q1644-i01、ユーザー 2026-10-28「P3は仮の扱いで実装を行ってよいです」）:
  - **commit**: 86ab21fc3（compiler・isa.c・2 つの vmunix.mk の GHCI の一覧）。その後の変更（下の file）は Q1 の規則の変更（P3 経由「commit しない」）で未 commit のまま P3 に渡した。
  - **code**: `src/drivers/gpu/ghci/compiler/` を GHCI 1.0 の ABI に書き直した。program は code だけ（header・interface 表を作る code を消した）。新しい `gsi1-abi.h`（bank: c0〜c23 push 0〜95、c24〜c31 鏡、c32〜c39 push 96〜127、c40〜 dynamic offset。set 0・1 = slot、set 2 = 資源の表（16 byte の単位、buffer descriptor か texture descriptor の写し）、set 3 = driver の表（scratch の番地を VS 0・FS 8・CS 16）。descriptor の field の offset。属性の 60）。`gsi1-compile.h` の program の記録（code、used_regs、lds_bytes、local_size、flag（centroid・depth export・discard）、varying_count、attribute_registers、color_outputs、push_bytes、dynamic_used、scratch_bytes、pipeline の表 `ghci_gsi1_bindings`（texture・sampler の slot、資源の表の entry = Vulkan の set・binding・要素・種類・offset））と options（registers 0 = 63、samples、primitive（三角形・線・点、R08）、robust、属性の表 `attribute_components`・`attribute_integer`、dynamic の一覧、他の stage の表）。VS と FS の表は FS を VS の表から続けて compile して共有する（同じ binding は同じ slot・entry）。`compiler/gsi1_isa.h` は d9252e4 の値（`GSI1_COLOR_ATTACHMENTS` 8、`GSI1_EXP_COLOR0_SRC1`、`GSI1_VS_REG_*`、`GSI1_VTX_INDEX` 120・`GSI1_INST_INDEX` 121、`GSI1_W1_SAMP_SLOT_SHIFT` 6、lda の word 0〜3）にし、header・interface の定義を外した。I/O は §12.10.3（属性を location 順に詰めた register、足りない成分は 0・0・1（整数は 1）、60 を超えると断る、index は operand 120・121、export POS・VARYING 4L+c・COLOR(k,c) k=0〜7・DEPTH・SAMPLE_MASK・COLOR0_SRC1、noperspective と FragCoord.w は lda の word 3、flat と点は vertex 0、線は vertex 2 を読まない、点の FragCoord.w は 1/w0）。encoder に sampler slot の field。`isa.c` は hcr.h・packets.h を外し `ghci-regs.h` の caps で選ぶ（`ghci_isa_select`・`ghci_isa_native`・`ghci_isa_options`（registers = min(63, RESIDENT_WARPS[23:16])、sample mask は CAP_MSAA））、宣言は新しい `compiler/ghci-isa.h`。kernel の build に compiler・isa.c と共有の `spirv.c`・`optimize.c` を戻した（amd64 は他の driver と重ならないよう filter-out）。
  - **試験**: plan/ws288/tests の gsi1 の試験を plan/ws290/tests に写して新しい ABI に直した（期待値は ghci.md と design §11.4 から手で書く。harness は compiler の header の値を使わない）。tools は amaterrace-gpu cf8be08 の `tools/gsi1*.py` 5 file を `gsi1-tools/` にそのまま写した（U2）。tools は d9252e4 より古い（lda の PT word 0〜3 を 0 で読む、operand 120・121 を 0 で読み qstat・sastat と書く、sampler slot の field を知らない、`ld.64` が無い、model の export は attachment 0〜3 だけ）ので、`gsi1-model-run.py` が model の本文を 2 か所だけ置き換えて走らせ（disk の写しは変えない）、sampler slot は試験が読んでから消して round trip する。新しい場面: 資源の表と slot（resources.frag、shared.vert+shared.frag の pipeline、msarray.frag、image.comp、texelbuffer.comp、subpass.frag）、bank（push.comp の c0〜c23 と lwc 32〜39、uniform.comp:dynamic の c40・c41）、set 3 の scratch（spill.*）、属性（attrib.vert、amap、attrib60.vert の 60 で断る・amap で 60 ちょうど）、operand 120・121 と used_regs < 62（basic.vert）、lda の word 3 と頂点（basic.frag、:line、:point）、MRT 8（mrt8.frag）、dual-source（dual.frag）、17 個の texture で断る（tex17.frag）、code に header が無い（全部）。
  - **結果**: `sh plan/ws290/tests/ghci-host-test.sh`（末尾の前に compiler の節を足した）: words 141/0、ref 56/0、model 254/0、driver 587/0、old-names PASS（21 sources）、ghci-isa PASS、gsi1 50 passed 0 failed → PASS ghci290-host-test。build（各 timeout 1800）: `config-amd64-ghci.mk` BUILD=build/ws290-p006-amd64、`config-visionfive2-ghci.mk` BUILD=build/ws290-p006-vf2、`config/ci/config-amd64.mk` BUILD=build/ws290-p006-amd64-ci、`config/ci/config-visionfive2.mk` BUILD=build/ws290-p006-vf2-ci、全部 rc=0・warning 0。QEMU・実機は無し（この Phase は host と build だけ）。
  - **design と違えた所**（P3 の判断を頼む）: (1) 資源の descriptor は `ld.64` でなく `lw`（c28 + entry + field）で読む。§12.8 の `lw`・`sw`・`atom` は 32 bit の番地なので `ld.64` の上位の語は使えず、`ld.64` は偶数の register の組が要り、参照の tools も知らない。executor は shader の届く memory（資源・descriptor の表・scratch）を 4 GiB 未満の VA に置く（p007〜p009 の要件）。(2) uniform texel buffer は texture slot（256 texel 幅の linear 2D、gsi1-abi.h）と、要素の数のための set 2 の buffer descriptor（stride = texel の bytes）の両方を取る（format の変換を TU に任せるため）。(3) set 3 の配置（stage ごとの scratch の番地）と、spill する compile は少なくとも 128 byte/warp を出す。(4) sample shading の flag は出さない（IR に sample ごとの入力が無い）。(5) fragment stage の無い pipeline の 2 命令の FS（R08）は executor（p008）で作る想定で、compiler には足していない。(6) ghci-host-test.sh の節は「末尾」の status の判定の直前に置いた（判定の後では失敗の時に走らない）。
  - **仕様と参照の食い違い（記録、止めてはいない）**: amaterrace-gpu d9252e4 `include/ghci_cmd.h` の `GHCI_VS_REG_ATTR(loc, comp)` は 4 loc + comp で、ghci.md §12.10.3 の累積の詰め方と違う（ghci.md に従った）。ghci.md §8.4・§12.10.2 は dynamic offset を c0〜c23 の push constant で渡すと書くが、design §11.4 の c40〜 に従った（driver の取り決めで、HW は関与しない）。
  - **Q1 が消してよい重複**（P3 の確認の後）: `plan/ws288/tests/gsi1-host-test.c`・`gsi1-host-test.py`・`gsi1-host-test.sh`・`gsi1-reference.c`・`gsi1-reference.h`・`plan/ws288/tests/gsi1/`（34 file）。
  - **未実施**: p007 以降の executor から compiler を呼ぶ所（呼ぶ側はまだ無い）。実機・QEMU。規約の全文の見直しは p015。

- 2026-10-28 P3（q1638-i01）: design §18 から立てた。
