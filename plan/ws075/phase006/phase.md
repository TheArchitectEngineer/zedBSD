<!-- awesome-plan project=zedbsd record=ws075p006 -->

# ws075-p006: MRT・query・texel と storage の buffer・multisample（実行器と compiler）

Phase ID: `ws075-p006`
Parent: [WS075](../ws.md)
Status: in-progress（2026-09-28〜。増分 1〜3 済み（MRT・occlusion query・VS の storage buffer）、増分 4（stencil）は中断して wip.patch）
Phase disposition: normal
承認: 2026-09-27 ユーザー「…i915の高度化に進んでください。」、WS075 の計画（main の登録）。p005 の後（依存 p005 cleared）。

## 範囲

egltest の OpenGL ES 3 の場面 targets・blits・queries・feedback を実機の i915 で通す。そのために要る:

1. MRT（ws031-p031）: 複数の colour attachment への描画（compiler の location 0 以外の出力、render target の binding table と
   blend state の entry、clear）。
2. depth と stencil の形式: DEPTH_COMPONENT24（`X8_D24_UNORM_PACK32`）、DEPTH24_STENCIL8（`D24_UNORM_S8_UINT`）と stencil の試験。
3. occlusion query（sync の module）と fence sync。
4. texel buffer（buffer view、samplerBuffer の texelFetch・textureSize）と storage buffer（transform feedback の VS の store）。
5. multisample の image と resolve（sampler2DMS の texelFetch、blit の resolve）。

範囲外: geometry shader・gl_Layer・layered の描画（p007）、GLX の glxtest（p007 以降で capture の場面を足す）。

## 手順

最初に 4 場面を今の実行器で実機に走らせ、log の CHECK と executor の拒否（`i915: vk: ... refused`・`XXX unimplemented`）から
不足を並べ、場面ごとに直す。11 場面は 1 回の capture に収まらないので、4 場面は別の runner
`plan/ws031/tests/zdesktop/run-egltest6.sh`（`ZDESKTOP_APP=egltest6`、`vkloop-hw.sh` に足した）で走らせる。

基準の run（`build/ws075-p006/base-egltest6`）の不足: 4 つの colour attachment の render pass の拒否（MRT）、opcode 47
（vkCreateQueryPool）の未実装、vertex shader の SPIR-V の BufferBlock の拒否（transform feedback の capture の storage buffer）、
D24S8・D32S8（stencil）、multisample。failures: targets 16・blits 10・queries 18（glerror 0x505）・feedback 9。

## 設計

### 増分 1: MRT（2026-09-28）

- compiler（`compile.c`）: fragment の colour の location n（0〜3）を r(124 − 4n)..r(127 − 4n) に置き、書く location ごとに
  render-target write を昇順に（最後だけ Last Render Target Select と EOT）。binding table の entry は `I915_SHADER_RT_BTI(n)`
  （0、または 16 + n: texture の 1〜16 の後）、render target index は extended descriptor の bit 12〜14（Mesa の Gen11+ の
  `lower_fb_write_logical_send()`）。何も書かない shader は location 0 に 0 を書く。Mesa の brw_disasm が受ける。
- 実行器: render pass は colour attachment 4 つまで（`I915_GFX_MAX_ATTACHMENTS` 8、`i915_gfx_pass.color_attachments[]`、
  slot 0 は従来の `color_attachment`、`drv_i915_gfx_pass_color()`）。draw は slot ごとに render target の surface（view の無い slot は
  null surface、`i915_state_null_surface_write()`）、BLEND_STATE の entry を slot ごとに（書き込み mask は attachment ごと、
  blend の式は attachment 0 の、integer の target と blendEnable の無い attachment は blend しない）。colour の無い pass
  （depth だけ）は depth の view の大きさで描く（`drv_i915_gfx_draw_extent_view()`）。vkCmdClearAttachments は colour index の
  attachment を clear する。

### 増分 2: occlusion query（2026-09-28）

- `fence.c`（sync の module）: vkCreateQueryPool・vkDestroyQueryPool・vkGetQueryPoolResults（opcode 47〜49）。pool は GPU の object
  （query q の開始・終了の depth count が 16q・16q + 8、availability が 16 count + 8q）。vkCmdBeginQuery・vkCmdEndQuery は PIPE_CONTROL
  の post-sync の PS depth count（depth stall、anv の emit_ps_depth_count()）、終了の後に availability 1、vkCmdResetQueryPool は
  availability 0（64 query ずつの操作）。結果は submit が終わってから CPU で読む（clflush の後）。occlusion の pool だけ。
- 実行器の object の作成を公開（`drv_i915_gfx_object_create()`・`_destroy()`、draw.c）。host の fixture の stub を足した。

### 増分 3: vertex shader の storage buffer（transform feedback）（2026-09-28）

- compiler: BufferBlock の struct の Uniform の変数（と StorageBuffer の class）を storage buffer（`PTR_SSBO`、uniform の種類
  `I915_IR_UNIFORM_STORAGE`）、OpTypeRuntimeArray、NonWritable・NonReadable は無視。access chain の動的な index は byte offset の
  IR の整数に。load・store は `I915_IR_LOAD_STORAGE`・`I915_IR_STORE_STORAGE`（store は block の predicate を持つ）。
- codegen: A64 の untyped surface read・write（1 channel、SIMD8、stateless 253、SFID 12、Mesa の
  `brw_dp_a64_untyped_surface_rw_desc()`）。buffer の GPU address は push data の block（`i915_shader_block.address`）で渡り、
  channel ごとの address は low + offset と carry（CMP の −1 を ADD で引く）を payload の 2 register に interleave。predicate の
  ある store は f0.0 で mask。Mesa の brw_disasm が受ける。
- 実行器: storage buffer の descriptor を bind（`descriptor.c`）、push data に address を書く（`state.c`）。draw ごとの cache の
  invalidate に VF cache を足した（shader や copy の書いた vertex buffer）。`vertexPipelineStoresAndAtomics` を報告（libGLESv2 は
  これで GL_VERSION を 3.0 にする。atomic は未実装）。

## 検証

| 確認 | 結果 |
| --- | --- |
| host の vk の fixture spirv・lower・resdispatch・eu・compile・pipe | PASS（res・sync・cmdbuf は p005 から既存の失敗） |
| gentool（Mesa brw_disasm、`BRW_TOOLS=/home/awe/p014-c/mesa/build-asm/src/intel/compiler`） | PASS。MRT の fragment shader（location 0・1・3、discard あり）と storage buffer の vertex shader（load・store・predicate）も受ける（scratch で確認） |
| 実機 capture egltest6（`build/ws075-p006/`） | mrt1（MRT）: targets 16 → 7。q1（query）: queries 18 → **0**、targets 6。ssbo1: feedback 8 → 2。ssbo2（VF の invalidate と feature）: feedback **1**、targets 6、blits 10、queries 0。画面 `build/ws031-shots/ws075-p006-20260928-{q1,ssbo2}-sheet.png`。hang・fault なし |
| QEMU | 未実施（i915 の実機の変更） |

## 中断（2026-09-28、main の wrap up）: 増分 4 の stencil

- stencil の実装（D32_SFLOAT_S8_UINT・S8_UINT、Y tile の separate stencil plane、3DSTATE_STENCIL_BUFFER、3DSTATE_WM_DEPTH_STENCIL の
  stencil、vkCmdSetStencil*、pass の stencilLoadOp と ClearAttachments の aspect、stencil の clear は Y tile の R8_UNORM に value/255）は
  build できる（commit 62a10ad0）が、実機で後退した: st1（`build/ws075-p006/st1`）は wlkill の直後の frame 194 で compositor が
  止まり、st2 は targets が 6 → 14（glerror 0x505、eglSwapBuffers の失敗、MRT の四角も黒）。原因は未調査。**62a10ad0 は revert し、
  差分は `plan/ws075/phase006/wip.patch`（show の形）に置いた。**
- 再開の手順: wip.patch を当てる（または 62a10ad0 を cherry-pick）→ targets の GL_OUT_OF_MEMORY の元を探す（D32S8 の image の
  作成・view・copy の拒否、image->bytes に stencil plane を足したことの影響（subresource layout、`i915_image_surface_write()` の
  単一 slice の QPitch が bytes/pitch）、`image_supported()` の D32S8）→ st1 の compositor の停止が再現するか（stencil の変更と
  無関係の可能性: wlkill の直後、BUG-077 の系統）→ 実機の egltest6。

## 残り（2026-09-28 の時点）

- stencil: D24S8・D32S8 の形式、separate stencil buffer、3DSTATE_WM_DEPTH_STENCIL の stencil、動的な stencil の state（targets の
  stencil-complete・stencil-inside/outside・depth-stencil-test）。
- multisample の image と resolve（blits の 10 件）、sampler2DMS の texelFetch（survey）。
- texel buffer（samplerBuffer、survey）。
- feedback の drawn-from-captured: capture した buffer を vertex array にした四角が見えない（原因は未調査）。
- targets の draw-buffer-other: glDrawBuffers の後の glGetError が GL_INVALID_ENUM（期待は GL_INVALID_OPERATION）。libGLESv2 の
  glDrawBuffers 自体は INVALID_OPERATION を出すので、その前の呼び出しの残りの error と見られる（WS068 の領域、未調査）。
- `egltest6` の guest の log: `ufs-cat.py` が zdesktop.log を sparse として読めない run がある（ssbo1）。そのときは mview.log を
  別に読む（`ufs-cat.py ... /var/log/mview.log`）。
