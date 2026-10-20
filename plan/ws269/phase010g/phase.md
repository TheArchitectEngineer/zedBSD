<!-- awesome-plan project=zedbsd record=ws269-p010g -->

# ws269-p010g: render pass・framebuffer・load/store・clear・job の列

- ID: ws269-p010g（親 [ws269](../ws.md)）
- Status: in-progress（q1382-i01 は前半。q1383-i01 で kick への受け渡し（draw の無い render pass）。実機は未確認）
- Disposition: normal
- Queue: q1382 / q1382-i01（2026-10-20 P2、前半）
- 設計: [design-p010.md](../design-p010.md) 版 2.2 の §2・§4.3・§10 の p010g
- 依存: O-p010e（kick の関数。前半は依らない）

## 受け入れ条件（design-p010.md §10 の p010g の行）

render pass・framebuffer・begin/end・load/store・全面の clear attachments・barrier、render の job の列（§2）、部分の renderArea（color は受ける、depth の STORE がある時だけ拒否）、2 subpass の error の値。host 試験（render pass の decode と job の列）、build。

## q1382-i01 の範囲（前半、Q1 の依頼）

Venus の command の decode と render の job の列の組み立てまで。job は p010e の後半で P1 が作る kick の関数に渡せる struct で止め、kick は呼ばない。dynamic rendering は Venus の wire に命令が無い（`include/uapi/gpu-op.h` に BeginRendering が無い）ので対象外。

## 作った物（2026-10-20 P2）

- `src/drivers/gpu/powervr/render-pass.{h,c}`（新規、純粋な関数、host でも build）:
  - `powervr_pass_build`: VkRenderPassCreateInfo を `struct powervr_pass` に。subpass は 1 つ、color は 4 まで、attachment は 5 まで、1 sample、input・resolve・preserve なし、format は image-state.c の表の物。他の形は **ENOBUFS**。
  - `powervr_render_job_begin`: pass・framebuffer・renderArea・clear の値から `struct powervr_render_job`。job の中身は color target（PBE の入力の `powervr_render_surface` と load の入力の `powervr_view_shape`、load/store、clear の 4 word）、depth（format・address・load/store・clear の depth の bit）、area。view の level と layer の address と大きさ（linear の row・twiddled の padded 幅）を出す。area が framebuffer の外なら EINVAL、clear の値の無い CLEAR は EINVAL。**部分の renderArea は color は受け、depth の STORE がある時だけ EPERM**。
  - `powervr_render_job_clear`: vkCmdClearAttachments。draw の前の全面（layer 0）の clear は load の CLEAR に変える。部分・draw の後は ENOTSUP（p011）。
- `src/drivers/gpu/powervr/vk-pass.c`（新規、kernel 側）:
  - route: CREATE/DESTROY_RENDER_PASS、CREATE/DESTROY_FRAMEBUFFER（framebuffer は view を retain し、各 view の shape を持つ）。render pass の形の拒否は log の 1 行（`powervr: render pass: 2 subpasses not supported`）と **VK_ERROR_OUT_OF_HOST_MEMORY**。
  - record: CmdBeginRenderPass（pass・framebuffer を hold、area と clear の値を record の data に）、CmdClearAttachments（attachment と rect を data に）。wire の形は i915 の `i915_record_begin_pass`・`i915_record_clear_attachments` と同じ。
  - `powervr_vk_render_jobs`: command buffer の record から job の列。begin → job、clear attachments → 適用、end → 列に足す。部分の area と depth の STORE の instance は log の 1 行（`powervr: render pass: partial render area with depth store not supported`）を出して捨てる。
- 既存の Vulkan 側の変更:
  - `vk.h`: record に `data`（record_drop で free）、image view に base_level・base_layer、新しい関数の宣言。
  - `vk-object.c`: ENOBUFS → VK_ERROR_OUT_OF_HOST_MEMORY。
  - `vk-command.c`: BEGIN/NEXT_SUBPASS/END_RENDER_PASS・CLEAR_ATTACHMENTS を記録。NEXT_SUBPASS は ENOTSUP。barrier は今までどおり何も残さない（job の境で全部を待つ、§2）。
  - `vk-image.c`: view に level と layer を残す。
  - `vk-stream.c`: route に pass を足した。
  - `vk-queue.c`: render pass の instance を含む command buffer の submit は ENOTSUP（kick ができるまで黙って飛ばさず断る）。
  - `platform/riscv64/vmunix.mk` に vk-pass.c・render-pass.c。

## 確認（2026-10-20）

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| host 試験 | `sh plan/ws269/tests/p010/run.sh OUT`（`pass-host-test.c` を足した） | `render-pass host test: 15 passed, 0 failed`。同じ run.sh の他（stage 24・image 55・target 77・kick 50・selftest・data 83・render 26・transfer 22・render-load 5）も PASS |
| VF2 の kernel | `timeout 3600 make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p010g/vf2 vmunix -j16` | rc=0、warning 0 |

- 未実施: vk-pass.c の wire の decode と `powervr_vk_render_jobs` の host 試験（kernel の object の層に依るので host では作れなかった。decode の形は i915 と同じ）、QEMU・実機。PBE の word（`powervr_render_pbe_words`）は job の `surface` と `area` からそのまま作れる。

## 残り（後半、p010e の後半の後）

- `powervr_vk_render_jobs` の job を P1 の kick の関数に渡す（vk-queue.c の ENOTSUP を置き換える）。draw の record（p010i）で `draw_count` を数える。
- vkCmdClearAttachments の部分・draw の後（p011）。dynamic rendering は Venus の wire に来たら。

## q1383-i01（2026-10-20 P1、後半の kick への受け渡し）

- `render-job.{c,h}`（新規、kernel）: `powervr_render_job_run` が `struct powervr_render_job` を render context の kick に渡す。draw の無い job（空の VDM stream）、color 1 つ（32 bit の texel）、depth は load・store とも DONT_CARE の時だけ（他は ENOTSUP）。background は load の op で: CLEAR は P2 の clear（clear の値の 1 word）、LOAD は P2 の render-load-program（image と sampler の 8 word を GENERAL に、DMA の PDS）、DONT_CARE は 0 の clear。PBE の store は render area で clip、`pds_pr_bgnd` も同じ background。program と word は job ごとの memory（GENERAL・PDS・USC の窓）に置き、終われば返す。時間切れは session を abandoned にし memory を core に残す。
- `vk-queue.c`: render pass を含む command buffer の ENOTSUP を置き換えた。command buffer の `powervr_vk_render_jobs` の job を、begin の位置で（それより前の dispatch の stream を先に流してから）1 つずつ流す。捨てられた instance があり数が合わない時は、残りを command buffer の終わりにまとめて流す。
- 試験: p009 の run.sh に vk-pass・render-pass・render-program（P2 の merge で link が切れていた）と、`powervr_render_job_run` の host の stand-in。gcc の -O1 が vk-pass.c の area を maybe-uninitialized と言うので p009 の compile に `-Wno-maybe-uninitialized`（kernel の clang は言わない）。
- 確かめ: p009 165/0、p010 の 12 本 0 failed、VF2・riscv64-virt・amd64 の build warning 0。実機は未確認（draw の無い render pass の clear と store）。
- 残り: MRT（color 2 つ以上、EOT が 1 つの PBE の state だけ）、depth の ZLS（p010j）、draw（p010i）、64・16・8 bit の texel。
