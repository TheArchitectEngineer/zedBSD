<!-- awesome-plan project=zedbsd record=ws075p001 -->

# ws075-p001: 調査（i915 で今のデスクトップとグラフィックスに足りないもの）

Phase ID: `ws075-p001`
Parent: [WS075](../ws.md)
Status: cleared（2026-09-27。host の調査と実機の zdesktop の capture PASS）
Phase disposition: normal
承認: 2026-09-27 ユーザー「OpenGL 3.2が問題なければ、それ以降のOpenGLはいったん保留して、i915の高度化に進んでください。」、
main の WS075 の登録（2026-09-27）。試験は amd64 のみ、phase の最後に。

## 範囲

1. shader: i915 の compiler（`src/drivers/gpu/i915/compiler/spirv.c` の parser と lowering）が受けないものを、**最初の拒否で止めずに**
   全て数える。対象は今の client の shader（zdesktop・files・terminal・mview・vkdemo）、libGL の固定機能、
   libGLESv2 の GLSL compiler が作る shader（GLSL の host 試験の pass/ の組と、egltest・glxtest の場面の shader）。
2. 実行器: client（zdesktop とその client、libEGL・libGLESv2 を通る GL の client）が使う Vulkan の command と、
   実行器（`src/drivers/gpu/i915/render/`）が受ける command の突き合わせ（静的）。
3. 実機の出発点: 今の zdesktop を実機の capture で 1 回撮る（`CAPTURE=zdesktop plan/ws031/tests/vkloop-hw.sh zdesktop`、
   `flock /tmp/i915-hw.lock`）。何が描け、何が落ちるか。
4. 結果: 不足の一覧と WS075 の p002〜p008 への割り当て（ws.md の Phase の表を直す）。

範囲外: 不足を直すこと（p002 から）。zdesktop・client の source の変更。

## 設計

- `plan/ws075/tests/shader-survey/survey.py`: SPIR-V を `spirv-dis --raw-id` で読み、i915 の parser の規則（下）を写した表で
  拒まれるものを全て数える。規則の正本は i915 の `compiler/spirv.c` で、表の各行に元の関数名を書く。写しの誤りは、各 shader の
  最初の拒否を i915 の compiler そのもの（`plan/ws068/tests/i915-shader-check` を host で）と比べて確かめる（survey の最初の不足が
  i915-shader-check の拒否と同じ opcode であること）。
  - decoration: Location・Binding・DescriptorSet・BuiltIn・ArrayStride・Block・RelaxedPrecision だけ受ける
    （`i915_spirv_declare_decoration`）。member decoration: Offset・BuiltIn・MatrixStride・RowMajor・ColMajor・RelaxedPrecision
    （`i915_spirv_declare_member_decoration`）。
  - module の変数: Input・Output・PushConstant・UniformConstant・Uniform、初期化子なし（`i915_spirv_declare_variable`）。
    関数の中の変数は lowering の規則による。
  - 定数の composite: 2〜4 個の vector・matrix だけ（struct・配列は無い）。
  - 関数は 1 つ（呼出し無し）、OpSwitch 無し。
  - 命令: `i915_spirv_lower_instruction` の case の一覧（算術・比較・論理・変換・行列・composite・load/store/access chain・
    GLSL.std.450 の一部・`OpImageSampleImplicitLod` だけ）。
  - GLSL.std.450: `i915_spirv_lower_extended` の case の一覧。
  - builtin: `compile.c`・`spirv.c` の受ける BuiltIn の一覧。
- `plan/ws075/tests/shader-survey/run.sh`: 対象の SPIR-V（client のものは build と同じ flag か checked-in の SPIR-V） を集めて survey.py に渡し、`build/ws075-p001/` に shader ごとの不足と、
  不足ごとの shader の数の表を書く。egltest・glxtest の shader は C の source の文字列の定数から取り出し、同じ file の
  vertex・fragment（・geometry）を GLSL の host 試験の `glsl-test link` で組にして link する（組にならないものは外して数える）。
- 実行器の command: `plan/ws075/tests/vk-calls.py` が client の source（と libGLESv2・libEGL）の `vk*` の呼出しを数え、
  実行器の `render/command.c`・`objects.c` 等が受ける command（libvulkan の opcode の表 `vulkan-codec.inc` を通して）と比べる。
  静的なので、作成時の parameter（format、sample 数、pipeline の stage）の不足は数えない。これは実機の capture と p002 以降の
  実機の run で見る。

## 判断が要る点（既定を選んで進める）

（なし）

## 結果（2026-09-27）

### 1. 実機の出発点（i915、capture。QEMU の Venus ではない）

`plan/ws075/tests/capture-hw.sh zdesktop zdesktop build/ws075-p001/hw-zdesktop`（`CAPTURE=zdesktop plan/ws031/tests/vkloop-hw.sh zdesktop`
を `flock /tmp/i915-hw.lock` の下で）: **status pass、6 検査全て PASS**（desktop_drawn、dock_changes_view、wiseview_changes_view、
wiseview_closes、close_ends_viewer、desktop_after_close）。今の main の zdesktop（glass の title bar、backdrop のぼかし
`ZWL BACKDROP ready width=240 height=135`、Wiseview、docking）と mview・wl_shm の窓が実機の実行器で描けた。executor の拒否・
`vkCreateGraphicsPipelines failed` の行は無い。画像 `build/ws075-p001/hw-zdesktop/capture/sheet.png`
（写し `/home/awe/zedBSD-rpi4/build/ws031-shots/ws075-p001-20260927-zdesktop-hw.png`）。

**訂正**: 提案の時（WS031 の節）に「zdesktop の panel.frag は OpFunctionCall で拒まれる」と書いたのは、glslc を `-O` 無しで
走らせた誤り。client は `regenerate.py` で `glslc -O`（関数を inline 化）で作るので、実際の SPIR-V は通る（下の 2 の検査も
build の flag で作る形に直した）。

### 2. shader（host、`plan/ws075/tests/shader-survey/run.sh`、出力 `build/ws075-p001/shaders/`）

対象 122 module: client の shader（build と同じ flag か checked-in の SPIR-V）16、GLSL の host 試験の program と libGL の固定機能
（`build/ws068-glsl-host`）、egltest・glxtest の場面から取り出して zedBSD の GLSL compiler で link した program。
**i915 の compiler と survey の最初の不足が全 module で一致**（compiler が通す module は survey も不足 0、拒む module は
survey が同じ opcode の不足を最初に出す。`first.txt`）。

- **client（zdesktop・files・terminal・mview・vkdemo）の shader は全て通る。**
- 不足のある module 43（全て libGLESv2 の生成する GL/GLES の shader）。不足ごとの module の数（`gaps.txt`）:

| 不足 | module | 種類 |
| --- | --- | --- |
| decoration Flat（member の Flat 4 を含む） | 18 | 補間 |
| NoPerspective・Centroid（member を含む） | 6 | 補間 |
| OpImageFetch・OpImage（texelFetch）、OpImageQuerySize(Lod)（textureSize） | 9・7・9 | texture |
| input builtin VertexIndex・InstanceIndex | 6・6 | builtin |
| OpImageSampleDrefImplicitLod（shadow） | 5 | texture |
| texture() of an integer sampler・sampler2DArray・samplerCube・sampler3D | 4・4・2・2 | texture |
| geometry（execution model、EmitVertex・EndPrimitive） | 4 | stage |
| input builtin FragCoord 3・PrimitiveId 2・PointCoord 1・FrontFacing 1、output builtin Layer 2・PrimitiveId 1・PointSize 1 | | builtin |
| colour outputs past location 0（MRT） | 3 | output |
| OpImageSampleExplicitLod（textureLod）、texture() の ConstOffset・Bias | 2・2・1 | texture |
| local の配列・struct、配列の定数 | 2・1・2 | 変数 |
| GLSL.std.450 Determinant・MatrixInverse・PackHalf2x16・UnpackHalf2x16、OpFwidth | 2・1・1・1・1 | 算術 |

survey が写さない形の規則（operand の大きさ、入れ子の深さ、動的 index、phi）は、全 module で compiler の最初の拒否と一致したので、
この corpus では最初の不足の後ろに隠れている分だけが数えられていない（直す Phase で host の i915-shader-check を回して確かめる）。

### 3. 実行器の command（静的、`plan/ws075/tests/vk-calls.py`、出力 `build/ws075-p001/vk-calls.txt`）

client（zdesktop・files・terminal・mview・vkdemo・wltest・xserver・libEGL）の Vulkan の command は
**全て実行器が受ける**。libGLESv2 だけが受けられない command を使う: query（vkCreateQueryPool・vkCmdBeginQuery・vkCmdEndQuery・
vkCmdResetQueryPool・vkGetQueryPoolResults・vkDestroyQueryPool。sync の module が未移植）、buffer view（vkCreateBufferView・
vkDestroyBufferView、texel buffer）、vkCmdResolveImage（multisample）、vkCmdClearDepthStencilImage。

作成時の parameter の不足（静的には数えない。実行器の `XXX` の記録から）: **primitive topology は triangle list だけ**
（`render/state.c`。strip・fan・line・point は拒否。GL の app の大半が使う）、image は 2D・1 layer・1 sample だけ
（cube・配列・3D・multisample は拒否）、colour attachment は 1 つ、mip level 0 以外への描画、depth の image の copy、
buffer の descriptor（storage buffer: transform feedback の VS の store）、logic op、幅 1 以外の線、geometry の stage は走らない。

### 4. Phase への割り当て（ws.md の表を直した）

desktop は今の実機で動くので、p002 は新しい desktop の機能（tab、System Menu、files・terminal）の実機の確認に縮め、
GL/GLES の不足を先に数の多い順に並べた: p003 topology（全 GL の app）→ p004 GLES 2 の compiler（補間、builtin、texture の
bias・lod・offset、local の配列、算術）→ p005 texture の種類（texelFetch・size、shadow、integer、cube・配列・3D の image と sampler、
mip の描画）→ p006 MRT・query・texel buffer・storage buffer・multisample → p007 geometry と layered（GL 3.2）→ p008 性能 →
p009 安定 → p010 照合と回帰。

## 検証

| 確認 | 結果 |
| --- | --- |
| 実機（i915、capture）の zdesktop | PASS（6/6）。`build/ws075-p001/hw-zdesktop/` |
| shader survey（host） | 122 module、不足 43、compiler の最初の拒否と全一致 |
| 実行器の command（host、静的） | client 全て受ける、libGLESv2 の 10 command が無い |
| Venus の回帰・boot test | 未実施（製品の source を変えていない。道具だけ） |
