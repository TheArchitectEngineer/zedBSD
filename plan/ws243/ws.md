<!-- awesome-plan project=zedbsd record=ws243 -->

# WS243: nvrtx の NVDEC で H.264 の decode（Vulkan Video）

<!-- awesome-plan-current:start -->
Status: incomplete（すぐ取り組む物。2026-10-27 ユーザー「すぐ取り組むものの一つです。」）。旧: incomplete・保留（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: 2026-10-15 ユーザーの指示で一時停止（ws243/ws.md「p001 を途中で一時停止、実機の debug は WS241 の後」）…
Primary Milestone: MG006
Related Milestones: MG002
Parent: [Master](../master.md)
Related: [WS037](../ws037/ws.md)（nvrtx、GSP-RM・channel・Vulkan の executor）、[WS083](../ws083/ws.md)（Vulkan Video の拡張と i915 の H.264、`src/drivers/gpu/i915/vk/vk-video.c`）、[WS236](../ws236/ws.md)（Pi 4 の Vulkan Video、`src/drivers/gpu/bcm2711/vulkan-video.c`）、[WS202](../ws202/ws.md)（libmedia の `media_vkvideo_ops`）、[WS242](../ws242/ws.md)（RTX 20/30/40）
Resume point: 2026-10-29 q1677-i02 P5: p004 cleared（hash 3 本一致、readback 3.4 fps）。ユーザーの決定（下）で p006（GPU の道、Video Player で 1080p60）へ。p006 は kernel の側（sampled NV12・plane の view・texture header）を実装し host 試験まで。実機（`rtx-p004.sh … video` の V1〜V4、V4 が Video Player の GPU の道）は 10.0.10.3 の枠待ち（[p006](phase006/phase.md)）。
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-15）

「それから、NVDECでH.264を利用できるようにするWSを作ります。Vulkan Videoで利用できるようにします。」
「NVDECはまずコードだけ書いておいて、WS241が完了してから実機デバッグします。」

## 目的と完了の条件

- nvrtx の Vulkan の device が `VK_KHR_video_queue`・`VK_KHR_video_decode_queue`・`VK_KHR_video_decode_h264` を出し、libmedia の今の `media_vkvideo_ops` の道（i915・Pi と同じ）で H.264 の mp4 が NVDEC で decode される。
- 完了: RTX 2070（TU106）の実機で Video Player が H.264 の mp4（WS236 の試験の clip、720p・1080p）を NVDEC で再生し、decode の結果が software の decoder と hash で一致する。30/40 は実機の確かめなし（WS242 と同じ扱い）。
- 範囲外: HEVC・AV1・VP9、encode（NVENC）。

## 前提（Q1 の理解、設計で置き換える）

- NVDEC は stateless（driver が picture の parameter と slice の bitstream を渡し、DPB は呼び手が持つ）で、Vulkan Video の形に近い。Turing の NVDEC の class・picture の parameter の構造は NVIDIA の公開の header（open-gpu-doc・open-gpu-kernel-modules、MIT）にある。Mesa NVK の video の decode（MIT）も参考になる。
- GSP-RM の環境では NVDEC の engine の channel を RM に割り当ててもらう（GR の work channel と同じ道、engine の class が違う）。NVDEC の firmware は GSP-RM が load するかを設計で確かめる。
- 出力は NV12（または生の block linear の形）で、compositor が sample できる VkImage に置く。
- 参考の source の扱いは WS037 と同じ（値は事実として使い、名前・構造・comment は写さない、guardrail）。

## Phase（案、p001 の設計で確定）

| ID | 内容 | 状態 |
| --- | --- | --- |
| [p001](phase001/phase.md) | 設計: NVDEC の channel の取り方（RM の alloc、class、firmware）、picture の parameter の組み立て、DPB と出力の image の layout、Vulkan Video の object と executor の口（WS083・WS236 の共通の部分の再利用）、license の監査、試験の道 | in-progress（設計済み・review 1 回済み） |
| [p002](phase002/phase.md) | 実装: NVDEC の channel と engine の起動、work channel の copy の object（code のみ、host 試験） | in-progress（実装済み・試験未実施） |
| [p003](phase003/phase.md) | 実装: Vulkan Video の object（profile・capability・session・parameters・DPB）、RESULT_STATUS の query pool、H.264 の decode の命令、NV12 の image と copy engine での readback（code のみ、host 試験） | in-progress（実装済み・試験未実施） |
| [p004](phase004/phase.md) | 実機の debug（RTX 2070 の素の起動、10.0.10.3）。main の取り込み、1080p30 の readback の速さもここで測る | cleared（2026-10-29 q1677-i02 P5: hash 3 本一致、readback 3.4 fps） |
| p005 | 規約の全文の見直し | planned |
| [p006](phase006/phase.md) | GPU の道: SAMPLED の NV12（plane の R8・R8G8 の view、BL GOB 2 段の texture header）と memory の export、Video Player が NVDEC の picture を GPU で描き compositor へ、RTX 2070 で 1080p60 の再生 | in-progress（2026-10-29 q1677-i02 P5: 実装と host 試験済み、実機は未実施） |

## ユーザーの決定（2026-10-29 22:4x、Q1 経由）

- 原文:「NVDECですが、動画アプリでの再生で、NVDECからGPUバッファに描画して、それをコンポジタが描画する流れにして、1080p60が再生できるようにしてください。CPUメモリはあくまでも機能性テストの1ステップです。」
- 解釈（Q1）: p006 の目的は、動画の app（Video Player）が 1080p60 を再生できること。NVDEC が GPU の buffer（VkImage）へ decode し、app はそれを CPU を通さずに（export・共有）使い、compositor が描く。受け入れは RTX 2070 での 1080p60 の再生の計測を含む。CPU の readback の道は機能の試験（hash の照合）の 1 段としてだけ残す。guardrail の一時の例外は外す。(a) の host cached な memory type はこの目的には要らない（覚え書きだけ）。
- 完了の条件への効き方（P5 の読み）: 上の「目的と完了の条件」の Video Player の再生は GPU の道（[p006](phase006/phase.md)）で、1080p60 を落とさず（FRAMES の late と表示の数で見る）に再生することを加える。

## 実装だけの担当 P6（2026-10-29、q1667）

- 2026-10-29 01:3x ユーザー「P6は設計が決まっているWSについて、テストなしでひたすら実装だけ進めてテスト未完了でincompleteだけど実装はできていてマージされていない状態のWSを増やす。」
- ユーザー「調査しなくてよい、設計ができているか自明なWSについて、ひたすら書いてください。選択したもののうち、調査が必要なものはスキップします。WSごとにサブのサブエージェントを作ってよいです。」（選択: WS223・WS282〜WS284・WS243・WS296）
- 解釈（Q1）: 試験をせず実装だけを P6 の branch に積み、main に merge しない。Status は incomplete のまま（試験は未実施）。調べが要ると分かれば P6 は飛ばして Q1 に返す。

## 推しで進めた判断（2026-10-29、q1677-i01、ユーザー不在のため Q1 の指示で）

- Q1（原文）:「(1)(2) はユーザーが不在なので推しで進めて: 今は readback で正しさを先に（guardrail の「画像の処理は GPU で」からの一時の例外として ws.md に記録）、SAMPLED NV12 と export の GPU の道は後の Phase（p006 案）に置き、design.md に「本線は GPU の道」と書く。1080p30 の readback の速さは p004 の実機で測ってから判断（cached な system memory の type は WS243 の外の提案として記録だけ）。Q1 がユーザーに後でメッセージで聞く。」
- ~~guardrail の一時の例外~~（2026-10-29 22:4x のユーザーの決定（上）で外した: CPU の readback の道は機能の試験（hash の照合）の 1 段に限る）。
- **WS243 の外の提案（記録だけ、不要）**: readback の buffer を host cached な system memory に置く memory type（executor 全体の変更）。ユーザーの決定（上）で 1080p60 は GPU の道で出すので、この目的には要らない（覚え書きとしてだけ残す）。
- **p004 の結果と判断（2026-10-29 22:3x）**: readback の道の 1080p30 は 3.4 fps（libmedia の CPU の copy が 264 ms/picture、nvrtx の唯一の memory type は VRAM で CPU は BAR1 の WC の窓から約 12 MB/s で読む）。Q1（原文）:「(b) で進めてください。design.md で GPU の道が本線と決めてあり、guardrail「画像の処理は GPU で」に沿うので、p006 を次に。(a) の host cached な memory type は WS243 の外の提案として記録だけ（Q1 がユーザーに聞く）。」 (a) に要る物: executor 全体に host cached な sysmem の memory type（GPU の page table の sysmem の aperture と memory の割当の両方の変更）。
