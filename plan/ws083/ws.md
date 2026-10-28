<!-- awesome-plan project=zedbsd record=ws083 -->

# WS083: Vulkan Video の拡張と i915 の対応（最初の目標 H.264 の decode）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: p001〜p008 cleared（p007 は 2026-10-13 ユーザーの実機の確かめ「OKでした」）。Vulkan Video は既定で on。p009 は 2026-10-10 ユーザー「P2はp009は不要です。p007, p008完了でラップアップでいいです。」）
Completed: 2026-10-27（q1608 の精査）
Primary Milestone: MG006
Related Milestones: MG002
Objectives: O1
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

- 自前の libvulkan（`userland/desktop/libvulkan`）に Vulkan Video の拡張（`VK_KHR_video_queue`・`VK_KHR_video_decode_queue`・
  `VK_KHR_video_decode_h264`）を足し、i915 の driver（5330、Alder Lake、Gen12）の video の engine（VCS、MFX）で H.264 を decode する。
- 最初の到達点: H.264 の elementary stream（8 bit・4:2:0・progressive、Baseline/Main/High の一般的な形）を decode し、各 frame が
  参照の decoder（host の ffmpeg 等）の出力と一致する。I frame から、次に P・B と DPB。
- encode（`VK_KHR_video_encode_h264`）・H.265・AV1 は後（Future）。

設計は [design.md](design.md)（第 3.1 版、review 3 回）、GPU の UAPI の差分の案は [proposed/](proposed/README.md)、利用者向けの説明は `docs/reference/vulkan-video.md`。

## ユーザーの決定

- 2026-09-28: 「WSを追加します。OSCデモの必須ではないです。Vulkan Video拡張を追加して、i915のドライバ対応を行います。H.264を最初のターゲットにします。」
- 2026-10-02: fg019（ベータ1、10/17）に入れる。「Vulkanのビデオ再生拡張をIntel Xe-LPで実装する。H.264を最初のターゲットとする。」動画プレーヤ（WS122）・ブラウザ（WS121）の土台（VA-API の WS123 は canceled、アプリが Vulkan Video を直接使う）。
- 2026-10-07: 人の判断 H1〜H5・HD1〜HD6 を全部決定（design.md）。p001・p002 と host で作れる所までエージェントが進める（10-02 の「別セッション」の指示を置き換え）。
- 2026-10-10: 「P2はp009は不要です。p007, p008完了でラップアップでいいです。」
- 2026-10-11: 「Vulkan Videoをデバッグオプションでなくてデフォルトで有効な機能にしてください。」
- 2026-10-13: p007 の実機の確かめ「OKでした」。「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」
- 2026-10-27（q1608）: 「…masterとWSを修正して、completedやcanceledにしてください。」

## 結果

- libvulkan の Vulkan Video（`VK_KHR_video_queue`・`video_decode_queue`・`video_decode_h264`、sync2 の翻訳、capset の native の語）（p002）、i915 の VCS0 の engine・context・hang の封じ込め（p003a）、実行器の video の module と submit（p003b）、MFX AVC の builder・genxml の独立の decoder・試験の stream・`vkvideo-probe`（p004）、P・B と DPB（p006a）、`GRDOM_MEDIA` の engine 単位の reset と hang の回復（p007）、性能・門・result status query（p008）。
- 実機（5330）: p005 の VCS の bring-up と I frame の hash、p006b の P・B の hash（p-baseline-64 10/10・pb-main-352 15/15・pb-high-352-pyramid 15/15 一致）、p007 はユーザーの確かめ「OKでした」。
- 設計と実装の照合の review（2026-10-08 q897、design-reviewer）: blocking 無し。R-S1（upper bound を page の終わりに）・R-S2（止まった後の ECANCELED）・R-S3（quarantine の session の video を拒む）・R-S5（VCS0 の timeout 1 秒）・R-S6（libvulkan の native の経路の host 試験）を直した。R-S4（ready-to-reset の無い時の reset）は実機で。

## 制限・移管

（p008、2026-10-10 P2 の記録。既定は 2026-10-11 のユーザーの決定で ON）

利用者向けの説明は `docs/reference/vulkan-video.md`。WS083 の到達点の制限:

- **既定は ON**（2026-10-11 ユーザー「Vulkan Videoをデバッグオプションでなくてデフォルトで有効な機能にしてください。」）: kernel は VCS0 のある GT で video decode を出す。`i915.debug=video` は受けるが何も変えない。2026-10-10 の「release は OFF」を置き換えた。image の中で使う program は試験の `vkvideo-probe` だけ（Video Player は libmedia → FFmpeg で CPU、FFmpeg は `--disable-hwaccels`、自前の decoder は [WS202](../ws202/ws.md)）。
- **形**: H.264 の decode だけ（Baseline・Main・High、8 bit、4:2:0、progressive、4096x4096・level 5.1 まで）。interlaced（field・MBAFF・PAFF）・4:2:2 以上・10 bit・encode・H.265・AV1 は無い（Future）。
- **規格に合わない点**（HD6）: N1 apiVersion 1.0 のまま video の拡張を名乗る（sync2 は 1.0 の command への翻訳）、N2 ycbcr の拡張無しで NV12 と plane の aspect、N3 OPTIMAL の subresource layout を返す、N4 slice が 256 を超える picture は飛ばす（libvulkan の README）。result status の pool は profile を見ない。
- **出力の使い道**: decode の picture は host から読むだけ（SAMPLED・TRANSFER_SRC は無い、HD5）。表示には app が de-tile して別の image に写す。
- **実機で確かめた範囲**: 5330（Alder Lake-P、Gen12）で `tests/streams` の 6 本（I 3・P/B 3、CAVLC・CABAC・複数 slice・非対称の scaling list・weighted・list の modification・B pyramid と MMCO 1）の全 frame の hash が一致、result status は COMPLETE。long term・frame_num の gap・MMCO 5・4 byte の start code・1080p の hash は host の golden だけ（実機の hash は未）。
- **hang の回復**: VCS0 の engine 単位の reset（GRDOM_MEDIA）と上限 3 回は実装と host 試験済み、実機（p007 の F1・F2、試験の kernel が要る）は p008 の時点で未実施、後に 2026-10-13 ユーザーの実機の確かめ「OKでした」で p007 は cleared。hang した session の quarantine は video の submit・session の create だけを拒み、render の stream の入口は見ない（WS031 の側の残件、上の review の節）。
- **性能**（T1-435 の E・E2、5330）: 1920x1080 の H.264（Baseline・Main・High、60 frame）で 1 decode の mean 4.2〜5.2 ms、longest 8 ms（probe は 1 decode ごとに submit と wait、worker は 1 本で同期）。60 fps に余裕。decode と同時の描画の fps への影響（U8）は測っていない。

- 残件（2026-10-08 Q1 の指示で記録）: render 側の quarantine の門。hang した session（quarantined）でも render の stream の入口（`vk/vk-command.c` の `i915_command_render`）は quarantine を見ず、graphics の family の submit・新しい resource は続けられる（design §6.1 の「quarantine はその session の新しい resource・job を拒む」に届いていない）。R-S3 は video の submit と video session の create だけを拒む。直すなら WS031（i915 の Vulkan 実行器）の側の別 Queue。
- 規約の全文（p009）は 2026-10-10 ユーザーの決定で不要。
- 試験（`tests/`、試験の stream の生成を含む）は WS202・WS229・WS252 の試験と vkvideo-probe が使うので `plan/tools/` へ移す（Q1 の OK の後）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws083-p001 | 設計 | cleared（2026-10-08 Q1） |
| ws083-p002 | libvulkan の骨組み、host の試験 | cleared（2026-10-10 Q1、5330 の実機で照合） |
| ws083-p003a | i915: VCS0、engine ごとの context、hang の封じ込め | cleared（2026-10-10 Q1） |
| ws083-p003b | 実行器の video の module、submit の骨組み | cleared（2026-10-07 Q1） |
| ws083-p004 | MFX AVC の I frame の builder、genxml の decoder、試験の stream、vkvideo-probe | cleared（2026-10-10 Q1） |
| ws083-p005 | 実機: VCS の bring-up と I frame の hash | cleared（2026-10-10 Q1、5330） |
| ws083-p006a | P・B と DPB（host） | cleared（2026-10-10 Q1） |
| ws083-p006b | P・B と DPB の実機の hash | cleared（2026-10-10 Q1、5330） |
| ws083-p007 | `GRDOM_MEDIA` の reset と VCS の hang の回復 | cleared（2026-10-13 ユーザー「OKでした」） |
| ws083-p008 | 性能、門の既定化、result status query | cleared（2026-10-10 Q1） |
| ws083-p009 | 全文規約確認と回帰 | 取り下げ（2026-10-10 ユーザー「P2はp009は不要です。…」） |

## 記録の所在

Phase の計画・結果は削除する。git の commit `fb1f6afee` の `plan/ws083/` にある。Queue の履歴は [plan/history](../history/index.md)。
