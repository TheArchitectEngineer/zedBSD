<!-- awesome-plan project=zedbsd record=ws269-p010i -->

# ws269-p010i: graphics の pipeline と draw

- ID: ws269-p010i（親 [ws269](../ws.md)）
- Status: in-progress（q1384-i01 は前半。kick への受け渡し・GPU の memory への配置・実機は後半）
- Disposition: normal
- Queue: q1384 / q1384-i01（2026-10-20 P2、前半）
- 設計: [design-p010.md](../design-p010.md) 版 2.2 の §4.4・§10 の p010i
- 依存: O-p010g（render-pass.c・job の列、main に merge 済み）、O-p010h（descriptor・data-program.c）、O-VSFS（compiler）

## 受け入れ条件（design-p010.md §10 の p010i の行）

compile の入力の組み立て、vertex・fragment の PDS、固定の state、viewport・scissor（dynamic を含む）、VDM の draw（index あり・なし）、gpu-selftest の `draw`・`sample`。host 試験（vkdemo の shader の pipeline と draw の stream の golden）、build。実機: gpu-selftest の `draw`・`sample`。

## q1384-i01 の範囲（前半、Q1 の依頼）

kick に依らない部分: pipeline の decode、compile の入力（key）、vertex・fragment の PDS、固定の state（PPP の word）、viewport・scissor、VDM の draw の stream、draw の記録と render job への付加。触らない: render-queue.c・render.c・render-kick-layout*.c・render-private.h・render-context.c・render-check.c・render-command.{c,h}・vk-queue.c。

## 作った物（2026-10-20 P2）

- `src/drivers/gpu/powervr/render-draw.{h,c}`（新規、純粋な関数、host でも build）:
  - `powervr_draw_fixed_build`: VkGraphicsPipelineCreateInfo から固定の state と vertex・fragment の compile の key（属性の format、color attachment の format と blend）。受ける形: vertex＋fragment、TRIANGLE_LIST/STRIP（restart なし）、FILL、cull（FRONT_AND_BACK 以外）、1 sample、viewport・scissor 1 つ（static か dynamic）、depth test（stencil・bounds なし）、blend（logic op なし）、dynamic は VIEWPORT・SCISSOR・LINE_WIDTH・BLEND_CONSTANTS。他は ENOTSUP。
  - `powervr_draw_varyings_build`: fragment の coefficient の PDS（w と各 varying の DOUTI、Mesa の `pvr_graphics_pipeline_setup_fragment_coeff_program`・`pvr_pds_coefficient_loading`）、OUTPUT_SEL（vtxsize・rhw）、VARYING0（smooth・flat の数）、usc の varying の大きさ。vertex の varying は smooth の後に flat の順を要する（TSP の順、違えば ENOTSUP）。
  - `powervr_draw_viewport_words`: a0・m0・a1・m1・a2・m2 を float の bit から整数だけで、Mesa の単精度の計算と同じ 1 回の丸めで（kernel は浮動小数点を使わない）。
  - `powervr_draw_scissor_words`: viewport と scissor の重なり（Mesa の `pvr_get_viewport_scissor_overlap`）の IPF_SCISSOR の 2 word と、16 の tile の region clip。
  - `powervr_draw_ppp_build`: 1 draw の PPP の state を毎回全部（header、ISPCTL・ISPA・ISPDBSC、PDS の ptr0・1・3、region clip、viewport、wclamp、OUTPUT_SEL、VARYING0〜2、PPP_CTRL、STREAM_OUT0、25 word）。depth は attachment が無ければ ALWAYS・書かない（`vk_optimize_depth_stencil_state` の形）、pass type は blend があれば TRANSLUCENT、他は OPAQUE。
  - `powervr_draw_vdm_build`: PPP_STATE、vertex の shared の PDS_STATE0〜2（shared がある時）、VDM_STATE0・2〜5（cam は BXE-4-32 の表）、INDEX_LIST0〜4（index あり・なし、instance、vertex offset）。
- `src/drivers/gpu/powervr/vk-draw.{h,c}`（新規、kernel 側）:
  - `powervr_vk_graphics_decode`: vkCreateGraphicsPipelines の record（i915 と同じ wire）を VkGraphicsPipelineCreateInfo に戻し、render pass と shader module を引き、固定の state・key を作り、vertex・fragment を自分の compiler で key 付きで compile し、varyings を作る。
  - draw の record: BindVertexBuffers（buffer を hold、4 つまで）、BindIndexBuffer（16・32 bit）、SetViewport・SetScissor（0 番、1 つ）、PushConstants（128 byte まで）、Draw・DrawIndexed（firstInstance は 0 だけ）。
- 既存の Vulkan 側の変更:
  - `vk-layout.c`: GPU_OP_CREATE_GRAPHICS_PIPELINES を受ける（compute と同じ create の道）、pipeline の destroy で graphics を free。
  - `vk-command.c`: graphics の bind point の pipeline と descriptor set を受ける。record の opcode は自分の値（`POWERVR_VK_RECORD_BIND_GRAPHICS`・`_SETS_GRAPHICS`）にして、vk-queue.c の compute の walk が取り違えないようにした。draw の record を route。
  - `vk.h`: pipeline に `graphics`、record の opcode、関数の宣言。
  - `render-pass.h`: `struct powervr_render_draw`（pipeline・set・vertex buffer の address・index・count・instance・vertex offset・viewport・scissor・push constant）と job の `draws[16]`。
  - `vk-pass.c`: `powervr_vk_render_jobs` が record を順に読み、state（pipeline・set・vertex/index buffer・viewport・scissor・push）を追い、draw ごとに job に足して `draw_count` を数える。job は呼び手の配列の中に直接組む（job は 1 つ約 4.5 KB、呼び手は heap に置くこと）。
  - `platform/riscv64/vmunix.mk` に render-draw.c・vk-draw.c。

## 確認（2026-10-20）

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| host 試験 | `sh plan/ws269/tests/p010/run.sh OUT`（`draw-host-test.c` を足した） | `render-draw host test: 30 passed, 0 failed`（vkdemo・vkstage の colour・texture の pipeline を key 付きで compile、varyings、PPP・VDM の golden、indexed の list、viewport・scissor、refusal）。同じ run.sh の他（stage 24・image 55・target 77・kick 81・program 38・command 22・selftest・data 83・render 26・transfer 22・render-load 5・render-pass 15）も PASS |
| M9 の照合（tree に入れない） | `build/p010i/m9.c`（Mesa 26.2.5 の gen_pack_header を `python3 -I` で走らせた ppp・vdm・pds・ipf の header） | `M9: 41 words/values compared, 112 fields, 0 mismatches`（vkdemo の PPP 25 word、DOUTI 2、VDM 12、indexed の INDEX_LIST0、IPF_SCISSOR_WORD_0） |
| VF2 の kernel | `timeout 3600 make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p010i/vf2 vmunix -j16` | rc=0、warning 0 |

- M9 の限界は前と同じ（Mesa がどの値を選ぶかの読み違いは見つからない）。
- 未実施: vk-draw.c の wire の decode と `powervr_vk_render_jobs` の draw の付加の host 試験（kernel の object の層に依る）、QEMU・実機。

## 残り（後半）

- job の draw ごとに: vertex の PDS（data-program.c の `powervr_data_vertex_build` に属性の DMA）、vertex・fragment の shared の PDS（`powervr_data_resolve`・`powervr_data_shared_build` に set・push constant）、fragment の kick（`powervr_render_kick_build`）、coefficient の PDS、USC の code を GPU の memory に置き、`powervr_draw_ppp_build`・`powervr_draw_vdm_build` で stream を組み、P1 の kick に渡す（scissor の表の address は fragment の command）。
- gpu-selftest の `draw`・`sample` の実機。
- 2 つ以上の viewport・stencil・depth bias・MSAA・primitive restart・firstInstance・instance rate の属性（p011）。

## q1385-i01（2026-10-20 P1、後半: draw を kick に）

- `render-job.c`: job の draws を載せた（draw の ENOTSUP を外した）。draw ごとに、vertex と fragment の USC の code（draw の USC の buffer に並べる）、set の写し（`set->bytes` を draw の GENERAL に、`powervr_data_resolve` の set の address に）、push constant、0 の blend constant、vertex と fragment の shared の PDS（要求が無ければ halt 1 つ）、vertex の PDS（attribute ごとの DMA、stride は binding から、`powervr_data_vertex_build`）、fragment の kick の PDS、coefficient の PDS（data の後に code）、scissor の表の 1 entry（index = draw の番号）を置き、P2 の `powervr_draw_ppp_build`・`powervr_draw_vdm_build` の stream を順につなぎ、最後に terminate。geometry の resume size は vertex の shared の最大。fragment の command に scissor と depth bias（0 の entry）の表の address。
- `render-command.{c,h}`: fragment の `isp_scissor_base`（16+96）・`isp_dbias_base`（16+104）、bits 2..39。
- `vk-draw.h`: `struct powervr_vk_graphics` を vk-draw.c から header へ（render-job が読む）。
- 確かめ: p010 の host 試験 全部 0 failed、p009 165/0、VF2・riscv64-virt・amd64・試験の image の build warning 0。実機（vkstage の draw・sample）は未確認。
- 未対応・推測: render check の三角形は今も自分の固定の program の道（vk の pipeline を通さない）。depth bias の表は 0 の 1 entry、MRT・depth は render-job の制限のまま。
