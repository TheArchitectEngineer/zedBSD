# WS031 設計正本: i915ネイティブVulkan実行器

対象: Dell Latitude 5330 / Alder Lake-P（Gen12 Xe-LP、`8086:46a8`）。前提: WS029 i915 driver（実機動作済み）、WS030 libvulkan.so（Vulkan 1.0 core 137 + WSI）。本書は決定・アーキ・ファイル構成・段階・試験・受け入れの正本。値やテーブルはここに書かず、実装時に出典付き`.inc`へ転記する。

## 1. 目的と非目的

- 目的: libvulkanが送るVulkanコマンドを、ホスト（QEMU/Venus/Mesa ANV）に依存せず、i915 driver内でGENへ変換して実機で実行・表示する。到達点は実機ネイティブでの`/bin/vkdemo`描画とscanout。
- 非目的: 最適化、完全なVulkan適合、全shader機能、複数engine並列、GLES、Wayland全体。これらは後続または別WS。

## 2. 現状分析（実測）

- libvulkanは`/dev/gpu0`をdrv_gpu UAPIで**直接**開く（`context.c`: `open("/dev/gpu0")` → `GPU_GET_INFO`/`GPU_GET_CAPSET` → `GPU_BLOB_CREATE`/`GPU_RESOURCE_MAP`/`GPU_COMMAND_SUBMIT`）。transportの意味でのVenus依存はない。
- 送信payloadはVenus wire形式（`opcodes.h`: virglrenderer 1.1.0のop番号を再利用、147 op、`enum vulkan_opcode`）。各`vkXxx`をop＋引数へ直列化したもので、**ハードウェア非依存**。GEN ISAは含まない。
- 実行（SPIR-Vコンパイルと3Dパイプライン駆動）は現状すべてホストのMesa ANVが担う（virtio-gpu経由）。`libvulkan/pipeline.c`はshaderをコンパイルせずwireへ転送し、`venus-backend`は「Vulkan実行はエミュレートしない」と明記。
- drv_gpu UAPI（`include/drivers/gpu.h`）は**汎用のcommand-blob転送**で、capsetがプロトコルを広告する。Vulkan/Venus/i915/GENは焼き込まれていない。
- i915 driver（WS029）はGEN native stream（magic 0x31394958）でblit（`XY_SRC_COPY_BLT`/`XY_COLOR_BLT`）とstore（`MI_STORE_DWORD_IMM`）を実行。RCS0/BCS0のexeclists/LRC/request経路は動作。3Dパイプラインとshaderは未対応。display registerは触っていない（scanoutはWS029 f003で保持のみ実装）。

結論: 「Venus依存ゼロでi915で動かす」の本質は、(1) 直列化方言（Venus op）はそのまま**zedBSDのVulkanコマンドUAPI**として使い、(2) それを**デコードしてGENを駆動するnative実行器をi915 driverに新設**すること。libvulkanは無改造または最小改造（capset整合）。

## 3. アーキテクチャ層分け

```
userland:  vkdemo → libvulkan.so（Vulkanコマンドを直列化、無改造/最小改造）
             │  drv_gpu UAPI（GPU_COMMAND_SUBMIT / BLOB / capset）
kernel:    i915 native Vulkan実行器（新設。src/drivers/gpu/i915/vk/）
             ├─ decoder + object/handle table
             ├─ resource/image/sampler/descriptor モデル（GEM/GGTT/PPGTT）
             ├─ SPIR-V→GEN baseline compiler（in-kernel）
             ├─ Gen12 3D pipeline state emitter
             ├─ command buffer → GENバッチ変換
             └─ WSI/scanout（KMS modeset、display plane）
           i915 core（WS029: engine/LRC/execlists/request/GGTT/PPGTT/GEM/uncore）
```

libvulkanが要求するcapset（現状Venus 156 byte）に対し、i915が**互換capsetを広告**してlibvulkanを無改造で通すか、libvulkanに**native capset**を足すかはp001で決める。既定は「i915が必要最小のcapabilityを広告し、libvulkanは既存経路のまま」。

## 4. native実行器の構成（in-kernel、`src/drivers/gpu/i915/vk/`）

### 4.1 decoder + object table
`GPU_COMMAND_SUBMIT`で来るwireを読み、op番号でdispatchする。instance/physical device/device/queue/memory/buffer/image/image view/sampler/descriptor set (layout/pool)/pipeline (layout/cache)/shader module/command pool/command buffer/fence/semaphore/query poolのhandleを、i915側の実体（GEM object、GGTT/PPGTT mapping、state構造体）へ対応付けるtableを持つ。反射的なget系（features/properties/format properties）はi915の能力を返す。

### 4.2 resource/image/sampler/descriptor モデル
- buffer/memory: WS029のGEM object＋PPGTT bindを再利用。host-visible memoryはCPU view（`resource_map`）で提供。
- image: tiling（Y-tiled/linear）とsurface state（`RENDER_SURFACE_STATE`）を持つ。texture uploadはblit（既存`XY_SRC_COPY_BLT`）またはCPU write。
- sampler: `SAMPLER_STATE`（nearest固定から。vkdemoはnearest checker）。
- descriptor: binding table（`BINDING_TABLE_STATE`）とsurface/sampler stateのheapを構築。push constantはpush constant領域またはインライン。

### 4.3 SPIR-V→GEN baseline compiler（in-kernel）
方針: **1対1・最適化なし**。SPIR-Vの各命令を、小さなGEN EU命令列へ素朴に落とす。SIMD8。SSA値ごとにGRFレジスタを1つ割り当て（coalescing/spillは最小限、足りなければ増やす）。制御流れはvkdemoに無いので当初は不要（構造化分岐は後続）。
- 入力: SPIR-V（`OpEntryPoint`、`OpFunction`、算術（`OpFAdd/FMul/FSub`、`sin/cos`はGEN math send）、swizzle/compose、`OpLoad/OpStore`、push constant/varying/sampler access chain、`OpImageSampleImplicitLod`、出力）。
- vertex shader出力: clip座標をURBへ書く。fragment shader出力: RTへ書く（`SIMD8` PS payload、render target write message）。
- sampler: `send`（sampler message、simple sample）でtexel取得。
- 命令encoding（EU命令のbit layout、message descriptor）は公開Intel PRMとMesa（`src/intel/compiler`、MIT）を参照し、encoding表を出典付き`.inc`へ転記。レジスタ割当・命令選択のロジックは新規実装。
- 出力: GENバイナリ（kernel object）＋要求リソース（URB/threadレイアウト、binding/sampler index）。

in-kernel配置の制約（p001で確認）: kernelはfloat演算・大きなstackを避ける。compilerは整数演算主体で書き、作業メモリは`kern_calloc`から確保、再入なし。代替（userlandでコンパイルしGENを提出）は設計オプションとして記録するが、既定はユーザー指示どおりin-kernel。

### 4.4 Gen12 3D pipeline state emitter
`3DSTATE_VS`/`3DSTATE_PS`/`3DSTATE_VF`/`3DSTATE_URB_*`/`3DSTATE_PS_EXTRA`/`3DSTATE_SBE`/`3DSTATE_WM`/`3DSTATE_DEPTH_BUFFER`/`3DSTATE_PS_BLEND`/`3DSTATE_VIEWPORT_*`/binding table pointers/sampler state pointersを、pipeline作成時とdraw時に発行する。URB割当、VF（vertex fetch、`3DSTATE_VERTEX_BUFFERS/ELEMENTS`）、render target（`3DSTATE_RENDER_TARGETS`相当のsurface state）を含む。`PIPELINE_SELECT`で3Dへ切替え、RCSの`CTX_R_PWR_CLK_STATE`など3D必須のcontext stateを有効化（WS029はp004で0のまま）。

### 4.5 command buffer変換とdraw実行
`vkBeginCommandBuffer`〜`vkCmdBindPipeline`/`BindVertexBuffers`/`BindDescriptorSets`/`PushConstants`/`Draw`/`vkCmdBeginRenderPass`等を、記録時にi915のバッチ（GEN 3D command列）へ変換し、`vkQueueSubmit`でWS029のRCS0 request経路（LRC/execlists/ELSQ/CSB/breadcrumb）へ投入する。render passのload/store（clear）はblitまたは3D clear。

### 4.6 同期
fence/semaphore/query/timelineをWS029のseqno/HWSP breadcrumbと`drv_gpu_complete`へ接続する。`vkQueueWaitIdle`/`vkWaitForFences`はrequest retireで満たす。

### 4.7 WSI/scanout
`VK_KHR_display`/`swapchain`のimageをGGTT可視のlinear/tiled surfaceとして確保し、presentで**実scanout**する。i915にKMS/modeset（display plane、pipe/transcoder/PLL/DDI、EDID/mode選択）を新設する。WS029 f003のframebuffer保持work（GMADRアパーチャ→GGTTオフセット）を土台に、OS自身がモード設定して任意フレームを全画面presentする経路へ拡張する。

## 5. ライセンス方針

- Mesa（`src/intel/compiler`、`src/intel/genxml`等、MIT系）とIntel公開PRMを参照。参照ファイルは実装前にSPDX/permission noticeを機械監査（WS029の`i915-license-audit.md`と同型）。
- EU命令encoding、message descriptor、3Dパイプラインの`3DSTATE_*` layout、surface/sampler stateのbit定義は、値・テーブルとして出典（file:line/PRMセクション）・SHA・変換規則付きの`.inc`へ転記する（`gen-inc.py`拡張）。
- decoder、compilerのレジスタ割当・命令選択、pipeline構築、WSIのロジックはzedBSD規約（`coding-style.md` §14）で新規実装。
- GuC/HuC不使用でfirmware不要。必要時のみ`userland/firmware/<機種>/`。

## 6. 段階（増分）

- 増分A: 三角形1枚。texture無し、定数色FS、passthrough VS（またはvkdemo VSの座標のみ）。3Dパイプライン最小＋最小compiler＋WSI scanout。実機で単色三角形が全画面に出る。
- 増分B: texture＋depth。sampler send、depth buffer、UV補間。
- 増分C: `vkdemo`の実vertex/fragment shader（回転cuboid、checker texture）。実機で回転cuboid表示、独立oracleで画像照合。

## 7. Phase対応

p001設計/ライセンス、p002 decoder/object、p003 image/sampler/descriptor、p004 SPIR-Vパーサ/IR、p005 EUエンコーダ、p006 codegen、p007 3Dパイプライン、p008 command buffer/draw、p009同期、p010 KMS/WSI、p011 vkdemo統合（A→B→C）、p012レビュー。増分Aはp002・p004・p005・p006・p007・p008・p010を横断する最初の実機到達点。

## 8. 試験設計

- host fixture（実機不要、agent-1）: decoderのop dispatchとobject table、SPIR-Vパーサ、EUエンコーダ（既知SPIR-V→既知GENバイト列の照合）、3D state emitのdword照合、command buffer変換。WS029と同じくASan/UBSan併用。
- 実機（Latitude 5330ネイティブ）: 増分A/B/Cの描画とscanoutを目視＋独立oracle照合（vkdemoの既存ray-texture oracleを流用）。dmesgでpipeline/compile/submitのログ確認。
- build: `make -j16`でi915 config、warning 0、`vmunix check` PASS、GPUなしbuildでsymbol 0。

## 9. リスク・未決事項（p001で確定）

- in-kernel compilerの妥当性: kernelでのSPIR-V→GEN。作業メモリ量、float回避、再入なし。代替（userlandコンパイル＋GEN提出）を残すか。既定はin-kernel。
- GEN命令encodingの検証手段: 実機無しでの正しさ確認（Mesa出力との対照、PRM照合）。
- KMS/modesetの範囲: eDPパネル1枚のmode設定に限定するか。PLL/DDI/transcoderの最小実装範囲。
- SIMD幅（8固定か）、非LLC機、tiling（Y-tiled必須か）、texture uploadの経路。
- capset整合: libvulkan無改造で通すためにi915が広告すべき最小capability。
- Vulkan機能の最小subset: vkdemoが実際に使うop/機能のみを対象化する台帳を作る。

## 10. 受け入れ条件

各増分について、実機ネイティブで期待画像が全画面scanoutされ、独立oracleがGPU出力を使わずに照合を通ること。ホスト（QEMU/Venus/ANV）を一切使わないこと。HAL/UAPI不変（変更が要る場合は事前承認）。静的解析0件、規約全文確認、回帰PASS。制限（最適化なし、機能subset、単一engine等）を明記する。
