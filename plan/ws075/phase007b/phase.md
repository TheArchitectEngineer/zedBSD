<!-- awesome-plan project=zedbsd record=ws075-p007b -->

# ws075-p007b: GL 3.2 の stage の実行器（render/）: 増分 b1〜b5

Status: in-progress（q833、P1。2026-10-07 夜 b3、2026-10-07 b1 を実装と host 試験。次は b2）
Disposition: normal
Parent: [WS075](../ws.md)
設計: [phase007/design.md](../phase007/design.md)（§5・§14 が優先）、増分の表は [phase007/phase.md](../phase007/phase.md)。

## 承認

Q1（2026-10-07）: 判断 1〜8 を既定どおりで承認、Phase の ID は p007a・p007b で可、b3 は p007a より先でよい。b3 の範囲の ACK（2026-10-07 夜）。

## 記録

- 2026-10-07 夜 増分 b3（layered の描画、p007a に依存しない）:
  - `render/gfx.h`: framebuffer に `layers`、clear_attachment の op に `base_layer`・`layer_count`。
  - `render/render-pass.c`: `VkFramebufferCreateInfo.layers` を持つ（0 は 1）。
  - `render/state.c`: `i915_state_target_range` は view の `layer_count` が 2 以上（3D の image でない）なら layer_count に（Surface Array、Depth と Render Target View Extent = layers − 1）。3DSTATE_DEPTH_BUFFER と 3DSTATE_STENCIL_BUFFER の Depth を「image の全 slice − 1」から「view の layer − 1」に、dword 7 に Render Target View Extent（bits 31:21）を足した（isl `isl_emit_depth_stencil.c` 147〜163、gen120.xml bits 245〜255、出典は code の注に sha256）。
  - `render/command.c`: pass begin の clear は attachment ごとに framebuffer の layers（view の layer 数で頭打ち）の各 layer を 1 layer の view で fill（本体を `i915_execute_clear_layer` に分けた）、vkCmdClearAttachments は VkClearRect の `baseArrayLayer`・`layerCount` を記録し layer ごとに fill（本体を `i915_execute_clear_rect` に、count 0 は 1 と読む）。
  - `render/instance.c`: `maxFramebufferLayers` 2048。
  - 試験（新）: `plan/ws075/tests/host-layered.c`・`run-host-layered.sh`（framebuffer の layers の decode と 0 → 1、2 layer の framebuffer で begin の clear が 2 layer・ClearAttachments の 2 layer の rectangle が 2 fill（各 layer の address と rectangle）、1 layer の framebuffer では begin が 1 layer、2 layer の色の target の surface state の Surface Array・Depth 1・RTVE 1、2 layer の depth view の 3DSTATE_DEPTH_BUFFER の Depth 1・RTVE 1・QPitch）。

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws075/tests/run-host-layered.sh` | plain・ASan/UBSan PASS、leak 0 |
| `sh plan/ws031/tests/run-vk-host-tests.sh "res resdispatch pipe cmdbuf"` | PASS（design §12 の 13 の cmdbuf の既存の失敗 `test_blend_state` は今の main では PASS、直す物は無い） |
| `sh plan/ws083/tests/run-host-video-roundtrip.sh` | PASS（command.c の変更の後も） |
| `make -j16 BUILD=build/p1-k ZEDBSD_CONFIG=config/ci/config-amd64.mk build/p1-k/vmunix` | 成功 warning 0 |

未実施: 実機（RTAI による layer の書き分け、design §12 の 10。b5 で T1）。libvulkan の wire が `layers`・`VkClearRect` の layer を運ぶことは codec の decoder（generic）と host の wire で確かめた（§12 の 11 の b3 の分）。

残り: b2・b4・b5（b1 は下）。

- 2026-10-07 増分 b1（pipeline の 3 stage、P1 の新しい世代、base main b009070c8 + P1 の 051f4bce8・66b1f037f）。再開の前に 10 個の host 試験を流し全て PASS（4 分）。
  - `render/gfx.h`: pipeline に `geometry`（module）と `gs_binary`。`render/pipeline.c`: `VK_SHADER_STAGE_GEOMETRY_BIT` を `pipeline->geometry` に。
  - `render/pipeline-prepare.c`: VS → GS（`drv_i915_shader_compile_stage(ir, vs_binary)`）→ FS の順。GS は compile の前に IR の `LOAD_VERTEX_INPUT` の location（Position・PointSize を除く）が VS の varying にあるかを確かめ、無ければ「the geometry shader reads location N, which the vertex shader does not write」と ENOTSUP。FS の入力は最後の stage（GS があれば GS）の varying と照合し、log は stage の名前を出す。GS の fit（新 `i915_pipeline_geometry_fits`）: code ≤ PS − GS（16 KiB）、push constant ≤ 一つの command buffer の block、push data ≤ 1 KiB（minor 8）、sampler 0。VS の上限は GS − VS（S7）。compile の refuse の log に GS の IR の事実（入力の頂点の数・最大の出力の頂点・topology）を足した。release は `gs_binary` も。`drv_i915_gfx_pipeline_kernels` は GS の field を写し、`varyings`・`vs_point_size`（S3）・`ps_input_slots` を最後の stage から。
  - 決め（p007a の再開の情報の b1 の項）: compiler の API は変えない。prepare で判る物（producer の location、push data、sampler、window の枠）は prepare で理由付きに検査。URB entry > 32 KiB など compiler の中の refuse は「refused by the compiler: error 95」と IR の事実（`the refused geometry shader: 1 vertices in, at most 256 vertices out (topology 1)`）の 2 行で、理由の文字列は compiler に持たせない（diagnostic の口を compile に足すのは後の候補）。
  - `render/heap.h`: window 64 KiB（`I915_GFX_INSTRUCTION_BYTES` 0x10000）、VS 0x0000、新 `I915_GFX_GS_KERNEL` 0x4000、PS 0x8000。kernel object は 32 × 64 KiB（+512 KiB、minor 4。instruction heap の clear も window の大きさで回る）。`render/state.h`: kernels に GS の field（code・bytes・grf start・push・vertices in・output topology・vertex/control の hword・control format・URB entry・PrimitiveID・layer・scratch）。
  - `render/draw.c`・`draw.h`: `drv_i915_gfx_window` に gs_code/gs_bytes（GS の枠に写す）。呼び出し（draw.c、compute.c、blit.c の 3 つ）を更新。**draw は GS のある pipeline を b2 まで refuse**（「draw refused: the geometry stage is not programmed yet」、ENOTSUP。3DSTATE_GS・URB を出さずに走らせない）。feature `geometryShader` は b4 まで出さない（今のまま FALSE）。
  - 試験（新）: `compiler-shaders/varyings.vert`（varyings.geom が読む 0・1・2 を書く VS、regenerate.py の GEOMETRY の表に足した。他の `.spv`・`.inc` は不変）。`plan/ws031/tests/i915-vk-pipe-test.c` の `test_geometry_pipeline`（wire で cells.vert + points.geom + passthrough.frag: geometry の module、3 つの kernel、GS の field（vertices in 1、triangle strip、varying 1、URB entry 5 × 64 B）、kernels の GS の field と最後の stage からの slot、destroy で leak 0）と `test_geometry_interfaces`（varyings.vert+varyings.geom+primitive-id.frag は gl_PrimitiveID を GS の VUE の slot 2 から、GS 無しの primitive-id.frag は refuse、GS の入力が VS に無い・FS の入力が GS に無い（VS にはある）・72 KiB の URB entry は refuse と log の行）。既存の 2 stage の pipeline の検査は不変（kernels の GS の field が 0 も足した）。

| コマンド（b1） | 結果 |
| --- | --- |
| `sh plan/ws031/tests/run-vk-host-tests.sh`（10 個、b1 の前と後） | 前・後とも plain・ASan/UBSan 全て PASS |
| `python3 src/drivers/gpu/i915/tests/render/compiler-shaders/regenerate.py` | 成功、既存の `.spv` と kernel の `.inc` は不変、`varyings.vert.spv` を追加 |
| `sh plan/ws075/tests/run-host-layered.sh`・`sh plan/ws083/tests/run-host-video-roundtrip.sh` | PASS |
| `make -j16 BUILD=build/p1-k ZEDBSD_CONFIG=config/ci/config-amd64.mk build/p1-k/vmunix` | 成功 warning 0（include check・vmunix check PASS） |
| `I915_TESTS=y I915_TEST_SET=vkx`・`vkc`・`vke1`・`vke2`・`compute` の vmunix（BUILD=build/p1-kt-SET） | 全て成功 warning 0 |
| `python3 plan/tools/style-check.py`（変えた file） | 増えない（pipeline-prepare.c は 1 → 0） |
| `git diff --check` | 問題無し |

- 範囲外（Q1 へ）: `plan/ws101/tests/host/compute-batch-test.c` の `drv_i915_gfx_window` の stub に gs_code/gs_bytes の 2 引数が要る（P1 の範囲外なので差分を Q1 に送った）。また `plan/ws101/tests/host/run.sh` は今の main で既に link に失敗する（executor の一覧に `render/video.c` が無い、WS083 の後）。
- 未実施: 実機（b5 で T1）。b1 だけでは GS の draw は refuse（b2 で 3DSTATE_GS）。


### 再開の情報（2026-10-07、P1。b1 の後に更新）

- 済み: b3・b1。次: b2（design.md §5.2、§14 S6、phase007/phase.md の表）。draw.c の「the geometry stage is not programmed yet」の refuse を b2 で外す。

### 再開の情報（旧、2026-10-07、P1。ws113-p011a の merge を受け、Q1 の指示で ws051-p004b の code を優先して中断）

- 済み: b3。base main 3b004a2c6 で `sh plan/ws031/tests/run-vk-host-tests.sh`（10 個）を流し plain・ASan/UBSan 全て PASS（3 分 49 秒）。
- b1 は code に未着手（変更無し）。次: b1（design.md §5.1・§14 S3・S7・minor 8、phase007/phase.md の増分の表）。p007a の再開の情報の b1 の項（prepare の GS の検査と compiler の refuse の理由の log の決め）もここで扱う。
- 再開の前に: main の今を merge、10 個の host 試験を一度流す。
