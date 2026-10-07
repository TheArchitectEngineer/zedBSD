<!-- awesome-plan project=zedbsd record=ws031-p033 -->
# ws031-p033: executor: image view の format の読み替えと component swizzle

Phase ID: `ws031-p033`
Parent: [WS031](../ws.md)
Status: cleared 候補（q833、P1、2026-10-07: 実装と host の試験 PASS。実機は対象外）
設計: [p019](../phase019/phase.md) §2.3・§2.4・§7（B1・B2・M1〜M4）
範囲の変更（2026-10-07、p019 §2.5・S9）: ws.md の「usage 照合」は変えない（input・transient attachment は executor に無く、今の答えが正しい。誤用の照合は backlog）。

## 実装（2026-10-07、P1）

| 部分 | file | 中身 |
| --- | --- | --- |
| view の format | `render/image.c`（`drv_i915_gfx_view_format`）、`gfx.h` | view の format で texel を読む: image と view がどちらも executor の置く colour の format で、texel の byte 数が同じ時（`MUTABLE_FORMAT` の例: UNORM の image を SRGB で、RGBA8 を R32_UINT で）。depth・stencil、byte 数が 0（未対応の format）、byte 数の違う view は image の format |
| texture・target | `render/state.c`（`struct i915_image_range` に `format`・`swizzle_set`、`i915_image_surface_write`、`i915_state_target_range`、`i915_state_target_integer`） | texture と render target の SURFACE_FORMAT、整数の target の判定を view の format から |
| attachment の clear（B2） | `render/command.c`（`i915_attachment_surface`） | LOAD_OP_CLEAR と vkCmdClearAttachments の surface も view の format（SRGB の view の clear が描画と同じく encode） |
| swizzle（B1） | `render/image.c`（`view->swizzle_set`）、`render/state.c` | vkCreateImageView が作った view だけ channel select を使い、4 成分 ZERO（0）も書く。memset の view（試験）は今の identity。`XXX` を消した |
| 試験 | `plan/ws031/tests/i915-vk-cmdbuf-test.c`（`test_view_format_swizzle`） | memset の view が identity・image の format、4 成分 ZERO が 0、R と B の入れ替え、SRGB の view が SRGB（0x0c8）、R32_UINT（0x0d7）、R8G8（2 byte）は image の format、depth の image の colour の view・R16_UNORM（大きさ不明）・UNDEFINED は image の format |

## 確認（host、2026-10-07）

| 確認 | 結果 |
| --- | --- |
| WS031 の host fixture の全部（`run-vk-host-tests.sh` の既定の一覧、通常と ASan/UBSan、script の rm を除いて手で） | PASS |
| `make -j16 disk-image`（vmunix の i915） | exit 0、自前の warning 0 |

未実施: 実機（5330、Q1 の範囲で対象外）、render pass の clear の fixture（cmdbuf の fixture に framebuffer が無い、S7。clear は同じ `drv_i915_gfx_view_format` を通る）、規約の見直し（p048）。
