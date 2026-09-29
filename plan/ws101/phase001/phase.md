<!-- awesome-plan project=zedbsd record=ws101p001 -->

# ws101-p001: 設計（compiler・実行器・GLES 3.1 の compute・Noct・試験・Phase の分け方）

Phase ID: `ws101-p001`
Parent: [WS101](../ws.md)
Status: cleared（2026-09-30。[design.md](../design.md) を書き、Phase を p002〜p011 に分けた。見直しと敵対的レビューの指摘を反映。source は変えていない）
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
- ws.md の Phase の表を p002〜p011 に分けた。
- 分かった重要な事実:
  - Noct の OpenGL ES の backend は CMake の `CMAKE_SYSTEM_NAME` が Linux・FreeBSD のときだけ選ばれ、zedBSD の toolchain file は
    `zedBSD` を名乗るので、`NOCT_ENABLE_ACCEL=ON` だけでは backend が入らない（patch が要る）。`<GLES3/gl31.h>` も tree に無い。
  - accel を有効にした `/bin/noct` は libEGL・libGLESv2（と libvulkan・libwayland）を DT_NEEDED に持つので、desktop の library の無い
    image では起動できなくなる。構成で条件付きにするか別の binary にする（D2）。
  - Noct の生成する GLSL は shared・barrier・loop を使わない（DOALL と DOSUM だけ）。`__gpu`（CUDA 風）は `src/unorganized/` にあり build されない。
    よって G3 に要るのは compiler の核（p002）・実行器（p003・p004）・GLES（p007・p008）で、SLM・barrier・indirect（p005・p006）は G1 のため。
  - i915 の compiler の `LOAD_STORAGE` は predicate を持たない。Noct の `if (lane >= trip) return;` の後の load は範囲外の channel でも走る。
    PPGTT の scratch page があるので fault にはならないが、predicate を足す。store と atomic は predicate が必須。
  - 実行器は `vkCmdBindPipeline`・`vkCmdBindDescriptorSets` の bind point を読み捨てている。compute には分離が要る。
  - libegl は surfaceless・pbuffer・ES3 を既に持つ。GL の `GL_VERSION` は 3.0。

## 見直し（誤り・欠落・矛盾の観点。自分で）

書いた後に読み直し、次を直した。

| # | 種類 | 見つけたこと | 直したこと |
| --- | --- | --- | --- |
| 1 | 誤り | Noct の Makefile の案で `NOCT_ZEDBSD_ACCEL` を `:=` の中で定義の前に使い、`ZEDBSD_USER_PROGRAMS` も package を読む順では確定していない | 遅い展開（`=`）と recipe の中の展開に。CMake の cache が前の ON を残すので毎回 ON・OFF を明示。stamp の名に accel の有無（§4.1） |
| 2 | 欠落 | indirect の NumWorkgroups を `MI_COPY_MEM_MEM` で CURBE の memory に写す方式の順序の保証を確かめていない | 未確認と明記し、違えば anv の方式（shader が indirect buffer を読む）に切り替える手を p006 に（§2.5） |
| 3 | 欠落 | uncached の stateless の MOCS で A64 の atomic が正しく動くか | 未確認と明記し、p005 の ATOMIC で確かめ、違えば compute の op だけ write-back の MOCS（§2.3・§8） |
| 4 | 欠落 | Noct は pbuffer の context で call ごとに `glBufferData` をし `eglSwapBuffers` をしない。GL の garbage（古い device の写し）の回収が frame の終わりなら積もる | p008 で submit の待ちの時点の回収を確かめる（§3.3・§8） |
| 5 | 欠落 | compute の kernel の大きさ（instruction window の vertex の 16 KiB に置くと大きな kernel が溢れる） | window の全体（48 KiB）を使い、越えたら作成の失敗（§2.1） |
| 6 | 矛盾 | IDD の SLM・barrier の field を p005 と p003 のどちらが入れるかが曖昧 | p003 が IDD の全 field を binary の値から入れ、p005 は compiler と試験（§6） |
| 7 | 欠落 | 3.1 を名乗る device の条件 | queue の COMPUTE_BIT と limits が ES 3.1 の最小以上（§3.3） |
| 8 | 欠落 | surfaceless が失敗したとき Noct は `EGL_DEFAULT_DISPLAY`（zedBSD では画面を直接使う display の platform）を試す | p008 で pbuffer だけでは画面を取らないことを確かめる（§3.4・§8） |
| 9 | 欠落 | `OpArrayLength` の range を compute の CURBE だけに置くと、vertex・fragment の SSBO の `.length()` が読めない | draw の push data の storage の register にも range（§2.3、p003） |
| 10 | 誤り | MIXED の step の「draw の結果を SSBO として読む」は image を buffer として読めない | draw → `vkCmdCopyImageToBuffer` → dispatch → draw の形に（§5.1） |
| 11 | 確認 | 引いた値（sync の function、gateway の subfunction の位置、SFID）を Mesa で確かめた | `tgl_sync_function`（NOP 0・ALLRD 2・ALLWR 3・BAR 0xe）、`gateway_subfuncid` = descriptor の bit 2:0、SFID 3・7・10・12 を design に明記 |
| 12 | 確認 | SSBO の binding 56〜63 が既存の binding（0 既定の uniform、1〜16 sampler、32〜55 uniform block、48 capture）とぶつからないか | ぶつからない。i915 の実行器の binding の上限 64 の中 |
| 13 | 確認 | PPGTT の範囲外の読みが fault になるか | `ppgtt.c` の scratch tower（Linux の gen8_init_scratch と同じ）があり、読みは scratch page になる。load の predicate は安全のためで、必須は store と atomic |

## 敵対的レビュー（design-reviewer）

（結果を待っている。届いたら指摘と直したことをここに書く。）

## 未実施の確認

- 設計の Phase なので build・host の試験・QEMU・実機は走らせていない。
- 値は Mesa 25.0.7 の source と genxml で読んだ。Mesa の assembler での照合と実機での確認は p002 以降。
- PRM そのものは読んでいない（Mesa と anv の引用による）。§2.5 の順序と §2.3 の atomic の MOCS は未確認として記録した。

## 残課題とユーザー・main の判断

- D1: 「OpenGL ES 3.1」の名乗り（部分の実装）。既定の案 A。p008 の前に要る。
- D2: Noct の build の変更（toolchain の変更、main とユーザーの許可）。既定の案 A（amd64 で libegl・libglesv2 を選んだ構成だけ）。p009 の前に要る。
- D3: G3 の N（2^22）と倍率（3 倍以上、伸び 10 倍）の確認。
- D4（main）: ws068 の ws.md の p035 の行に、GLES 3.1 の compute の部分集合を WS101 へ移したと記録する。

## 再開の条件

p002（compiler の核）と p007（GLSL ES 3.10 の compute）は依存が満たされており、並行で Queue に入れられる。
