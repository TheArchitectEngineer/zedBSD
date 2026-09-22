# WS031 p014 計画: モデルビューア（mview）を i915 で動かし、i915 のシェーダーを一通り動かす

p013 の mview（Venus で PASS）を QEMU＋passthrough（Latitude 5330）の i915 で動かす。ビューアが要る機能から順に i915 の
Vulkan executor（`src/drivers/gpu/i915/render/`）と SPIR-V compiler（`src/drivers/gpu/i915/compiler/`）を拡張し、各段で
小さな試験 shader を実機で確かめてから先へ進む。その後、ビューア以外の shader も一通り通るよう compiler を広げる。

## 現状との差（2026-09-22 調査）
mview の shader は意図して単純（行列型なし、push constant のみ、varying 2、頂点入力 3）。それでも次が足りない。

| 区分 | mview が使うもの | i915 の現状 |
| --- | --- | --- |
| 描画命令 | `vkCmdBindIndexBuffer`/`vkCmdDrawIndexed`（uint32） | 未実装（ENOTSUP） |
| 動的 state | `vkCmdSetViewport`/`vkCmdSetScissor`、dynamic state | 未実装（pipeline で警告） |
| 転送 | `vkCmdCopyBuffer` | 未実装 |
| texture | mip 付き RGBA8（`vkCmdBlitImage` で mip 生成）、trilinear sampler | 1 mip・LOD 0 固定・bilinear まで |
| push constant | fragment stage（offset 112 の material 色） | vertex stage だけ |
| command buffer | 1 frame で約 70 操作、upload で数百 | 1 command buffer 64 操作まで |
| blending | blend pipeline（qs40 は blend material なし、生成は必要） | 生成は通るが無視（XXX） |
| compiler（VS） | `normalize`、`max`（GLSL.std.450） | Sin/Cos/InverseSqrt のみ |
| compiler（FS cutout） | `FOrdLessThan`、`OpSelectionMerge`/`OpBranchConditional`、`OpKill` | 1 basic block のみ、discard なし |
| 検証 | 画面の取得 | passthrough は VNC がない（カメラのみ） |

## 段階
各段は「実機の小試験 PASS → 回帰（vkdemo offscreen `7523debe…05ff`、display `94615464…19b1` ended PASS、wltest、ktest）→ 次へ」。

### A0. capture display（試験 build 専用、ユーザー承認 2026-09-22）
iGPU に専用 VRAM はなく、描画結果は guest RAM にある。`I915_TEST_CAPTURE=y` の build では i915 は LCD を初期化せず
（modeset・panel 電源なし）、display node に「取り込み用の仮想 display」を出す（query/mode/claim は従来どおり）。present は GPU copy で
物理連続の capture 領域（複数 slot、header に frame 番号・寸法・hash）へ写し、領域の guest 物理 address を起動時に 1 度 log する。
host は `run-parity-vk.sh` に QMP socket・`usb-tablet`・`usb-kbd` を足し、`pmemsave` で領域を読んで PPM にする。p013 の Venus 試験
（`mview-qemu.py`）と同じ操作列・同じ 6 検査を i915 で行い、Venus 画像とも比べる。以後の段はこの経路で確かめ、実 LCD 経路
（`vkloop-hw.sh display`、ended PASS）は各段の区切りと Phase の最後にカメラで確かめる。

### A. 実行器の基礎（mview の描画命令）
1. `vkCmdBindIndexBuffer`/`vkCmdDrawIndexed`（uint16/uint32、`3DSTATE_INDEX_BUFFER`、`3DPRIMITIVE` の random access）。
2. `vkCmdSetViewport`/`vkCmdSetScissor` と dynamic state（`3DSTATE_VIEWPORT_STATE_POINTERS_*`/`SCISSOR`）。
3. `vkCmdCopyBuffer`（blitter か既存の GPU copy 経路）。
4. command buffer の操作数の上限を撤廃（可変長の op 列）。descriptor set の割当上限（1 回 8）も確認して必要なら緩める。
5. fragment stage の push constant（`3DSTATE_CONSTANT_PS`）。
- 小試験: 索引付き三角形、viewport の一部だけに描く、buffer copy の byte 一致、70 操作の command buffer。

### B. texture と mip
1. mip 付き image の配置（linear のまま mip ごとの offset、`RENDER_SURFACE_STATE` の MIP count/LOD）、mip 間の `vkCmdBlitImage`。
2. sampler の mipmap mode（nearest/linear）、min/max LOD、LOD bias。
3. format 特性の報告（RGBA8 の BLIT/LINEAR、D32 の attachment）が実装と一致すること。
- 小試験: 各 mip を別色で塗った texture を縮小して描き、選ばれた mip の色を readback で確認。

### C. compiler: mview の shader
1. GLSL.std.450 の `Normalize`、`FMax`/`FMin`、`FClamp`、`FAbs`、`Sqrt`、`Pow`、`FMix`、`Floor`/`Fract`（EU の math 命令と組合せ）。
2. 比較（`FOrd*`/`FUnord*`/整数比較）→ flag register、`OpSelect`。
3. 構造化制御フロー（`OpSelectionMerge`＋`OpBranchConditional`）を SIMD8 の predication/IF-ELSE-ENDIF で。
4. `OpKill`（discard）: pixel mask を落とし、全 channel が消えたら早期終了。
- 小試験: 各命令を 1 つずつ使う shader を readback で CPU 参照値と比較（`tests/render/` の oracle 方式）。

### D. mview を i915 で動かす
1. 試験環境: `run-parity-vk.sh` に `qemu-xhci`＋`usb-tablet`＋`usb-kbd`、QMP socket（`input-send-event`）を足す。
2. capture display（A0）の画像を Venus の画像と比べる（GPU が違うので完全一致でなく許容差: 平均差・PSNR と形の一致）。
   `R` で初期 frame と画素一致は i915 でも必須。
3. `vkloop-hw.sh mview`（zwl＋mview、QMP で p013 と同じ操作列）とカメラ写真で確認。
- 受入: model_visible・drag_rotates・right_drag_pans・wheel_zooms・keys_turn・reset_restores_first_frame が i915 で真、
  Venus 画像との許容差内、写真確認。

### E. blending と「一通りのシェーダー」
1. color blending（`BLEND_STATE`/`3DSTATE_PS_BLEND`、src/dst factor、op、write mask）。mview の blend pipeline を試験 model で確認。
2. compiler を一般化: `OpTypeMatrix`/`OpMatrixTimesVector`/`OpMatrixTimesMatrix`/`OpTranspose`、`FDiv`/`FMod`、整数演算、
   `OpLoopMerge` のループ（有界ループ・break/continue）、varying/頂点入力の上限拡大（16 まで）、register spill（scratch）、
   uniform buffer（`VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`、binding table 経由の読み）、複数 sampler。
3. shader 試験一式（`plan/ws031/tests/shaders/`）: GLSL→SPIR-V（glslc）を i915 で実行し CPU 参照と比較する表形式の試験。
   mview にも「行列 uniform＋per-pixel 光源」版の shader を追加して実機で使う。

### F. 性能（任意）
draw ごとの同期 batch を 1 command buffer 1 batch にまとめる、同期 present の見直し。mview の fps を記録。

## 依存・触れないファイル
- 前段: p013（mview、zwl 入力）、E-133（i915 の Wayland）。
- 触れる: `src/drivers/gpu/i915/render/**`、`compiler/**`、`tests/**`、`userland/base/mview/`（`--readback`、追加 shader）、
  `plan/ws031/tests/`、5330 の `~/bigbang/run-parity-vk.sh`（入力 device と QMP）。
- 触れない: UAPI、libvulkan の wire（必要なら事前に提示）、Venus 経路。

## 見積
A0 0.5 日、A 1 日、B 1 日、C 1.5 日、D 1 日、E 3 日、F 任意。D までで「mview が i915 で動く」、E までで「一通りのシェーダー」。

## 進捗
### A0 完了（2026-09-22）
- kernel: `display/capture.c`（`I915_TEST_CAPTURE=y` の時だけ build）。LCD は使わず（display absent 扱い）、present を GPU copy で
  連続 32 MiB の capture 領域（4 slot）へ。layout は capture.c 冒頭。production vmunix は変更前と byte 一致、host 試験 23/0。
- host: `~/bigbang/run-parity-vk.sh` に `QMP=1`（QMP socket・usb-tablet・usb-kbd）と `VK_STOP_RE`、
  `plan/ws031/tests/i915-capture.py`（pmemsave で読んで PPM、scenario 判定）、`vkloop-hw.sh` に `mview` mode と `CAPTURE=<scenario>`。
- 実機: `CAPTURE=vkdemo vkloop-hw.sh display` PASS（取り込んだ画素の RGB hash が vkdemo の報告 `94615464…19b1` と一致）、
  `CAPTURE=wayland vkloop-hw.sh wayland` PASS（wltest の frame を取得、動きあり、WLTEST DONE 600/60、ZWL EXIT error=0）。
### A 完了（2026-09-22）
- executor（`render/`）: `vkCmdBindIndexBuffer`/`vkCmdDrawIndexed`（uint16/uint32、bind offset・firstIndex・vertexOffset・instance、
  `3DSTATE_INDEX_BUFFER`＋`3DPRIMITIVE` RANDOM/Base Vertex）、`vkCmdSetViewport`/`vkCmdSetScissor`（index 0、pipeline の dynamic state で
  pipeline 値を上書き、未設定の draw は拒否）、`vkCmdCopyBuffer`（4096 texel 行の linear RGBA8 GPU copy、4 の倍数でない region は XXX で ENOTSUP）、
  op 列を可変長に（64 から倍々、上限 65536、超過・確保失敗は end が VK_ERROR_OUT_OF_HOST_MEMORY）、fragment の push constant
  （`3DSTATE_CONSTANT_PS` buffer 3、`3DSTATE_PS` Push Constant Enable）。compiler は既に FS の push load に対応しており変更なし。
  genxml.h に Mesa 25.0.7 gen120.xml（sha256 e2452c7d…542e）から INDEX_BUFFER・Vertex Access Type・PS Push Constant Enable。
  mview の他の上限（set 割当 1 回 1、bind 1、pipeline 作成 1 回 1×6、submit 1）は現行上限内で変更なし。
- 実機: `vkloop-hw.sh test vkx`（`tests/render/executor.c`）5/5 PASS: INDEX16・INDEX32・VIEWPORT・COPY（byte 一致）・GRID（134 操作、FS が byte 112 の push 色）。
- 回帰: offscreen `7523debe…05ff`、ktest 383/0（12 skip）、draw 1024/1024、tex 1024/1024、t3 9/9、bl 4/4、`CAPTURE=vkdemo display`
  PASS（capture_equals_presented、`94615464…19b1`）。host: vk fixture 全一覧 PASS、contracts PASS。

### C 完了（2026-09-22）
compiler に GLSL.std.450（Normalize/FMax/FMin/FClamp/FAbs/Sqrt/Pow/FMix/Floor/Fract/Exp2/Log2）、FDiv、比較 12 種・論理・Select、
選択構造の if-conversion（predication）、OpKill（f1.0 の pixel mask、RT write を predicate）。実機 `test vkc` 9/9 PASS。

### D: mview が i915 で動作（正常系 1 パス、2026-09-22）
- capture display（`CAPTURE=mview vkloop-hw.sh mview`）: 6 検査すべて真、`R` で初期 frame と画素一致、Venus 画像との PSNR は
  initial/pan/reset = ∞（完全一致）、rotate/keys 104.8 dB、zoom 107.8 dB（Venus も同じ 5330 の Iris Xe を Mesa anv で使うため）。
- 実 LCD（`vkloop-hw.sh mview`）: 640×480 を 1920×1080 panel に表示、写真確認（`C:\Work\qemu-work\mview-lcd.jpg`）。
- B（mip）は途中の実装のまま tree にあり、mview の経路では問題なし。B の試験（MIP-NEAREST/LINEAR、blit 連鎖）は後回し。

### B 完了（2026-09-22）
- 配置: linear のまま isl の 2D mip layout（ISL_DIM_LAYOUT_GFX4_2D）。level 0 が上、level 1 がその下の左端、level 2 が level 1 の右、
  以降は level 2 の下へ。各 level の幅・高さは 4 texel（HALIGN_4/VALIGN_4、isl の gfx8 linear 32bpp と同じ）に切上げ、全 level で
  pitch 共通。memory requirements は chain 全体（`image->bytes`）。D32 は 1 level のみ（Y tile）。
- `render/image.c`: `drv_i915_gfx_image_layout()`（chain 配置）・`drv_i915_gfx_image_level()`（1 level を linear surface として記述）・
  vkGetImageSubresourceLayout の level offset、view の baseMipLevel/levelCount（VK_REMAINING_MIP_LEVELS 可、範囲外は拒否）、
  sampler の mipmapMode・mipLodBias・minLod・maxLod。
- `render/command.c`: CopyBufferToImage/ImageToBuffer・CopyImage・BlitImage（同一 image の level→level+1 可）・ClearColorImage が
  subresource の mipLevel／range に従う（各 level を独立 surface として GPU rect）。
- `render/state.c`: RENDER_SURFACE_STATE に MIP Count（levelCount−1）・Surface Min LOD（baseMipLevel）・Mip Tail Start（isl と同じく
  linear は level 数）・QPitch。SAMPLER_STATE に mip filter（NEAREST/LINEAR）、LOD bias（S4.8、[−16, 15.996]）、min/max LOD（U4.8、[0, 14]）。
- `render/instance.c`: format 特性は実装どおり（RGBA8/BGRA8 は両 tiling で sampled＋linear filter・colour attachment・transfer・blit、
  D32 は optimal の depth attachment のみ）。vkGetPhysicalDeviceImageFormatProperties は tiling の特性と usage を照合し、
  特性で覆えない usage（D32 の sampled、linear D32、storage）は VK_ERROR_FORMAT_NOT_SUPPORTED。maxMipLevels は colour 15、D32 1。
- 実機 `vkloop-hw.sh test vkx`（`tests/render/executor.c`）**8/8 PASS**（A の 5 step＋B の 3 step、B 分は 1 回目で PASS）:
  - VKX-MIP-NEAREST: 64×64・7 level、level ごとに別色を per-level copy で upload、32/16/8 px の quad（縮小 2/4/8 倍）で level 1/2/3 の色。
  - VKX-MIP-LINEAR: LOD を 1.5 に固定した LINEAR で level 1・2 の半々、bias 2 で level 3、baseMipLevel 2 の view で level 2、
    LINEAR の自然 LOD 2 で level 2、minLod 5／5.5（LINEAR）／6 で level 5・level 5 と 6 の半々・level 6（2×2 と 1×1）。許容差 各 byte ±2。
  - VKX-MIP-BLIT: level 0 に模様を upload、level 1..6 を同一 image 内の linear BlitImage で順に生成、全 level を ImageToBuffer で読出し。
    level 0 は完全一致、level n+1 は level n の 2×2 box filter（丸め平均）と各 byte ±2 以内。
  - 同じ run の vkdemo offscreen は `7523debe…05ff`（参考、回帰の代わりではない）。
- host: `run-vk-host-tests.sh` 全 PASS。resdispatch に format 特性の試験（tiling・usage の照合、D32、3D、storage）を追加。
- 環境: HEAD の `lldb works` で LLVM の patch level が zedbsd6 になり、resident build が LLVM の source 再構築に入って失敗した
  （`build/llvm-source` は消えた）。`~/zedBSD/build/llvm`（zedbsd6 の install、identity 一致）を `build/llvm` へ複写して解決。
  旧 zedbsd5 の install は `build/llvm.zedbsd5-old` に退避（不要なら削除可）。

### E1 完了（2026-09-22）: colour blending・uniform buffer・複数 sampler
前任（停止した stage-E agent）の途中作業は commit `b1dce8ec Vulkan optimize` に性能作業と一緒に入っていた（差分ではなく HEAD の内容として確認）。
blend（pipeline.c の decode、state.c の BLEND_STATE／PS_BLEND、genxml の blend 定義）、uniform block（spirv.c の Uniform Block の
access chain・std140 の Offset／ArrayStride／MatrixStride・row/col major、`I915_IR_LOAD_UBO`、compile.c の push data への配置、
descriptor.c の UNIFORM_BUFFER、state.c の push data への CPU copy、draw.c の「copy 前に先行 transfer を flush」）と複数 sampler
（binary の sampler_set/binding、state.c の binding table）は完成していたので**そのまま引き取り**、以下を足した。E2 範囲（行列演算・ループ・整数演算）も前任が
既に入れているが、E1 では uniform の mat4 × vec4 の経路だけを実機で使った。
- 実装（追加・修正）:
  - `render/gfx.h`/`command.c`/`state.c`/`pipeline.c`: `vkCmdSetBlendConstants`（opcode 98、`I915_GFX_OP_SET_BLEND_CONSTANTS`）と
    `VK_DYNAMIC_STATE_BLEND_CONSTANTS`（`pipeline->dynamic_blend_constants`、未設定の draw は 0 を使い log）。
  - `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC`: descriptor.c が slot に `dynamic` を記録、command.c の
    `i915_record_dynamic_offsets()` が set 順・binding 番号順に dynamic offset を配り（過不足は EINVAL）、実行時 draw state の
    `dynamic_offsets[set][binding]` を state.c の push data copy が descriptor offset に足す。
  - `pipeline-prepare.c`: fragment shader が varying を 1 つも読まない pipeline を許す（Vulkan では合法、以前は拒否）。
  - `command.c` の clear-image 記録: range を memset（GCC -O2 の maybe-uninitialized で host build が落ちたため）。
- genxml（前任が追加、今回 Mesa 25.0.7 Debian source で照合済み）: BLEND_STATE / BLEND_STATE_ENTRY は gen80.xml
  （sha256 2962677c…439d）、3DSTATE_PS_BLEND と COLOR_CALC_STATE は gen90.xml（d86fb566…2ddb）、BLENDFACTOR/BLENDFUNCTION は
  gen40.xml（8fe6663f…f206）。bit 位置・enum 値とも一致。
- lowering の選択: **uniform buffer は push constant と同じ経路**（3DSTATE_CONSTANT_VS/PS の buffer 3）。compiler が shader の読む
  各 block の byte 範囲（32 byte 境界に広げる）を push data の push constant の後ろに配置し、draw 時に CPU が bound buffer から
  その範囲を slot の push data へ copy する（dataport send は使わない）。制約: 1 stage の push data 1 KiB まで、動的 index は
  配列長 `MAX_DYNAMIC_ELEMENTS` までの全要素 load＋select、同じ submit 内の GPU 書込み（copy）の後は draw 前に batch を flush。
- 実機 `vkloop-hw.sh test vke1`（`tests/render/features.c`、shader は `tests/render/feature-shaders/`＋`regenerate.py`）**3/3 PASS**
  （1 回目は ubo.frag が varying を読まないため pipeline 拒否 → 上記の pipeline-prepare 修正で 2 回目 PASS）:
  - VKE1-BLEND: 16 pipeline・16 cell（off、SRC_ALPHA/1−SRC_ALPHA、ONE/ONE、ZERO/SRC_COLOR、DST_COLOR/ZERO、1−DST_COLOR/ONE、
    DST_ALPHA/1−DST_ALPHA、SUBTRACT、REVERSE_SUBTRACT、MIN、MAX、CONSTANT_COLOR（pipeline 定数）、同（vkCmdSetBlendConstants の
    動的定数）、独立 alpha（ZERO/ONE）、write mask R|B（blend off）、write mask G|A＋ONE/ONE）を既知の destination（0.2,0.4,0.6,0.8）上で。
    期待値は regenerate.py が Vulkan の式から計算、各 byte ±1。
  - VKE1-UBO: descriptor は vkUpdateDescriptorSets（wire）。vertex は std140 の mat4（列として読む、平行移動は第 4 列）＋offset で
    quad を配置、fragment は vec4 × scale[1]（ArrayStride 16）＋ extra[2]（mat4 の第 3 列）。2 つ目の draw は dynamic uniform buffer
    ＋dynamic offset 256 で別の material と別 transform。矩形と色が ±1 で一致。
  - VKE1-TEX3: set 0 binding 0（linear）・set 0 binding 2（nearest）・set 1 binding 1（nearest）の 3 texture を 1 回の
    vkCmdBindDescriptorSets（2 set）で。(A.r, B.g, C.b) の全画素が ±2 で一致。
  - 同じ run の vkdemo offscreen は `7523debe…05ff`（参考）。
- mview の blend pipeline: `CAPTURE=mview MVIEW_MODEL=test vkloop-hw.sh mview`（vkloop-hw.sh に `MVIEW_MODEL=test` を追加: test model を
  `/usr/share/mview/test/` に入れて `--model` で表示、Venus 参照は使わない）で 6 検査すべて真。rotate 画像で glass 面（color 0.2,0.6,1.0、
  alpha 0.5）が背景 51 の上で (45,86,126) ＝ 0.5×glass_lit＋0.5×dst（R:G:B の比が 1:3:5 から背景分だけずれる）、cutout 越しの底面も
  床 80 の上で同様に合成されていることを確認（blend されなければ比 1:3:5 のまま）。
  併せて i915-capture.py の mview() の `deadline` 未定義（NameError、性能作業の write_count 化で混入）を修正。
- host: `run-vk-host-tests.sh` 全 PASS。cmdbuf に `test_blend_state`（BLEND_STATE・PS_BLEND・blend constants・MIN/MAX の ONE・
  第 2 source の無効化・3 sampler の binding table/sampler・uniform block と dynamic offset の push data・拒否 2 件）と
  `test_uniform_bindings`（wire の UBO plain/dynamic 更新、dynamic offset の binding 順の割当と過不足の拒否、vkCmdSetBlendConstants）、
  pipe に blend state と dynamic blend constants の decode、lower/compile に feature shader（mat4 の列読み、配列要素、3 sampler の
  set/binding、push data 配置 3+3 register）。
- 環境: vkloop-hw.sh は `BUILD=${BUILD:-build/resident}`（この段は `build/p014-e1`）。`vke1` を vmunix.mk の test build 一覧に追加。
- 後回し（下の一覧に追記）: 下記「E1 の残り」。

### E2 完了（2026-09-23）: SPIR-V compiler の一般化（行列・整数・ループ・16 入出力）
前任（停止した stage-E agent）が commit `b1dce8ec Vulkan optimize` に入れた E2 範囲（行列演算・整数演算・構造化ループ）は、host 試験も
実機試験も無いまま tree にあった（E1 で使ったのは uniform の mat4 × vec4 だけ）。今回それを host（IR interpreter と EU model）と実機で
全面的に確かめ、足りない所を足し、整数除算の lowering を Gen12 の方式に置き換えた。
- 引き取ったもの（変更なし、今回初めて検証）:
  - 行列（`compiler/spirv.c`）: OpTypeMatrix、OpMatrixTimesVector/VectorTimesMatrix/MatrixTimesMatrix/MatrixTimesScalar
    （`i915_spirv_lower_matrix_product`、和は SPIR-V の定義順＝spirv_to_nir の順）、OpTranspose（IR なし、名前の付け替え）、
    push constant と uniform block の行列の ColMajor/RowMajor と MatrixStride（`i915_spirv_chain_block`、`i915_spirv_lower_load_block`）、
    function-local の行列（列の access chain・述語付き store）、行列の OpCompositeConstruct/Extract、行列の定数。
  - 整数（spirv.c `i915_spirv_lower_integer*`、compile.c `i915_compile_integer`/`multiply`/`convert`/`integer_compare`）: IAdd/ISub/
    IMul（Mesa の 32×16 分割、`brw_lower_integer_multiplication.cpp`）、SNegate、shift 3 種、bitwise 4 種、ConvertFToS/SToF/FToU/UToF、
    OpBitcast（IR なし）、整数比較 10 種、GLSL.std.450 SAbs/SMin/SMax/UMin/UMax/SClamp/UClamp/FSign/Step/SmoothStep/Trunc/Ceil、
    OpFMod/OpFRem（`x - y * floor/trunc(x * rcp(y))`、Mesa nir_lower_fmod と同じ）。
  - ループ: OpLoopMerge を IR の LOOP_BEGIN .. LOOP_END に（ループ変数＝MOVE、`active`＝header の述語、break は merge への edge、
    continue は continue target への edge、if/else は従来どおり if-conversion）。EU は Mesa の `(+f0.0) while` 形
    （`drv_i915_eu_while`、brw_WHILE: null D destination、JIP＝WHILE からループ先頭への byte 数、Gen6+ の DO は命令なし）。
    戻らない channel は WHILE の後で待ち、全 channel が抜けた時点でループに入った channel で再開する（HALT 不使用）。
    展開はしない（動的な trip count そのまま）。
- 変更・追加:
  - **整数除算を Gen12.0 の math box に**（compile.c `i915_compile_divide`）: 前任の 32 段の筆算（1 除算 224 命令）を撤去し、
    `math intdiv`/`intmod`（UDIV/UMOD は UD、新 IR `I915_IR_IDIV`/`IREM` は D）の 1 命令にした。出典: Mesa 25.0.7 の brw_fs_nir.cpp
    （sha256 2e6116f7…20f8）が idiv/udiv を INT_QUOTIENT、umod/irem を INT_REMAINDER にし、brw_nir.c（7cb2e82e…32c2）は
    nir_lower_idiv を verx10 ≥ 125 だけで使う（Gen12.0 は math box）。selector 12/13 は brw_eu_defines.h（12a919ed…7abc）、
    source modifier 不可は brw_eu_emit.c の gfx6_math（`drv_i915_eu_math` が negate/abs の整数 source を拒否）、SIMD8 上限は
    brw_lower_simd_width.cpp。OpSDiv→IDIV、OpSRem→IREM、OpSMod は Mesa の imod と同じく「余りが 0 でなく符号が異なれば除数を足す」。
    gentool（brw_disasm --gen=adl）が `math intdiv(8) g63<1>D g16<8,8,1>D g17<8,8,1>D` と読む。
  - OpOuterProduct（`i915_spirv_lower_outer_product`）、GLSL Round/RoundEven（新 IR `I915_IR_FROUND_EVEN` → `rnde`、Gen12 opcode 70、
    brw_eu.c。Mesa も Round を fround_even にする）、SSign（比較 2 回と select）。
  - output の読み戻し（`sum += ...` のように out 変数を読む GLSL）: local と同じく最後に store した scalar を返す。
  - 16 入出力: compiler の上限は既に 16/16（`I915_SHADER_MAX_INPUTS`）。binary に `varying_locations[]` を足した。
    executor（`render/pipeline-prepare.c`、`state.c`、`state.h`）: fragment shader が vertex の varying の**一部だけ・任意順で**読む
    pipeline を受理（`kernels->ps_input_slots[]`: 入力 n ← その location の VUE slot。`3DSTATE_SBE` は vertex の全 slot を読み、属性数は
    fragment の入力数、`3DSTATE_SBE_SWIZ` で route。書かれない location を読む pipeline は ENOTSUP と log）。入力を読まない fragment
    shader は Mesa と同じく属性 0・PS_EXTRA attribute enable 0 に（E1 では varying 数を出していた）。
  - **VS の URB entry を入力と出力の大きい方に**（`render/draw.c`）: vertex fetcher が属性を書く entry を VS が VUE で上書きするので、
    entry は max(属性数, 2 + varying) slot（Mesa brw_compile_vs.cpp の urb_entry_size、sha256 750f079b…4bf6）。従来は出力側だけで、
    16 属性（256 byte）が 1 unit（64 byte）の entry からあふれ、隣の頂点を壊していた（下記 VIN16 の 1 回目）。
- spill の判断: **scratch への spill は実装せず、明示的に拒否**（`i915_compile_define` の XXX に理由）。値 register（r16..r95、VS は
  VUE staging の手前まで）が足りない shader は compile が ENOTSUP、pipeline 作成は失敗して kernel なし。spill には 3DSTATE_VS/PS の
  scratch 欄と per-draw の scratch buffer、Mesa brw_spill_reg() の stateless dataport message が要り、executor にまだ無い。
  VS は payload（属性 4 register ずつ）と VUE staging（slot 4 register ずつ）が別領域なので、16 属性 ＋ 16 varying は値 register が
  0 になり拒否される（12 ＋ 12 は通る、host 試験）。
- 実機 `vkloop-hw.sh test vke2`（`BUILD=build/p014-e2`、`tests/render/generality.c`、shader は `tests/render/generality-shaders/`＋
  `regenerate.py` → `tests/fixtures/generality-shaders-gen.inc`）**8/8 PASS**（2 回目。1 回目は VIN16 だけ FAIL → 上記 URB entry 修正）。
  各 fragment shader は 64x64 RGBA8 に 1 画素 1 語（32 bit）を書き、regenerate.py が SPIR-V の定義どおり（整数は 2^32 剰余、
  浮動小数は compiler の lowering 順の IEEE 単精度）に計算した語と比べる。
  - VKE2-MATRIX: vertex は uniform block の row-major turn の transpose × column-major scale で四隅を置く（layout を誤ると座標が回る）。
    fragment は std140 block の column-major・row-major・mat3（MatrixStride 16）と push constant の row-major・column-major、local 行列
    への列 store と条件付き置換、transpose、outer product、行列×scalar、vector×行列、mat2 の構築。値は 0.25 刻みで全結果が厳密、
    ±0 同一視で 4096 語一致。
  - VKE2-INT: 画素座標の整数 hash から負数・32 bit 全域・小さい値・負の値の 4 群、除数は 2 の冪（1..32768）と非冪（3, 40, …）と
    その負、大きな奇数。add/sub/mul（wrap）、SDiv/SMod/UDiv/UMod、shift 3 種、bitwise、negate/abs/sign/min/max/clamp、unsigned 版、
    比較 10 種を bit に、U→F（大きな値の丸め）、S→F、F→S/F→U、混合。4096 語 bit 一致。
  - VKE2-FLOAT: mod（正・負の除数、商が整数から 0.005 以上離れる入力）、roundEven/round（.5 は偶数へ）、trunc、ceil、sign、step、
    floor、smoothstep、fract、abs。許容 4 ulp（smoothstep の rcp）で一致。
  - VKE2-LOOP: `x % 17` 回の for（continue と break）、for の中の while（if/else と break）、do-while（Collatz、上限 40）、while (true)
    と break、float の累積。画素ごとに trip count が違い、host EU model では dispatch 内で channel が割れる WHILE が 10030 回。bit 一致。
  - VKE2-VARY16（16 varying を全部読む）、VKE2-SUBSET（16 中 location 0,1,6,11,15 を宣言順を崩して読む）、VKE2-VIN16（16 頂点属性、
    stride 256）: 画素ごとの `x*1000 + y*100000 + 定数` の bit 一致。
  - VKE2-SPILL: 96 個の float を同時に生かす spill.frag で pipeline 作成が ENOTSUP（kernel なし）。
  - 同じ run の vkdemo offscreen は `7523debe…05ff`（参考、回帰の代わりではない）。kernel の大きさ: matrix.frag 10512 byte（r65 まで）、
    vary16.vert 6160 byte、vin16.vert 4320 byte（r77 まで）。
- host: `run-vk-host-tests.sh` 全 PASS、`run-vk-gentool-test.sh`（BRW_TOOLS=Mesa 25.0.7）PASS、analyzer 0 件。追加分は
  `plan/ws031/tests/README-vk-host-tests.md` の「段階 E2」: lower（interpreter に IDIV/IREM/FROUND_EVEN、4 shader × 4096 画素が
  regenerate.py と bit 一致、interface shader、手組みの SRem/SMod/FRem/FMod）、compile（EU model に整数・変換・RNDZ/RNDE・INT DIV・
  WHILE の channel 待ち、4 shader × 4096 画素 bit 一致、spill と 16+16 の拒否）、pipe（subset の SBE/SBE_SWIZ route、書かれない
  location の拒否、16 属性）、gentool（intdiv/intmod D/UD、rnde、generality の全 kernel）。
- 後回し（下の一覧に追記）: 下記「E2 の残り」。

### E3 完了（2026-09-23）: register spill（scratch）・16 入力 + 16 出力・mview の per-pixel 光源・shader 試験の索引
- **spill（compiler、`compiler/compile.c`）**: 値 register が足りない shader を ENOTSUP で拒否するのをやめ、scratch memory へ spill する。
  方式は Mesa brw_spill_reg() の「spill した値は register を持たない」形（spill everywhere）: 定義のたびに temporary から scratch へ書き、
  読む命令の直前に temporary へ読み戻す。割当ては従来の 1 pass（定義時に取り、最後の使用で返す）のまま、register が尽きたら
  その時点で「この命令が読み書きしない値のうち生存区間が最も遠くまで続くもの」を victim に選び、victim を scratch に移して shader 全体を
  lower し直す（尽きなくなるまで繰り返す。victim が無ければ ENOTSUP）。ループとの整合は、spill した値に register が無いので自明
  （WHILE で先頭へ戻っても register の対応は変わらない）。
  - message は Gen12.0（verx10 < 125、LSC 前）の Mesa と同じ（出典は `intel/eu-encoding-gen12.h`: Mesa 25.0.7 brw_reg_allocate.cpp
    sha256 6ba5ce7a…97e8 の emit_spill()/emit_unspill()、brw_generator.cpp 0c3f99af…3b の generate_scratch_header()、brw_eu.h 87a58fd1…fc1b の
    brw_message_desc()/brw_dp_desc()、brw_eu_defines.h の SFID・message type・BTI）: header 1 register（最初に NoMask で 0 clear、
    dword 3 ← r0.3[3:0]、dword 5 ← r0.5[31:10]、以後は dword 2 の OWord offset だけを SIMD1 NoMask の MOV で書換え）、data cache
    （SFID 10）の stateless non-coherent（BTI 253）OWord block write（2 OWord = 1 register、data は第 2 payload、execution mask 下 =
    Mesa の per_channel spill）と OWord block read（NoMask）。desc は write 0x020a02fd（ex_mlen 1）、read 0x021802fd。
    brw_disasm が `DC OWORD block write/read, bti 253, owords = 2` と読む。Mesa は message ごとに header を作り直すが、ここでは 1 回だけ作る
    （dword 3/5 は変わらないため。形式は同じ）。
  - header は値 register の最上位（FS は r95、値は r94 まで）。値 n の slot は byte 32 n、per-thread scratch = slot × 32 を 1 KiB 以上の
    2 の冪に切上げ（最大 2 MiB）、`binary->scratch_bytes`。
  - EU encoder（`compiler/eu.c`）: `drv_i915_eu_mov_all`（SIMD8 NoMask）、`drv_i915_eu_mov_scalar`・`drv_i915_eu_alu2_scalar`（SIMD1 NoMask、
    subregister へ 1 dword）、`drv_i915_eu_send_all`（NoMask の SEND）。内部は alu2/send の共通関数に scope 引数を足しただけで、
    既存の emitter の出力は不変。
- **16 入力 + 16 出力（VS）**: payload（16 attribute = r2..r65）と staged VUE（18 slot = r55..r126）が重なる VS は、まず
  「gathered VUE」に切替えて lower し直す: 各 output component が program 順で最後に store した値を shader の最後まで生かし
  （`i915_compile_keep_outputs`）、終端で 2 slot ずつ r119..r126 に集めて URB write（最後の write は r127 に handle、EOT）。
  store が述語付き・ループ内でも、全 channel が全 store を通る（述語が偽の channel は前の値を store する）ので最後の store の値が答え。
  それでも足りなければ上の spill。staged VUE で収まる VS（vkdemo・mview・vary16/vin16/matrix.vert など）は従来と同じ code（vkdemo・mview・
  compiler/feature/generality/executor の既存 36 kernel が E3 前の compiler と byte 一致、host で確認）。
- **executor（`render/`）**: pipeline の kernels に各 stage の per-thread scratch（`vs_scratch_bytes`/`ps_scratch_bytes`）。draw が
  session の scratch buffer（1 object: 先頭 1 page は未使用、VS 部 = per-thread × 546 thread id、PS 部 = per-thread × 1024 × slice 数、
  各 page 境界。thread id の数は Mesa intel_device_info.c 1a3c7c6d…afd8 の init_max_scratch_ids(): max_vs_threads と Gen12 の
  max_wm_threads = 128 × 8 × slice）を必要時に作り、足りなくなれば batch を流してから作り直す（大きくなる一方）。
  `3DSTATE_VS`/`3DSTATE_PS` dword 4-5 に Scratch Space Base Pointer（bit 63:10）と Per-Thread Scratch Space（1 KiB << n、anv の
  get_scratch_space() と同じ n）。genxml は gen110.xml（VS、6598e556…e35d）・gen120.xml（PS）。
  - **General State Base Address = scratch buffer**: 1 回目の実機（SPILL・VIO16 FAIL）で、session の VA が 4 GiB 以上（0x1_0080_0000）に
    あり、GSBA 0・size 4 GiB では scratch pointer（GSBA 相対）が届かないことが判明（anv は scratch BO を 4 GiB 未満に置き GSBA 0）。
    spill する kernel の draw だけ STATE_BASE_ADDRESS の General State Base を scratch buffer に置き、pointer は buffer 内の offset にした
    （stateless message の address も r0.5 の pointer も GSBA 相対なので同じ扱い）。spill しない draw と rectangle は従来どおり GSBA 0。
  - 制約: object 1 つ 16 MiB まで（`I915_MAX_RESOURCE_BYTES`）なので per-thread は各 stage 8 KiB 程度まで（超えると draw が失敗し log）。
- **実機 `vkloop-hw.sh test vke2`（`BUILD=build/p014-e3`）9/9 PASS**（2 回目。1 回目は上の GSBA の問題で SPILL・VIO16 が
  「XXX … beyond the general state」で FAIL）:
  - VKE2-SPILL: spill.frag を作り直し（96 値を同時に生かしたまま、画素ごとに trip count 0..4 の loop が loop 変数 2 つを更新、最後に 96 積の和の
    bit を出力）。kernel 16976 byte（r95 まで、scratch 2048 byte/thread）、64x64 全画素 bit 一致。host EU model では scratch 書込み 30720 回の
    うち loop で channel が止まった状態の書込み 4096 回、読出し 51712 回。
  - VKE2-VIO16（新）: vio16.vert（16 attribute、16 varying、gathered VUE + scratch 2048 byte/thread、kernel 8976 byte）+ vary16.frag で
    全画素 bit 一致。VIO16 で scratch buffer が VS 部を足して作り直される経路も通った（log `i915: vk: scratch: …`）。
  - 他の 7 step（MATRIX・INT・FLOAT・LOOP・VARY16・SUBSET・VIN16）も PASS、kernel の大きさは E2 と同じ。
- **host**: `run-vk-host-tests.sh` 全 PASS、`run-vk-gentool-test.sh`（BRW_TOOLS=Mesa 25.0.7）PASS、analyzer 0 件。追加分は
  `plan/ws031/tests/README-vk-host-tests.md` の「段階 E3」（EU model の scratch memory・URB 記録、spill.frag と vio16.vert の実行、
  手組み IR の 16 + 16、pipe の VS/PS dword 4-5 と GSBA）。
- **mview `--shading=pixel`**（`userland/base/mview/`）: `shaders/pixel.vert`・`pixel.frag`・`pixel-cutout.frag`（glslc、`regenerate.py` と
  `provenance.json` を更新、既存 3 本の SPIR-V は byte 不変）。uniform buffer（binding 1、`struct mview_scene` std140 416 byte: model・view・
  projection・normal matrix、ambient、光源 3 つ）、fragment で 3 光源のループ（directional 1 + point 2、定数/1 次/2 次の減衰）の
  Blinn-Phong、texture × material 色、cutout は discard。既定は従来の per-vertex（p013 の参照画像はそのまま有効）。camera.c は
  push と scene が同じ `camera_frame()` を使うよう整理し、per-vertex の push が変更前と bit 一致であることを host で 2000 視点確認。
  mview host 試験に scene の検査（projection·view·model = clip、R で scene が bit 一致に戻る、std140 416 byte）を追加（98 checks PASS）。
- 試験環境: `vkloop-hw.sh` は `MVIEW_ARGS` に `--shading=pixel`（または `MVIEW_NO_VENUS=1`）があると Venus 参照を渡さず
  `i915-capture.py --no-venus`（*_like_venus を skip）。capture harness は mview の 6 画像を 1 枚の `sheet.png` にする。
- **実機 mview per-pixel（capture display）**: `CAPTURE=mview MVIEW_ARGS="--shading=pixel" vkloop-hw.sh mview` で 6 検査すべて真
  （model_visible・drag_rotates・right_drag_pans・wheel_zooms・keys_turn・reset_restores_first_frame、*_like_venus は skip）。
  1 回目で PASS。sheet は centris `/tmp/mview-pixel-sheet.png`（`/tmp/capture-last/sheet.png` の写し）。i915 で pixel.vert 3584 byte、
  pixel.frag 6256 byte、pixel-cutout.frag 6352 byte（spill なし、r60/r52 まで）。
- **fps（実 LCD、`MVIEW_ARGS="--spin=30 --shading=pixel"`、30 s）**: vsync build 59.07 fps（1774 frame、frame 16.92 ms、display ended PASS）、
  no-vsync build（`"mview -DI915_PRESENT_NO_VSYNC=1"`）129.61 fps（3890 frame、frame 7.71 ms: submit 5.45 ms、display ended PASS）。
  per-vertex の no-vsync 190 fps（性能 第 2 回）より submit が重い（UBO の push data copy と per-pixel の fragment）。
- 索引: `src/drivers/gpu/i915/tests/render/README.md`（suite ごとの feature 対応表、期待値の作り方、SPIR-V の再生成、未対応機能）。
- 後回し（下の一覧に追記）: 「E3 の残り」。

## 後回しの確認・強化（ユーザー指示 2026-09-22: 正常系の疎通を優先、以下は最後に 1 回）
（2026-09-23 ユーザー決定: 以下を Phase に分けて計画だけ作り、実行は後回し。確認と小修正は
[p015](../phase015/phase.md)、executor の未実装機能は [p016](../phase016/phase.md)、compiler の未実装機能は
[p017](../phase017/phase.md)、性能の構造改善は [p018](../phase018/phase.md)。統合回帰は p015 の最後に 1 回。）
- 回帰は最後に 1 回: offscreen `7523debe…`、display ended PASS（写真）、wltest、ktest、eu/draw/tex/t3/bl、vkx、vkc、CAPTURE=vkdemo。
  （vkx は B 完了時に 8/8 PASS、以後は回帰に含める。）
  （t3・bl は C の後まだ未実行。ktest は A 後 383/0/12、1 件 skip→pass の中身未確認。）
- B の残り（B 完了時点、2026-09-22）: mip level 0 以外への描画（draw・attachment clear は level 0 のみ、XXX で拒否）、
  view の format 読替え・swizzle 未適用、image create flags（MUTABLE_FORMAT 等）と input/transient attachment の usage は format 特性で
  未照合、mirrored blit は ENOTSUP、anisotropy・compare・border colour・unnormalized 座標は sampler で無視。
  準正常系の確認: 幅 32 texel 未満の mip 付き image（level 2 以降の base address が 64 byte 境界でない）を描画先・blit 先にする場合、
  非正方・奇数寸法（5×3 など、host 試験のみ）の実機 sampling、16384 級の大 image の 15 level、max_lod < min_lod の sampler、
  LOD の端数（自然 LOD 1.5 など 8 px 格子で作れない縮小率）。
- E1 の残り（2026-09-22）: blend は attachment 0 だけ（複数 colour attachment・logic op・dual source（SRC1_*、今は blend off に落とす）は
  未実装、XXX で名乗る）。uniform buffer は push data 経由のため 1 stage 1 KiB まで・block 8 個まで、descriptor 配列（descriptorCount>1）・
  descriptor copy・同一 command buffer 内で GPU が書いた UBO を draw 間で読み直す順序（flush はするが op 単位の依存は未検証）、
  vertex stage の sampler（binding table なし）。fragment が varying の一部だけ読む pipeline は E2 で対応済み。
  準正常系の確認: dynamic offset の範囲外（host 試験のみ）、SRC_ALPHA_SATURATE・CONSTANT_ALPHA 系の実機確認、float target の blend、
  vkCmdSetBlendConstants なしの動的定数 draw、UBO range が block より短い場合（0 埋め、host 試験のみ）。
- E2 の残り（2026-09-23）: scratch への spill なし（拒否）。VS の payload と VUE staging が別領域のため多入力・多出力の同居に上限
  （16+16 は拒否）。整数の varying（Flat decoration は拒否）・整数の頂点属性（入力は float のみ）、16/64 bit 型。ループの外で読む
  local をループ内で初めて store する形（escape で拒否）、行列の OpPhi、local の配列・構造体、local vector の動的 index
  （OpVectorExtractDynamic・動的 access chain）、OpSwitch、関数呼出し、ループ内 return は拒否のまま。trip count 0 の channel も
  body を 1 回（述語で無効化して）通る。終わらないループは GPU hang（compiler は検出しない）。SWSB は全命令直列のまま。
  準正常系の確認: 0 除算・INT_MIN/-1 の hardware 値（SPIR-V 未定義、未確認）、INV の精度が mod の floor に効く入力（商が整数に近い、
  今回は余裕 0.005 の入力だけ）、FRem の実機、ループ内の discard・texture sample、ループの入れ子 8 超（拒否）、32 以上の shift
  （hardware は下位 5 bit、SPIR-V 未定義）、入力を読まない fragment shader の SBE 属性 0（E1 の UBO step は最後の回帰で確認）。
- E3 の残り（2026-09-23）: spill は spill-everywhere（定義ごとの書込み・使用ごとの読出し、定数の再生成なし、victim は生存区間の終端が
  最も遠いもの、cost 重み付けなし）で、spill 1 つごとに shader 全体を lower し直す（spill 数 × compile 時間）。scratch buffer は session に
  1 object（16 MiB 上限、各 stage の per-thread はおよそ 8 KiB まで）、spill する draw だけ General State Base を buffer に置く。
  VS の gathered VUE は staged で収まらない時だけ。準正常系の確認: 同じ command buffer 内で scratch buffer が作り直される順序
  （VIO16 で 1 回通過、SPILL と VIO16 を交互に描く場合は未確認）、sample の 4 reply の一部だけが spill される形・MOVE 先の loop 変数の
  spill（host の EU model のみ）、FS の discard と spill の同居、per-thread 4 KiB 超（slot 128 超）の実機、spill した VS と PS の同時実行
  の実機負荷（多数 thread）、scratch buffer 作成失敗時の draw の失敗経路。mview `--shading=pixel` は Venus 未確認（要求外）、
  カメラでの LCD 写真は未撮影（fps 計測のみ）。
- 準正常系: uint8 index（XXX で拒否）、4 byte 非整列の CopyBuffer（ENOTSUP）、viewport/scissor index>0、負の viewport 高さ、
  blend material を持つ model、HALT による discard 早期終了、`R` 以外の視点の再現性。
- 異常系: 範囲外 index/offset、command buffer 65536 操作超、descriptor 上限、shader compile 失敗時の pipeline 解放（既知 XXX）、
  PS/2 keyboard の key が zwl に届かない件（p013）、QMP `input-send-event` の `device` 指定で QEMU abort。

## 性能（途中で停止、2026-09-22）
mview `--spin=30`（640×480、16 draw/frame、37k 三角形、実 LCD）。kernel の `i915: perf:` 行で内訳を記録。
| 段階 | fps（30 s 平均） | 内訳 |
| --- | --- | --- |
| 基準（変更前） | 14.87（計測コード入り 14.37） | submit 1 回 28.6 ms（build 0.28 / run 28.3 / GPU 3.8）、約 9.5 batch/submit、2 submit/frame、present 8.8 ms（copy 1.3 / publish 2.4 / flip 4.0） |
| command buffer ごとの 1 batch＋静的資源の再利用＋per-frame 検査の撤去 | 45.53 | submit 1.7 ms（GPU 1.2）、1 batch/submit、present 3.5 ms |
| ＋vsync を待たない（`-DI915_PRESENT_NO_VSYNC=1`） | 46.08（定常 約 48） | — |
- 未解決: no-vsync の run で表示停止が `ended FAIL`（調査前に停止）。起動直後の 1.9 s の frame は残る。
- 未実施: vkdemo offscreen hash と `CAPTURE=mview` の確認、変更の整理。途中の diff は centris `/tmp/perf-pass-stopped.diff`。

### 性能 第 2 回（2026-09-22、Fable で実施）: 14.9 fps → 190 fps（no-vsync）/ 60.4 fps（vsync、60 Hz 上限）
計測は mview `--spin=30`（640×480、16 draw/frame、37k 三角形）。mview の `MVIEW STAGES`（rdtsc、TSC 2.496 GHz）と kernel の `i915: perf:` で内訳。

| 段階 | fps（no-vsync） | 1 frame | 主因 |
| --- | --- | --- | --- |
| 前回の終了時 | 46 | 21.7 ms | — |
| libvulkan: `vkResetFences` ごとの notification 台帳の reap を止める（`sync_notifications_reap_if`、8 件未満は問い合わせない） | **155** | 6.45 ms | reap の ioctl 1 回が平均 4.8 ms（後述の scheduler の性質）× 2 reset/frame × 数件 = 13 ms/frame |
| kernel: marker request（fence 用の空 request）を GPU に送らず即完了 | 162 | 6.18 ms | worker は 1 request ずつ完走させるので marker は到達時点で完了 |
| kernel: flip を worker では arm だけにし、FIFO の待ちは present した thread で行う | 166 / **58.5（vsync）** | 6.0 / 17.1 ms |
| 計測用の一時 diagnostic を除去（最終） | **190 / 60.4（vsync）** | 5.25 / 16.6 ms | vsync 時に worker が vblank 待ちで塞がり render が後ろに並んでいた（submit 14.8 → 4.2 ms） |

そのほか: kernel tick を 1 kHz に（`KERN_CLOCK_HZ`、LAPIC 周期は 10 ms 校正窓を HZ で割る、i915 の raw tick 定数 5 か所を HZ 基準に）。
起動直後の最大 frame 時間 1.9 s → 0.63 s。fps への直接効果は無し。`wayland_wait` の nanosleep(1 ms) を socket の poll に（tick 量子化の除去、効果は小）。
zwl は present 後に client を flush（callback が次の pass まで残らない）。停止経路: buffer A へ戻す flip も settle（`ended PASS` に復帰）。

**見つかった構造（専門家向け）**
1. **scheduler**: thread は CPU に固定、quantum 5 tick、同一 CPU への wakeup は `need_resched` を立てるだけで、running thread は
   次の `kern_preempt_enable`（ほぼ全ての syscall 内）で yield する。i915 worker は CSB を udelay(50 µs) で busy-poll するので
   GPU 実行中（1–3 ms）はその CPU を占有し、同一 CPU に woken された thread は 1–5 ms 待つ（`SCHED_WAKE_LATENCY=1` の
   計測: same-cpu 455 件/5 s が 1–5 ms、cross-cpu 平均 32 µs）。この性質のため「非 block の ioctl」が平均 5 ms かかっていた。
   → worker の完了待ちを IRQ（user interrupt）+ waitq に、wakeup 時の preempt 方針の見直しが本筋。
2. **同期 executor**: 1 frame に GPU round trip が 3 回直列（render 1.8 ms、libvulkan の present job の `vkCmdCopyImage` 1.8 ms、
   zwl の拡大 copy 3.5 ms）で、CPU は各回完了を待つ。非同期 submit（queue して即返し、fence は完了時に signal、state/batch heap の
   多重化）にすれば frame ≈ max(CPU, GPU) ≈ 3 ms 台（300 fps 前後）が見込める。
3. **frame の copy が 2 回**: libvulkan の WSI は app の swapchain image（OPTIMAL）から export 用 shared image（LINEAR）へ毎 frame
   copy する（WS014 の設計）。i915 では両者 linear で同一 layout なので aliasing で 1 回分（≈1.8 ms）消せるが、Venus と共通の
   libvulkan では driver 依存になるため未実施。zwl 側の 640×480→1920×1080 拡大 copy（1.58 ms GPU）は plane scaler か
   zero-copy flip で消せる。
4. GPU 自体の render は 1.3 ms/frame（37k 三角形、640×480）で、まだ fixed overhead 支配。三角形数から見た上限はこれより 10 倍上。

**未解決・後回し**
- `I915_PRESENT_NO_VSYNC` は build option のまま（本来は present mode = MAILBOX/IMMEDIATE で選ぶべき）。
- 今回の変更の回帰（vkx/vkc/ktest/wltest/Venus 側 libvulkan）は最後の 1 回にまとめる。libvulkan の reap 変更は Venus にも効く。
- i915-capture.py: frame の到着は log 行でなく capture 領域の write_count で判定するよう修正済み（serial の混線対策）。
