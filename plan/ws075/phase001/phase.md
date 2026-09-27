<!-- awesome-plan project=zedbsd record=ws075p001 -->

# ws075-p001: 調査（i915 で今のデスクトップとグラフィックスに足りないもの）

Phase ID: `ws075-p001`
Parent: [WS075](../ws.md)
Status: in-progress（2026-09-27 着手）
Phase disposition: normal
承認: 2026-09-27 ユーザー「OpenGL 3.2が問題なければ、それ以降のOpenGLはいったん保留して、i915の高度化に進んでください。」、
main の WS075 の登録（2026-09-27）。試験は amd64 のみ、phase の最後に。

## 範囲

1. shader: i915 の compiler（`src/drivers/gpu/i915/compiler/spirv.c` の parser と lowering）が受けないものを、**最初の拒否で止めずに**
   全て数える。対象は今の client の shader（zdesktop・zdesktop-files・zdesktop-terminal・mview・vkdemo）、libGL の固定機能、
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
- `plan/ws075/tests/shader-survey/run.sh`: 対象の SPIR-V を集めて survey.py に渡し、`build/ws075-p001/` に shader ごとの不足と、
  不足ごとの shader の数の表を書く。egltest・glxtest の shader は C の source の文字列の定数から取り出し、同じ file の
  vertex・fragment（・geometry）を GLSL の host 試験の `glsl-test link` で組にして link する（組にならないものは外して数える）。
- 実行器の command: `plan/ws075/tests/vk-calls.py` が client の source（と libGLESv2・libEGL）の `vk*` の呼出しを数え、
  実行器の `render/command.c`・`objects.c` 等が受ける command（libvulkan の opcode の表 `vulkan-codec.inc` を通して）と比べる。
  静的なので、作成時の parameter（format、sample 数、pipeline の stage）の不足は数えない。これは実機の capture と p002 以降の
  実機の run で見る。

## 判断が要る点（既定を選んで進める）

（なし）

## 検証

未実施。host で survey の run と、i915-shader-check との最初の拒否の一致。実機で zdesktop の capture を 1 回。
