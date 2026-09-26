<!-- awesome-plan project=zedbsd record=ws068p021 -->

# ws068-p021: GLSL の uniform block

Phase ID: `ws068-p021`
Parent: [WS068](../ws.md)（p012 を分けたもの）
Status: planned
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
