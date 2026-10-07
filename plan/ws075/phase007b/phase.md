<!-- awesome-plan project=zedbsd record=ws075-p007b -->

# ws075-p007b: GL 3.2 の stage の実行器（render/）: 増分 b1〜b5

Status: in-progress（q833、P1。2026-10-07 夜 b3 を実装と host 試験。b1 は未着手で中断: ws051-p004b を優先、再開は下の「再開の情報」）
Disposition: normal
Parent: [WS075](../ws.md)
設計: [phase007/design.md](../phase007/design.md)（§5・§14 が優先）、増分の表は [phase007/phase.md](../phase007/phase.md)。

## 承認

Q1（2026-10-07）: 判断 1〜8 を既定どおりで承認、Phase の ID は p007a・p007b で可、b3 は p007a より先でよい。b3 の範囲の ACK（2026-10-07 夜）。

## 記録

- 2026-10-07 夜 増分 b3（layered の描画、p007a に依存しない）:
  - `render/gfx.h`: framebuffer に `layers`、clear_attachment の op に `base_layer`・`layer_count`。
  - `render/render-pass.c`: `VkFramebufferCreateInfo.layers` を持つ（0 は 1）。
  - `render/state.c`: `i915_state_target_range` は view の `layer_count` が 2 以上（3D の image でない）なら layer_count に（Surface Array、Depth と Render Target View Extent = layers − 1）。3DSTATE_DEPTH_BUFFER と 3DSTATE_STENCIL_BUFFER の Depth を「image の全 slice − 1」から「view の layer − 1」に、dword 7 に Render Target View Extent（bits 31:21）を足した（isl `isl_emit_depth_stencil.c` 147〜163、gen120.xml bits 245〜255、出典は code の注に sha256）。
  - `render/command.c`: pass begin の clear は attachment ごとに framebuffer の layers（view の layer 数で頭打ち）の各 layer を 1 layer の view で fill（本体を `i915_execute_clear_layer` に分けた）、vkCmdClearAttachments は VkClearRect の `baseArrayLayer`・`layerCount` を記録し layer ごとに fill（本体を `i915_execute_clear_rect` に、count 0 は 1 と読む）。
  - `render/instance.c`: `maxFramebufferLayers` 2048。
  - 試験（新）: `plan/ws075/tests/host-layered.c`・`run-host-layered.sh`（framebuffer の layers の decode と 0 → 1、2 layer の framebuffer で begin の clear が 2 layer・ClearAttachments の 2 layer の rectangle が 2 fill（各 layer の address と rectangle）、1 layer の framebuffer では begin が 1 layer、2 layer の色の target の surface state の Surface Array・Depth 1・RTVE 1、2 layer の depth view の 3DSTATE_DEPTH_BUFFER の Depth 1・RTVE 1・QPitch）。

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws075/tests/run-host-layered.sh` | plain・ASan/UBSan PASS、leak 0 |
| `sh plan/ws031/tests/run-vk-host-tests.sh "res resdispatch pipe cmdbuf"` | PASS（design §12 の 13 の cmdbuf の既存の失敗 `test_blend_state` は今の main では PASS、直す物は無い） |
| `sh plan/ws083/tests/run-host-video-roundtrip.sh` | PASS（command.c の変更の後も） |
| `make -j16 BUILD=build/p1-k ZEDBSD_CONFIG=config/ci/config-amd64.mk build/p1-k/vmunix` | 成功 warning 0 |

未実施: 実機（RTAI による layer の書き分け、design §12 の 10。b5 で T1）。libvulkan の wire が `layers`・`VkClearRect` の layer を運ぶことは codec の decoder（generic）と host の wire で確かめた（§12 の 11 の b3 の分）。

残り: b1・b2・b4・b5（p007a の a3 の後）。

### 再開の情報（2026-10-07、P1。ws113-p011a の merge を受け、Q1 の指示で ws051-p004b の code を優先して中断）

- 済み: b3。base main 3b004a2c6 で `sh plan/ws031/tests/run-vk-host-tests.sh`（10 個）を流し plain・ASan/UBSan 全て PASS（3 分 49 秒）。
- b1 は code に未着手（変更無し）。次: b1（design.md §5.1・§14 S3・S7・minor 8、phase007/phase.md の増分の表）。p007a の再開の情報の b1 の項（prepare の GS の検査と compiler の refuse の理由の log の決め）もここで扱う。
- 再開の前に: main の今を merge、10 個の host 試験を一度流す。
