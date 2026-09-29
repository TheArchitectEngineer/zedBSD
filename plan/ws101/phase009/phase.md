<!-- awesome-plan project=zedbsd record=ws101p009 -->

# ws101-p009: libglesv2 の OpenGL ES 3.1 の compute の API と Venus での G2

Phase ID: `ws101-p009`
Parent: [WS101](../ws.md)
Status: cleared（2026-09-30。Venus（QEMU、host は lavapipe）で glescompute の全 step PASS。WS068 の egl-p030 の GL_VERSION 3.0 の検査が期待どおり FAIL になる（下の「影響の調べ」、main への依頼））
Phase disposition: normal
Queue: main の割り当て（2026-09-30、サブエージェント `wt/ws101`。D1 は未決のため main の指示で既定の案: ES 3.1 を名乗り、未実装の 3.1 の関数は error の stub）

## 範囲（[design.md](../design.md) §3.3・§3.4・§5.4）

libglesv2 に OpenGL ES 3.1 の compute の部分集合（compute shader の program、`glDispatchCompute`・`glDispatchComputeIndirect`、
`GL_SHADER_STORAGE_BUFFER` と binding point、`GL_DISPATCH_INDIRECT_BUFFER`、`glMemoryBarrier`・`glMemoryBarrierByRegion`、compute の上限の
query、`glGetProgramiv(GL_COMPUTE_WORK_GROUP_SIZE)`）を足し、版の名乗り（D1 の既定の案）、`gl31.h`、export と stub、libGL への漏れの分離、
GLES の compute の試験の program を作り、Venus で G2 を確かめる。i915 での G2 は p010。

## 変えた file

| file | 内容 |
| --- | --- |
| `include/libc/GLES3/gl31.h`（新） | Khronos OpenGL-Registry の commit 1cdd228e（`gl3.h` と同じ registry）、sha256 bb17bfde…daf3、MIT。`include/libc/EGL/API-PROVENANCE.md` に行を足した |
| `libglesv2/gles.h` | `gl31.h` の include、`GLES_STORAGE_BINDINGS`（4）・`GLES_STORAGE_FIRST_BINDING`（56）、`struct gles_storage`、program の `compute`（shader）・`compute_pipeline`・`local_size`・`storages`、state の `compute`（context が compute を持つか）・`storage_buffer`・`dispatch_buffer`・`storage_ranges`、`gles_draw_blocks`・`gles_draw_descriptors` の宣言 |
| `libglesv2/gles.c` | `gles_compute_offered`（ES 3 の context（libGL でない）で、device が `vertexPipelineStoresAndAtomics`、display の queue family に COMPUTE_BIT、limits が ES 3.1 の最小以上: invocation 128、size 128・128・64、count 65535、shared 16384、stage と set の storage buffer 4、storage buffer の範囲 2^27）を state の作成で決める。`glGetString` は compute の context で「OpenGL ES 3.1 Kei」「OpenGL ES GLSL ES 3.10」、`GL_MINOR_VERSION` 1。compute の integer の状態（`gles_integers_compute`: binding、上限。image と atomic counter の上限は 0）、`glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE)` は device の `maxStorageBufferRange` |
| `libglesv2/buffer.c` | device の写しの usage に STORAGE_BUFFER・INDIRECT_BUFFER、target の `GL_SHADER_STORAGE_BUFFER`・`GL_DISPATCH_INDIRECT_BUFFER`（compute の context だけ）、`glBindBufferBase/Range(GL_SHADER_STORAGE_BUFFER)`（4 つ、offset は `minStorageBufferOffsetAlignment`）、`glGetIntegeri_v` の `GL_SHADER_STORAGE_BUFFER_BINDING/START/SIZE` と `GL_MAX_COMPUTE_WORK_GROUP_COUNT/SIZE`、`glDeleteBuffers` での解除 |
| `libglesv2/program.c` | `glCreateShader(GL_COMPUTE_SHADER)`（compute の context だけ）、compile の stage、GLSL ES 3.10 は compute の context だけ、attach・detach・解放（`program_let_go` にまとめた）、`program_link_compute`（compute だけで link、他の stage と混ぜたら失敗、SPIR-V の binary は断る。`glsl_link_compute` → storage の block（binding < 4、device の stage あたりの数以下）→ `gles_spirv_reflect`（GLCompute であること）→ uniform と named block → layout（stage は COMPUTE、storage は binding 56 + n）→ `vkCreateComputePipelines`（module は pipeline の後に破棄））、unlink で pipeline を garbage へ、`glGetProgramiv(GL_COMPUTE_WORK_GROUP_SIZE)`（libGL では INVALID_ENUM） |
| `libglesv2/spirv.c` | compute の shader の storage block（binding 56〜59）と Workgroup の変数を uniform の interface から外す |
| `libglesv2/draw.c` | `draw_blocks`・`draw_descriptors` を `gles_draw_blocks`・`gles_draw_descriptors` として compute.c と共有（storage の range の引数、descriptor pool の storage buffer の数を 1 + 4 倍に）、compute の program が current の draw は INVALID_OPERATION |
| `libglesv2/compute.c`（新、libGLESv2 だけ） | `glDispatchCompute`・`glDispatchComputeIndirect`（draw surface の frame に pass の外で記録。storage の range の検査と同期、used = frame、書ける block の buffer に `gpu_written`（読むときに `gles_buffer_fetch` が frame を待って読み戻す）、indirect の offset・範囲・map の検査と CPU の値での 0・上限の skip）、dispatch の前後の barrier（前: 全ての書き → compute の読み書きと indirect、後: compute の書き → 全ての使い方と host）、`glMemoryBarrier`・`glMemoryBarrierByRegion`（bit の検査だけ。順序は dispatch の barrier が持つ） |
| `libglesv2/es31.c`（新、libGLESv2 だけ） | compute 以外の ES 3.1 の 60 の関数（indirect の draw、framebuffer parameter、program interface query、separate shader object、`glProgramUniform*`、`glBindImageTexture`、`glGetTexLevelParameter*`、vertex attrib binding）は `GL_INVALID_OPERATION` を記録する stub（値を返すものは「無い」を返す）。`glGetBooleani_v`・`glSampleMaski`・`glGetMultisamplefv`・`glTexStorage2DMultisample` は既存の実装を export |
| `libglesv2/Makefile`・`exports.map` | `compute.c`・`es31.c`（libGL の Makefile には足さない: libGL は desktop GL の compute を持たない）、67 の entry point の export（251 → 318）。libGL の `exports.map` は変えていない |
| `userland/desktop/glescompute/`（新） | GLES の compute の試験の program（既定では image に入らない、`USERLAND_glescompute_DEFAULT` = n）。Noct と同じ順（surfaceless → default display）の pbuffer の ES 3 の context で: 版、上限、add（uniform・早期の return・workgroup size の query）、noct（Noct の shader の形: int の除算・剰余・shift・`?:`・atomicAdd の和）、shared（shared と barrier の workgroup の和）、indirect（offset 4 の grid）、chain（読み戻しなしの 2 つの dispatch）、release（dispatch の記録を `eglMakeCurrent(NO_CONTEXT)` と再取得をまたぐ）、repeat（毎回 `glBufferData` の 256 KiB の buffer で dispatch と読み戻し）、errors（program 無し、group の数の超過、compute の program の draw、compute と vertex の link、binding 4、不正な barrier の bit、stub の `glDrawArraysIndirect`） |
| `platform/amd64/vmunix.mk` | `/bin/glescompute` の link の規則（egltest と同じ形、libEGL・libGLESv2・libc）。既定の構成には入らないので既定の動作は変わらない |

## 試験（`plan/ws101/tests/gles/`、新）

| file | 内容 |
| --- | --- |
| `reflect-host.c`・`run.sh` | host: `plan/ws101/tests/glsl/pass/*.comp` を GLSL の compiler で link し、libglesv2 の `gles_spirv_reflect` で読む（GLCompute、storage と shared を uniform から外す、add.comp の uniform n・k）。ASan・UBSan |
| `config-amd64-compute.mk`・`build-image.sh` | Venus の guest の image（WS068 の lean な GLSL の image に glescompute を足す、`build/ws101-p009-img`） |
| `venus.sh` | guest を自分の runtime（`build/ws101-p009-run`）で起動し、compositor（`/bin/wayland`）を走らせたまま glescompute を auto（surfaceless）・default（default display の pbuffer）・repeat 1000 で実行し、各 run の後に compositor が生きていることを確かめる |

## 確認（QEMU。実機は未実施）

| コマンド | 結果 |
| --- | --- |
| libGLESv2 の build（target の compiler、`-Wall -Wextra -Werror`） | PASS（warning 0） |
| `plan/ws101/tests/gles/build-image.sh`（image の build。libGL も同じ source で build） | PASS（初回は glescompute の link の規則が無く失敗 → vmunix.mk に規則を足して PASS） |
| `plan/ws101/tests/gles/run.sh build/ws101-p009-gleshost` | PASS（10 shader、0 failed） |
| `plan/ws068/tests/spirv-host/run.sh`（spirv.c の回帰） | PASS |
| `plan/tools/style-check.py`（新しい C の file: compute.c・es31.c・glescompute/main.c・reflect-host.c。変えた file は HEAD との差分で新しい違反 0） | 違反 0（最初の run の後に直し、image を build し直して下の venus.sh を最終の code で再実行） |
| `plan/ws101/tests/gles/venus.sh build/ws101-p009-venus`・最終の code で `build/ws101-p009-venus2`（Venus、host の Vulkan は lavapipe） | **両方 PASS**。auto（surfaceless）・default・repeat（1000 回）とも `GLESCOMPUTE DONE failures=0`、全 step PASS。「OpenGL ES 3.1 Kei」「OpenGL ES GLSL ES 3.10」3.1、上限 count 65535×3・size 1024×3・invocation 1024・binding 4・block 4・alignment 16・shared 32768・block size 134217728。compositor は 4 回の確認で生きていた |
| WS068 の Venus の回帰（同じ image、`build/ws101-p009-egl/`）: egl-p020（GLSL ES 3.00）・egl-p024（ES 3.0 の buffer・VAO・UBO）・egl-p027（query・fence） | PASS |
| egl-p030（transform feedback と GL_VERSION 3.0） | **FAIL（予想どおり）**: 捕捉の 6 つの結果と捕捉した位置の正方形は緑、「GL_VERSION が OpenGL ES 3.0 で始まる」の正方形（(480,400)）だけ赤。`display.png`・`wayland.png` は `build/ws101-p009-egl/egl-p030/` |
| libGL の回帰（同じ image、`build/ws101-p009-glx/`）: glx-p031（desktop GL 3.1）・glx-p033（3.2） | PASS（libGL は libglesv2 の変えた source を組み込む） |

## 影響の調べ（D1 の既定の案で「OpenGL ES 3.1」を名乗ることの影響）

- 名乗る条件: libGLESv2 の ES 3 の context で、device が上の条件を満たすとき。**Venus（lavapipe）と i915 の実行器（instance.c: queue に
  COMPUTE_BIT、上限はちょうど ES 3.1 の最小、`maxDescriptorSetStorageBuffers` 24）の両方で満たす**ので、この変更が入ると 5330 でも
  ES 3 の context は 3.1 を名乗る（i915 での compute の動作の確認は p010）。ES 2 の context（`EGL_CONTEXT_CLIENT_VERSION` 2）は今までどおり
  （GL_VERSION は 3.0 の文字列のまま、compute は無し）。libGL（desktop GL）は版を fixed の層が返し、`gles_fixed` があると compute を持たない
  （target・shader の種類・query は INVALID_ENUM）ので漏れない。libGL の `exports.map` と Makefile は変えていない。
- in-tree で GL の版を見て分岐する program（`GL_VERSION`・`GL_MAJOR_VERSION`・`GL_MINOR_VERSION` を grep）:
  - `userland/desktop/egltest/feedback.c`（WS068）: `GL_VERSION` が「OpenGL ES 3.0」で始まることを検査の 1 つにしている → **egl-p030 が FAIL
    になる**（上の表）。GL の実装の誤りではなく、試験の期待が 3.0 に固定されているため。**main への依頼**: WS068 の egltest の検査を
    「OpenGL ES 3.0 か 3.1 で始まる」（または 3.0 以上）に変える（WS068 の source なので WS101 では変えていない）。
  - `userland/desktop/egltest/main.c`（表示だけ）、`userland/retro/zgears`・`userland/retro/glxtest`（libGL、desktop GL の版を見る。影響なし）。
  - `userland/base/noct/noct/src/accel/accel_opengles.c`（Noct）: `GL_MAJOR/MINOR_VERSION` が 3.1 以上なら GLES の backend を使う。意図した
    利用者。Noct の accel は guest の build でまだ無効（D2）なので、今は影響しない。D2 で有効にすると、5330 でも p010 の確認の前に GPU の経路を選ぶ。
  - `userland/packages/`（外部の package）: GL の版の参照は無い（grep で 0 件）。
- 3.1 の経路を選ぶ app が stub に当たる危険: in-tree には compute 以外の 3.1 の関数（indirect の draw、program interface query、separate
  shader object など）を使う program は無い（grep）。外部の ES 3.1 の app がそれらを使うと `GL_INVALID_OPERATION` と「無い」の値を受ける。
- ES 3.1 の最小に届かない報告値: `GL_MAX_COMPUTE_ATOMIC_COUNTERS`・`GL_MAX_COMPUTE_ATOMIC_COUNTER_BUFFERS`・`GL_MAX_COMPUTE_IMAGE_UNIFORMS` は
  0（ES 3.1 の最小は 8・1・4）。atomic counter と image load/store を持たないため（ws068-p035 の範囲）。ES 3.1 の他の上限（例:
  `GL_MAX_VERTEX_ATTRIB_BINDINGS`、`GL_MAX_IMAGE_UNITS`、`GL_MAX_FRAMEBUFFER_WIDTH`）は INVALID_ENUM のまま。

## 決めたこと・制限

- **dispatch には current の draw surface が要る**（pbuffer か window。記録は surface の frame の command buffer に入る）。surface の無い
  context（`EGL_KHR_surfaceless_context`）の dispatch は `GL_INVALID_OPERATION`。Noct は 1×1 の pbuffer を作るので満たす。
- **各 dispatch が前後の barrier を自分で記録する**ので、`glMemoryBarrier` は bit の検査だけ（design §3.3 の「glMemoryBarrier が
  vkCmdPipelineBarrier」から変えた。barrier を忘れた app でも正しく、Noct の dispatch ごとの barrier と同じ数）。i915 の実行器での barrier の
  費用は p010 で見る。
- **`glFinish`・`glFlush` は今までどおり何もしない**（design §3.3 の未決を「変えない」に決めた）。submit と待ちは読み戻し
  （`glMapBufferRange` などの `gles_buffer_fetch` → `query_finish`）か swap の frame の終わりで起きる。release と再取得をまたいだ記録は
  次の読み戻しで submit される（release の step で確かめた）。garbage は `query_finish` の `gles_collect` で回収される（repeat の 1000 回で
  確かめた。memory の量は測っていない）。
- storage の block の binding の上限は 4（`GLES_STORAGE_BINDINGS`。compiler は 8 まで受けるので、4〜7 は link の失敗）。
- 書ける block に bind された buffer は、shader が実際に書いたかによらず dispatch の後 `gpu_written`（次の CPU の読み書きで frame を待つ）。
- 未 bind の storage の binding point、range が block の固定部より短い、map 中の buffer は `GL_INVALID_OPERATION`（GL では未定義の動作）。
- indirect の grid は、CPU の bytes が新しいとき（device が書いていない）に読んで、0 か上限を越える値なら dispatch しない（GL では未定義）。
- compute の SPIR-V の binary（`glShaderBinary` の compute）は断る（link の log「the compute shader is not compiled GLSL」）。
- `GL_UNIFORM_BLOCK_REFERENCED_BY_COMPUTE_SHADER` などの program interface query は stub。

## 未実施

- i915（5330）での GLES の compute（p010）。実機での確認は全て未実施（この Phase の結果は QEMU の Venus だけ）。
- `glDispatchComputeIndirect` の grid を compute が書いた場合（`gpu_written` の dispatch buffer）の試験。
- boot test（userland の library と試験の program の変更。既定の image の中身は libGLESv2 だけが変わる）。
- repeat での device の memory の量の測定。

## 残り

- main への依頼: WS068 の egltest の `feedback.c` の版の検査を 3.1 も受けるように（egl-p030 の回帰を直す）。
- main への依頼: Tools 節への登録（`plan/ws101/tests/gles/run.sh`・`venus.sh`・`build-image.sh`）。
- D1 はユーザーの判断のまま（既定の案で実装した。変えるなら `gles_compute_offered` の条件と `glGetString` の分岐を戻す）。
