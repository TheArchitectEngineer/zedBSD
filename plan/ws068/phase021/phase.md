<!-- awesome-plan project=zedbsd record=ws068p021 -->

# ws068-p021: GLSL の uniform block

Phase ID: `ws068-p021`
Parent: [WS068](../ws.md)（p012 を分けたもの）
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: —（2026-09-27 ユーザーの指示でサブエージェントが実行）
設計: [glsl-design.md](../glsl-design.md) §11

## 範囲

1. 構文と検査: `layout(std140) uniform Name { members } [instance];`（1.40 以上、ES 3.00）、`shared`・`packed` は std140 として扱う、
   member の `row_major`・`column_major`、instance 名の有無、両 stage の同じ名前の block の一致。
2. SPIR-V: block ごとに set 0 の binding 32 + i（両 stage を併せた block の順）の Uniform の Block、std140 の Offset、ArrayStride、
   MatrixStride、RowMajor・ColMajor。
3. libGLESv2 の反射（spirv.c）: 既定の block（binding 0）の外の Uniform の Block を記録して拒まない。link は API（GLES 3.0 の
   uniform buffer、p005・p013）が来るまで「uniform block は OpenGL ES 3.0 の API が要る」と断る。
4. 試験: lavapipe の実行で、uniform block の buffer を「byte offset o の float は o / 4」で埋め、shader が std140 の offset の値を読む。

## 受け入れ

1. glsl-host の試験が PASS（uniform block の実行の試験を含む）。
2. 新しい C の style-check 0、warning 0。既存の試験が変わらず PASS。
3. Venus と i915 実機は API の後（未実施と書く）。

## 結果（2026-09-27）

cleared。受け入れ 1〜3 を満たした。

### 実装

- check.c: `uniform Name { ... } [instance];`（1.40・ES 3.00）。instance 名が無ければ member がそれぞれの名前で block を読む
  （`GLSL_VAR_BLOCK_MEMBER`）。書けない、配列の block と sampler の member は誤り。`row_major`・`column_major`（block と member）。
- types.c: row-major の matrix（とその配列）の std140（行ごとに 16 byte）。
- link.c: block の binding は 32 から（vertex、fragment の順。同じ名前の block は一つで、中身が同じでなければ誤り）。
- emit.c: block ごとに laid-out な struct（Offset・ArrayStride・MatrixStride・RowMajor/ColMajor）の Block、set 0 の binding、
  葉ごとの load（bool は uint から）。
- libGLESv2 の反射（spirv.c）: binding 0 の外の Uniform の Block を `named_bindings` に記録。program.c の link は、それがあれば
  「uniform blocks need the OpenGL ES 3.0 API, which is not there yet」で断る（API は p005・p013）。

### 検証（host）

- `plan/ws068/tests/glsl-host/run.sh` PASS: exec の `40-uniform-blocks`（float・vec3・mat4・float の配列（動的な添字も）・vec2・
  row-major の mat2・struct の配列（動的な添字も）・bool・int の std140 の offset を、buffer の「offset o の float は o / 4」で確かめる。
  instance 名付きの block、vertex の別の block）、`41-uniform-blocks-es300`（両 stage の同じ block が一つの binding、動的な添字）、
  pass/ の `blocks330`（入れ子の struct、row-major の mat3x4 と mat2 の配列）、fail/ の 3 例（1.30、sampler の member、書き込み）。
- style-check 0、warning 0（cross）。

### 検証（QEMU・Venus、i915 実機）

- uniform block の shader を libGLESv2 で描くのは API（p005）の後（未実施）。link が断ることは code の上だけ（guest で未実施）。
- 回帰（この image）: egl-p020・egl-p019・egl-p008 PASS、`plan/ws069/tests/x11-p005.sh` PASS（turn: differs ok、ZGEARS CHECK
  failures=0、frames=300）、spirv-host PASS、boot test PASS（build/ws068-p021-boot/login.png）。i915 実機は未実施。
