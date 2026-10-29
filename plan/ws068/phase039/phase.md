<!-- awesome-plan project=zedbsd record=ws068p039 -->

# ws068-p039: ETC2・EAC の圧縮 texture

Phase ID: `ws068-p039`
Parent: [WS068](../ws.md)
Status: cleared（2026-10-29 Q1: Venus で egl-p039 PASS（q1682-i02 P2）、p037 と同じく i915 実機は未実施のまま cleared） 2026-10-29 ユーザーの決定で WS068 の受け入れは Venus だけ（i915 実機は受け入れに含めない、[ws.md](../ws.md) の「ユーザーの決定」）。
Phase disposition: normal
依存: p004
経緯: 2026-10-05 Q1 が p004 の範囲外から新設（その時は実行しない）。2026-10-29 ユーザー「作業の手が空いてしまったら、テストなしで実装だけ
進める戦略で、残っているWSのphaseをまんべんなく進めましょう。」（Q1 の依頼 q1682-i01 の引用）で着手。試験なし、build と host の試験まで。

## 範囲

OpenGL ES 3.0 の必須の 10 format（`GL_COMPRESSED_R11_EAC`・`SIGNED_R11_EAC`・`RG11_EAC`・`SIGNED_RG11_EAC`・`RGB8_ETC2`・`SRGB8_ETC2`・
`RGB8_PUNCHTHROUGH_ALPHA1_ETC2`・`SRGB8_PUNCHTHROUGH_ALPHA1_ETC2`・`RGBA8_ETC2_EAC`・`SRGB8_ALPHA8_ETC2_EAC`）、`glCompressedTexImage2D`・
`glCompressedTexSubImage2D`・`glCompressedTexImage3D`・`glCompressedTexSubImage3D`（2D 配列）、`GL_NUM_COMPRESSED_TEXTURE_FORMATS`・
`GL_COMPRESSED_TEXTURE_FORMATS`。

## 実装（2026-10-29）

- `userland/desktop/libglesv2/etc2.c`（新、libGLESv2 と libGL の build に入れた）: 10 format の表（kept の形: RGB・RGBA は RGBA8、sRGB は
  SRGB8_ALPHA8、R11・RG11 は R16F・RG16F。level は圧縮の internal format を持つ）、`gles_compressed_format`・`gles_compressed_size`
  （4x4 の block、8 か 16 byte）・`gles_compressed_formats`・`gles_compressed_decode`。ETC1 の individual・differential、ETC2 の T・H・planar
  （differential の赤・緑・青の overflow で選ぶ）、punch-through（bit 33 が opaque、不透明でない block は index 2 が透明な黒・index 0 と 2 の
  modifier 0、planar は常に不透明）、EAC の 8 bit alpha と 11 bit の unsigned・signed（multiplier 0 は modifier だけ、signed の base -128 は
  -127）。出典: Khronos Data Format Specification の ETC2 の節（OpenGL ES 3.0 の annex C）、bit の取り出しは Mesa（MIT）の
  texcompress_etc の読み方と同じ。
- texture.c: 4 つの call の stub を実装に（format の検査 `GL_INVALID_ENUM`、imageSize の照合 `GL_INVALID_VALUE`、pixel unpack buffer の
  offset と範囲、NULL は 0 の texel、sub image は block の角と端の規則・同じ format（`GL_INVALID_OPERATION`）、3D texture は `GL_INVALID_OPERATION`、
  rectangle texture は `GL_INVALID_ENUM`）。
- gles.c: `GL_NUM_COMPRESSED_TEXTURE_FORMATS` 10 と一覧。format.c: `gles_half_from_float`（R11 の half）。
- device の `textureCompressionETC2` があっても CPU で展開する（一つの道。i915 の Gen12 は hardware に無い）。

## 判断（推しで進めた）

1. 11 bit の channel は R16F・RG16F に（Vulkan の必須の filter できる format。R16_UNORM は必須でない）。half の誤差は 11 bit の値の 1/4096 未満。
2. `glGenerateMipmap` などの圧縮 texture への誤りの検査は足していない（kept の形で動く）。

## 検証

- build（2026-10-29）: amd64・arm64・riscv64 の libGL・libGLESv2・libEGL → exit 0、warning 0。
- host: `sh plan/ws068/tests/etc2-host/run.sh build/etc2-host` → `etc2-host: PASS`（`reference.py`: 仕様の表から書いた独立の参照で、
  64-bit の整数の bit の範囲で読む。7 format（sRGB は同じ展開）×64 block の乱数、T・H・planar・differential を強制した block と
  punch-through の透明を含む、全 texel が一致。ASan・UBSan）。参照も同じ人の読みなので、仕様の誤読は両方に入りうる（未確かめ）。
- Venus（QEMU、2026-10-29 q1682-i02 P2）: `plan/ws068/tests/egl-p039.sh`（egltest `--scene=etc2`、display 直接と zdesktop の窓）を
  `plan/ws068/tests/venus-all.sh` で tree 595743d90 から作り直した image で流し、rc=0 PASS（`build/p2-v6/egl-p039.txt`: 2 枚の画面の読み取り、
  `EGLTEST ETC2 ready failures=0`、`EGLTEST CHECK run=d|w failures=0 glerror=0x0`）。q1682-i02 の範囲（p037 と回帰）の外の試験なので
  Status は Q1 が決める。
- 未実施: i915 実機。
