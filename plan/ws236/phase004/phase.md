<!-- awesome-plan project=zedbsd record=ws236-p004 -->

# ws236-p004: V3D の Vulkan の executor の video decode（session・parameters・decode を MMAL へ、status query、NV12）

Parent: [WS236](../ws.md)
Status: in-progress（P3、2026-10-14。実装と build（warning 0）まで。実機は p005 の T1 の依頼と一緒）
Phase disposition: normal

## 範囲

[design.md](../design.md) §6・§7・§14（review-1 の S1・S2・S3・M1・M2・M3・M8）。Q1 の許可（2026-10-14）: WS141 の V3D の executor の変更は `#if CONFIG_DRIVER_BCM2711_VIDEO`・既定 off の範囲で。

## 変えた file

| file | 内容 |
| --- | --- |
| `src/drivers/gpu/bcm2711/vulkan-video.c`・`.h`（新） | 0x10000〜0x1000d の route: 問い合わせ（Baseline・Main・High、4:2:0 8 bit、16×16〜1920×1088、DPB 17・参照 16、level 4.2、SEPARATE_REFERENCE_IMAGES、COINCIDE、NV12 の OPTIMAL・DECODE_DST|DPB|TRANSFER_SRC、family 0 = H.264 decode）、session（作成で MMAL の decoder を作り失敗はその場で INITIALIZATION_FAILED、M8）、memory の要求（1 page、使わない）、parameters（作成・更新・template、i915 と同じ wire の読み）、記録 4 つ（begin・control・decode・end、scope の begin を同じ command buffer から引く）。RESULT_STATUS_ONLY の query pool（作成・破棄・結果、記録の begin・end・reset、S1）。実行: control の RESET は MMAL の flush と送った set の忘却、decode は SPS（VUI は並べ替え 0）・PPS を変わった時と IDR の時に、続けて app の bitstream の範囲を staging に、出力の image（NV12、COINCIDE）を MMAL の frame の宛先に指定し、render window を開けて（controller の mutex を離して、S2）送って受ける。遅れて来た前の decode の frame は捨てる。宛先と layout が合わない frame は bounce から行ごとに写す。bulk が完了しない時は image の memory の view を quarantine にする（S3）。失敗・timeout は picture を飛ばし status を ERROR（submission は失わない）、3 回続く timeout で session は待たなくなる。`VIDEO_BASELINE_ONLY`（R1 が外れた時に 1 にする） |
| `vulkan-private.h` | `struct bcm2711_vulkan_session` に `video`（capset の約束の latch、M2）を `#if` で |
| `render-runtime.c` | capset: VCHIQ が繋がっていれば（または既に約束した session なら）176 byte、native の語 "TANZ"・bit 0、session に latch |
| `vulkan-dispatch.c` | route の表に video と query pool の 2 つ（`#if`） |
| `vulkan-query.c` | 約束した session の family 0 に VIDEO_DECODE の flag、NV12 の format の feature（DECODE_OUTPUT・DPB・TRANSFER_SRC） |
| `vulkan-resource.c` | NV12 の image（usage DECODE_DST・DPB・TRANSFER_SRC、layout は MMAL と同じ: pitch 32 に丸め、luma 16 行に丸め、chroma は続けて）、bitstream の buffer の usage DECODE_SRC、`vkGetImageSubresourceLayout` の PLANE_0・PLANE_1（OPTIMAL でも、N3 と同じ私的な約束）。off の時の判定は `#else` で元のまま |
| `vulkan-buffer-copy.c` | NV12 の image から buffer への CPU の copy（PLANE_0 は 1 byte、PLANE_1 は 2 byte で半分の大きさ） |
| `vulkan-barrier-validate.c` | VIDEO_DECODE_DST・DPB の layout と、その usage の検査 |
| `vulkan-draw.c` | walk で video と query の event を選ぶ（render pass の外） |
| `vulkan-native-job.c` | barrier の後に video と query の event を CPU の仕事として順に実行 |
| `platform/arm64/vmunix.mk` | `CONFIG_DRIVER_BCM2711_GPU` と `CONFIG_DRIVER_BCM2711_VIDEO` が y の時 `vulkan-video.c` |
| `src/drivers/platform/rpi4/rpi4-h264.c`・`.h` | StdVideo の値から SPS・PPS の NAL を書く（scaling list・POC type 1・cropping・VUI の並べ替え 0） |
| `src/drivers/platform/rpi4/rpi4-mmal.c`・`.h` | frame を指定の物理 memory に直に受ける target（layout と大きさが合う時、plane の情報が無ければ format の stride で判定）、`busy` |

## 検証（2026-10-14、host と build）

- `sh plan/ws236/tests/run-h264-test.sh` → `h264: all PASS` と `h264-write: all PASS`（ws083 の 3 つの stream の SPS・PPS を libmedia の parser で StdVideo に読み、`drv_rpi4_h264_write_sps/pps` で書き戻して元と置き換えると、ffmpeg の trace_headers で SPS（VUI の前まで）・PPS（全体、cqm の scaling list を含む）が同じに読め、decode の結果の frame が同じ）。
- build（warning 0、`arm64 vmunix check: PASS`）: `plan/ws236/tests/config-rpi4-video.mk`（`build/ws236-video`）、既定の `config/ci/config-rpi4.mk`（`build/ws236-rpi4-off`）、probe の config。
- 未実施: 実機（下の p005 の T1 の依頼）。executor の video の wire の host の試験は作っていない（i915 と同じ wire の読みを写した。実機の vkvideo-probe で確かめる）。

## 既知の制限

- decode の間（1 枚 約 10〜20 ms の見込み）executor の thread は MMAL を待つ（mutex は離すので ioctl は進むが、他の session の V3D の仕事は待つ、R3）。
- DPB の slot の規則（active・inactive）は検べない（firmware が参照を持つ）。
- 出力の image は 1 GiB の下の V3D の memory（VPU の DMA が直に書く）。
