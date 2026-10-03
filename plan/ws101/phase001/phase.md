<!-- awesome-plan project=zedbsd record=ws101p001 -->

# ws101-p001: 設計（compiler・実行器・GLES 3.1 の compute・Noct・試験・Phase の分け方）

Phase ID: `ws101-p001`
Parent: [WS101](../ws.md)
Status: cleared（2026-09-30。[design.md](../design.md)（第 2 版）を書き、Phase を p002〜p013 に分けた。見直しと敵対的レビューの指摘を反映。source は変えていない）
Phase disposition: normal
Queue: main の割り当て（2026-09-30、サブエージェント `wt/ws101`）

## 範囲

設計の Phase。source は変えない。成果は [design.md](../design.md) と ws.md の Phase の表。

1. compiler: GLCompute、LocalSize・LocalSizeId、built-in の ID、Workgroup の変数の SLM、`OpControlBarrier`、SSBO と SLM の atomic、
   Mesa の brw のどこに従うか、Gen12 の encoding。
2. 実行器: PIPELINE_SELECT の GPGPU、MEDIA_VFE_STATE、MEDIA_INTERFACE_DESCRIPTOR_LOAD、CURBE、GPGPU_WALKER、indirect、
   graphics と compute の切り替えの flush、eu-test の設定の使い方。
3. GLES 3.1 の compute（GLSL ES 3.10、API、EGL）、Noct の `accel_opengles.c` の使う API と足りない所、ws068-p035 との関係。
4. Noct の `NOCT_ENABLE_ACCEL` の build の変更の案（差分）、G3 の見本と N・倍率。
5. 試験（i915 の compute の試験の群、host、実機、Venus）。
6. Phase の分け方と依存。
7. 見直し。

## 読んだもの

- AGENTS.md の後半、`plan/coding-style.md`（節の構成）、`plan/guardrail.md`、`plan/master-design-policy.md`、`plan/ws101/ws.md`、
  `plan/ws068/ws.md`（p035・p037 の行）。
- i915: `compiler/`（spirv.c・compile.c・eu.h・ir.h・compiler.h）、`intel/eu-encoding-gen12.h`・`genxml.h`、`render/`（command.c・draw.c・
  draw.h・state.c・state.h・heap.h・gfx.h・instance.c・memory.c・objects.c・batch.c）、`tests/execution/eu-test.c`、`tests/render/README.md`、
  `memory.c`・`memory.h`・`device.c`・`ppgtt.c`、`drivers/gpu/gpu.c`（mmap）。
- libvulkan（pipeline.c・commands-generated.inc）、libglesv2（gles.h・gles.c・buffer.c・program.c・glsl/）、libegl（egl.c）、`include/libc/GLES3`・`EGL`。
- Noct（`build/NoctLang` の展開済みの snapshot、読み取りのみ）: `src/accel/accel_opengles.c`・`accel_shader_source.c`・`accel_vulkan.c`、
  `CMakeLists.txt`、`CMakePresets.json`、`cmake/toolchains/zedbsd-amd64.cmake`、`docs/accel.md`、`tests/testcases/accel/`。
  `userland/base/noct/`（Makefile・version.mk・zedbsd.cmake・patches）。
- Mesa 25.0.7（`/home/awe/p014-c/mesa`）: `brw_fs_thread_payload.cpp`（cs_thread_payload）、`brw_nir_lower_cs_intrinsics.c`、`brw_fs_nir.cpp`
  （workgroup ID、barrier、fence、load_num_workgroups）、`brw_compile_cs.cpp`、`brw_fs.cpp`（subgroup ID の param、push の大きさ）、
  `brw_eu_emit.c`（brw_barrier、brw_memory_fence）、`brw_generator.cpp`（generate_barrier: Gen12 は sync.bar）、`brw_fs_visitor.cpp`
  （emit_cs_terminate）、`brw_lower_logical_sends.cpp`（lower_hdc_memory_logical_send）、`brw_eu.h`（descriptor の helper）、
  `brw_eu_defines.h`（SFID、AOP、BTI、sync）、`brw_eu_inst.h`（gateway_subfuncid）、`vulkan/genX_cmd_compute.c`・`genX_pipeline.c`・
  `genX_cmd_buffer.c`（flush_pipeline_select）、`genxml/gen120.xml`・`gen110.xml`・`gen80.xml`・`gen60.xml`、`common/intel_compute_slm.c`・
  `intel_l3_config.c`、`dev/intel_device_info.c`。

## 結果

- [design.md](../design.md) を書いた（§0 今の状態、§1 compiler、§2 実行器、§3 GLES、§4 Noct、§5 試験、§6 Phase、§7 判断、§8 危険）。
- ws.md の Phase の表を p002〜p013 に分けた（第 1 版は p002〜p011。レビュー M13 で実行器・GLES の API を分け、素の機械の Phase を足した）。
- 分かった重要な事実:
  - Noct の OpenGL ES の backend は CMake の `CMAKE_SYSTEM_NAME` が Linux・FreeBSD のときだけ選ばれ、zedBSD の toolchain file は
    `zedBSD` を名乗るので、`NOCT_ENABLE_ACCEL=ON` だけでは backend が入らない（patch が要る）。`<GLES3/gl31.h>` も tree に無い。
  - accel を有効にした `/bin/noct` は libEGL・libGLESv2（と libvulkan・libwayland）を DT_NEEDED に持つので、desktop の library の無い
    image では起動できなくなる。構成で条件付きにするか別の binary にする（D2）。
  - Noct の生成する GLSL は shared・barrier・loop を使わない（DOALL と DOSUM だけ）。`__gpu`（CUDA 風）は `src/unorganized/` にあり build されない。
    よって G3 に要るのは compiler の核（p002）・実行器（p003〜p005）・GLES（p008〜p010）と SSBO の atomic（DOSUM と結果の word）で、
    SLM・barrier・indirect（p006・p007）は G1 のため。
  - Noct の GLES の backend は float の program を GPU に出さない（CPU に戻る）。group の数が device の上限（65535）を越えると error。
    shader の compile・link の失敗は CPU への fallback ではなく error（敵対的レビュー H1・H2・M8 で分かった）。
  - i915 の compiler の `LOAD_STORAGE` は predicate を持たない。Noct の `if (lane >= trip) return;` の後の load は範囲外の channel でも走る。
    PPGTT の scratch page があるので fault にはならないが、predicate を足す。store と atomic は predicate が必須。
  - 実行器は `vkCmdBindPipeline`・`vkCmdBindDescriptorSets` の bind point を読み捨てている。compute には分離が要る。
  - libegl は surfaceless・pbuffer・ES3 を既に持つ。GL の `GL_VERSION` は 3.0。

## 見直し（誤り・欠落・矛盾の観点。自分で）

書いた後に読み直し、次を直した。

| # | 種類 | 見つけたこと | 直したこと |
| --- | --- | --- | --- |
| 1 | 誤り | Noct の Makefile の案で `NOCT_ZEDBSD_ACCEL` を `:=` の中で定義の前に使い、`ZEDBSD_USER_PROGRAMS` も package を読む順では確定していない | 遅い展開（`=`）と recipe の中の展開に。CMake の cache が前の ON を残すので毎回 ON・OFF を明示。stamp の名に accel の有無（§4.1） |
| 2 | 欠落 | indirect の NumWorkgroups を `MI_COPY_MEM_MEM` で CURBE の memory に写す方式の順序の保証を確かめていない | 未確認と明記し、違えば anv の方式に切り替える手を置いた。第 2 版でレビュー M14 を受け、最初から address を push し shader が読む形に変えた（§2.5） |
| 3 | 欠落 | uncached の stateless の MOCS で A64 の atomic が正しく動くか | 未確認と明記し、p005 の ATOMIC-SSBO で確かめ、違えば compute の op だけ write-back の MOCS（§2.3・§8） |
| 4 | 欠落 | Noct は pbuffer の context で call ごとに `glBufferData` をし `eglSwapBuffers` をしない。GL の garbage（古い device の写し）の回収が frame の終わりなら積もる | p009 で読み戻しの待ちの時点の回収を確かめる（§3.3・§8。第 2 版でレビュー M1 を受けて submit の点を実装に合わせた） |
| 5 | 欠落 | compute の kernel の大きさ（instruction window の vertex の 16 KiB に置くと大きな kernel が溢れる） | window の全体（48 KiB）を使い、越えたら作成の失敗（§2.1） |
| 6 | 矛盾 | IDD の SLM・barrier の field を p005 と p003 のどちらが入れるかが曖昧 | 第 2 版では p004 が IDD の全 field を binary の値から入れ、p006 は compiler と試験（§6） |
| 7 | 欠落 | 3.1 を名乗る device の条件 | queue の COMPUTE_BIT と limits が ES 3.1 の最小以上（§3.3） |
| 8 | 欠落 | surfaceless が失敗したとき Noct は `EGL_DEFAULT_DISPLAY`（zedBSD では画面を直接使う display の platform）を試す | p009 で pbuffer だけでは画面を取らないことを確かめる（§3.4・§8） |
| 9 | 欠落 | `OpArrayLength` の range を compute の CURBE だけに置くと、vertex・fragment の SSBO の `.length()` が読めない | draw の push data の storage の register にも range（§2.3、第 2 版では p004） |
| 10 | 誤り | MIXED の step の「draw の結果を SSBO として読む」は image を buffer として読めない | draw → `vkCmdCopyImageToBuffer` → dispatch → draw の形に（§5.1） |
| 11 | 確認 | 引いた値（sync の function、gateway の subfunction の位置、SFID）を Mesa で確かめた | `tgl_sync_function`（NOP 0・ALLRD 2・ALLWR 3・BAR 0xe）、`gateway_subfuncid` = descriptor の bit 2:0、SFID 3・7・10・12 を design に明記 |
| 12 | 確認 | SSBO の binding 56〜63 が既存の binding（0 既定の uniform、1〜16 sampler、32〜55 uniform block、48 capture）とぶつからないか | ぶつからない。i915 の実行器の binding の上限 64 の中 |
| 14 | 確認 | 計算した message の descriptor が正しいか | scratchpad の host の program（既存の `eu.c` を include）で SLM の read・write・atomic、A64 の atomic、fence、gateway の barrier の send を作り、Mesa 25.0.7 の `brw_disasm --gen=adl` が意図どおりに読み、`brw_asm` で組み直して 256 bytes が同じ。eu-test の参照の kernel の EOT（thread spawner、0x02000000）も同じ disassembler で読んだ。design §1.6 の表に値を書いた。**ただし A64 の atomic の応答なしの値は誤っていた**（応答の bit が立ったまま。disassembler は応答の bit を表示しない）。敵対的レビュー H4 で見つかり、第 2 版で直して照合し直した |
| 13 | 確認 | PPGTT の範囲外の読みが fault になるか | `ppgtt.c` の scratch tower（Linux の gen8_init_scratch と同じ）があり、読みは scratch page になる。load の predicate は安全のためで、必須は store と atomic |

## 敵対的レビュー（design-reviewer、2026-09-30）

design-reviewer（サブエージェント、読み取りだけ）に第 1 版を渡した。指摘のうち高の 4 件と M1・M2・M7 は自分で source を読んで確かめた
（`accel_opengles.c` の group の数の検査と `accel_opengles_program_uses_f32`、Mesa の `init_max_scratch_ids`、応答なしの descriptor を encoder と
`brw_disasm`・`brw_asm` で作り直した照合、`gles.c` の `glFlush`・`glFinish`、`instance.c` の `maxPerStageDescriptorStorageBuffers`、
`plan/ws031/mesa-refs` の symlink と `dump-gen-packets.py` の属性）。全ての指摘を第 2 版に入れた。

| # | 重大度 | 指摘 | 直したこと（design の節） |
| --- | --- | --- | --- |
| H1 | 高 | N = 2^22 は Noct が dispatch できない（⌈N/64⌉ = 65536 > 65535） | N = 4,000,000（≤ 65535 × 64）。§4.2・§5.1・D3 |
| H2 | 高 | Noct の GLES の backend は float の program を GPU に出さない。saxpy の見本は意味が無い | 見本は整数だけ。float は判断 D5。§3.4・§4.2・§7 |
| H3 | 高 | compute の scratch の thread ID は 112 × DSS ではなく 128 × 6（Mesa の Gen12） | 768 thread 分。SPILL を全 thread が埋まる数の group で。§1.1・§2.3・§5.1 |
| H4 | 高 | A64 の atomic の応答なしの descriptor（0x0404a7fd）は応答の bit が立ったまま rlen 0。disassembler は応答の bit を表示しないので照合で見逃した | 0x040487fd（と SLM の 0x020097fe）を encoder と brw_disasm・brw_asm で照合し直した。試験は descriptor の数値で bit 5 と rlen の一致を見る。§1.6・§5.2 |
| M1 | 中 | `glFlush`・`glFinish` は何もしない。submit・回収は swap か読み戻し。`used` の設定、eglMakeCurrent の release をまたぐ記録 | §3.3 を実装に合わせて書き直した |
| M2 | 中 | SSBO の上限 8 は device（4）と compiler の block の上限（8）に合わない | 4（ES 3.1 の最小）。§3.3 |
| M3 | 中 | device の写しに STORAGE_BUFFER の usage が無い、buffer の target | §3.3（p009） |
| M4 | 中 | compute だけの program の link・種別・layout・stageFlags・render pass・libGL への漏れ | §3.1・§3.3（p009） |
| M5 | 中 | Noct の build の案: stamp と依存は rule を読む時点で展開、patch の一覧、`--fuzz=0` と行末の空白、link の hunk は不要、LINK_DEPENDS、host の Noct の作り直し | 構成の変数 `ZEDBSD_NOCT_ACCEL`、2 hunk、実物から diff、LINK_DEPENDS、host の作り直しを D2 に。実装は main。§4.1・§7 |
| M6 | 中 | 動的な index は 1 つまで（2 次元の shared の tile が断られる） | 任意の数を byte offset に足す。§1.4（p002） |
| M7 | 中 | `dump-gen-packets.py` は使えない（symlink と古い属性）。genxml の import の連鎖の記述の抜け | WS101 の dumper（start・end、gen120 → … → gen60）。冒頭と §5.2 |
| M8 | 中 | Noct の compile・link の失敗は error。DOSUM は SSBO の atomic が要る | SSBO の A64 の atomic を p002・p005 に（G3 の経路）。§3.4・§6 |
| M9 | 中 | return の後の loop の中の barrier が thread ごとにずれて固まる | parser が断り、GLSL の front-end も規則を検査。§1.2・§3.2 |
| M10 | 中 | 試験の欠落: 8 の倍数でない group、多数の dispatch、Noct の shader の形、拒否 | ODD・ODD-BARRIER・MANYOPS・REFUSE、Noct の fixture。§5.1・§5.2 |
| M11 | 中 | 型名、最適化の水準、JIT の warm-up、CPU の見込みの根拠、offload の確認 | §4.2（p011 で CPU を先に測る、offload の確認の方法） |
| M12 | 中 | 判断項目の漏れ | D5 を足し、D1・D2・D3 に影響を書いた。GPU が固まったときの回復は §8・p012 |
| M13 | 中 | p003・p008 は大きすぎる。p009 の toolchain の担当、G1 の素の機械 | Phase を p002〜p013 に組み直した。§6 |
| M14 | 中 | indirect の CURBE への写しは順序が未確認 | NumWorkgroups は常に address を push し shader が読む形に。§1.1・§1.5・§2.5 |
| L1 | 低 | CURBE の大きさの計算の誤り（3 KiB）と 0x3000 の重なり | 0x2000〜0x2fff に限る。§2.2 |
| L2 | 低 | `!= I915_STAGE_VERTEX` の分岐 | p002 で全数を見直す。§1.2 |
| L3 | 低 | 割り算の理由の誤り（UDIV・UMOD はある） | 理由を math box の重さに。§1.1 |
| L4 | 低 | SLM の切り上げと 16 KiB の拒否 | §1.2・§2.3 |
| L5 | 低 | Generic Media State Clear を省く理由、Wa_1409600907 | §2.4 |
| L6 | 低 | CMPXCHG の値の順 | parser で入れ替え。§1.4 |
| L7 | 低 | SSBO を書く draw が transfer_pending を立てない（既存の不足） | p004 で立てる。§2.2 |
| L8 | 低 | eglplatform.h の根拠、API-PROVENANCE.md | §3.3・§4.1 |
| L9 | 低 | call ごとの 16 MB の連続の確保 | §3.3・§8（p011 の繰り返しの run） |

レビューの「未確認」のうち残るもの: uncached の MOCS での A64 の atomic（p005）、0 の group の indirect（p007）、非特権の LRM（p007）、
CPU の JIT の時間と倍率（p011）、pbuffer の release をまたぐ記録（p009）、Noct の `>>` の符号と wrap（p011）。

## 未実施の確認

- 設計の Phase なので build・host の試験・QEMU・実機は走らせていない。
- 値は Mesa 25.0.7 の source と genxml で読んだ。message の descriptor 8 種は scratchpad の host の program と Mesa の disassembler・assembler で
  照合した（見直し #14。repo に試験としては置いていない。p002・p005 で試験にする）。packet（VFE・IDD・CURBE・walker）の照合と実機での確認は p003 以降。
- PRM そのものは読んでいない（Mesa と anv の引用による）。§2.5 の順序と §2.3 の atomic の MOCS は未確認として記録した。

## 残課題とユーザー・main の判断

- D1: 「OpenGL ES 3.1」の名乗り（部分の実装）。既定の案 A。p009 の前に要る。
- D2: Noct の build の変更（toolchain の変更、main とユーザーの許可、実装は main）。既定の案 A（構成で `ZEDBSD_NOCT_ACCEL := y` を置いた amd64 だけ）。
  共有の host の Noct の作り直しの扱いも含む。p011 の前に要る。
- D3: G3 の N（4,000,000）と倍率（3 倍以上、伸び 10 倍。CPU の時間を測ってから決め直す）の確認。
- D5: デモの見本は整数だけでよいか（Noct の GLES の backend は float を GPU に出さない）。
- D4（main）: ws068 の ws.md の p035 の行に、GLES 3.1 の compute の部分集合を WS101 へ移したと記録する。p008 の前。

## 再開の条件

p002（compiler の核）と p008（GLSL ES 3.10 の compute。D4 の記録の後）は依存が満たされており、並行で Queue に入れられる。
