<!-- awesome-plan project=zedbsd record=ws243 -->

# WS243: nvrtx の NVDEC で H.264 の decode（Vulkan Video）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-30 q1677-i02 P5。RTX 2070 の実機で Video Player が NVDEC の picture を GPU の道で 1080p60 再生、hash は software と一致）
Primary Milestone: MG006
Related Milestones: MG002
Parent: [Master](../master.md)
Related: [WS037](../ws037/ws.md)（nvrtx、GSP-RM・channel・Vulkan の executor）、[WS083](../ws083/ws.md)（Vulkan Video の拡張と i915 の H.264、`src/drivers/gpu/i915/vk/vk-video.c`）、[WS236](../ws236/ws.md)（Pi 4 の Vulkan Video、`src/drivers/gpu/bcm2711/vulkan-video.c`）、[WS202](../ws202/ws.md)（libmedia の `media_vkvideo_ops`）、[WS242](../ws242/ws.md)（RTX 20/30/40）
Resume point: 完了。残りは下の「制限・移管」（i915・amdrdna の実機の Video Player は未実施、BUG-353 の glass の時間は未計測）。
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-15）

「それから、NVDECでH.264を利用できるようにするWSを作ります。Vulkan Videoで利用できるようにします。」
「NVDECはまずコードだけ書いておいて、WS241が完了してから実機デバッグします。」

## ユーザーの決定（2026-10-29 22:4x、Q1 経由）

- 原文:「NVDECですが、動画アプリでの再生で、NVDECからGPUバッファに描画して、それをコンポジタが描画する流れにして、1080p60が再生できるようにしてください。CPUメモリはあくまでも機能性テストの1ステップです。」
- 解釈（Q1）: p006 の目的は、動画の app（Video Player）が 1080p60 を再生できること。NVDEC が GPU の buffer（VkImage）へ decode し、app はそれを CPU を通さずに（export・共有）使い、compositor が描く。受け入れは RTX 2070 での 1080p60 の再生の計測を含む。CPU の readback の道は機能の試験（hash の照合）の 1 段としてだけ残す。guardrail の一時の例外は外す。(a) の host cached な memory type はこの目的には要らない（覚え書きだけ）。
- 完了の条件への効き方（P5 の読み）: 上の「目的と完了の条件」の Video Player の再生は GPU の道（[p006](phase006/phase.md)）で、1080p60 を落とさず（FRAMES の late と表示の数で見る）に再生することを加える。

## 目的と完了の条件（当初）

- nvrtx の Vulkan の device が `VK_KHR_video_queue`・`VK_KHR_video_decode_queue`・`VK_KHR_video_decode_h264` を出し、libmedia の `media_vkvideo_ops` の道（i915・Pi と同じ）で
  H.264 の mp4 が NVDEC で decode される。
- 完了: RTX 2070（TU106）の実機で Video Player が H.264 の mp4（720p・1080p）を NVDEC で再生し、decode の結果が software の decoder と hash で一致する。
  2026-10-29 のユーザーの決定（上）で、再生は GPU の道（NVDEC → GPU の buffer → app → compositor）で 1080p60 を落とさないことを加えた。30/40 は実機の確かめなし（WS242 と同じ扱い）。
- 範囲外: HEVC・AV1・VP9、encode（NVENC）。

## 結果（実機は 10.0.10.3 の RTX 2070、QEMU ではない）

- decode の正しさ（readback の道、機能の試験）: `rtx-p004.sh … video` の V1 で 4 本とも software（`nv12-hashes.py`）と hash が順に一致:
  1080p30 150/150、1080p60 596/596、720p30 90/90、1080p24 Main 149/149（2026-10-30 01:29〜、vmunix 3454572979）。readback の道の速さは 3.5 fps（機能の試験の 1 段に限る）。
- GPU の道（[p006](phase006/phase.md)）: Video Player が libmedia の decode の image（SAMPLED の NV12）を export・import し、plane の view を自分の shader で描き、自分の窓へ。
  全画面 4 本とも `PRESENT own=1 gpu_pictures=1`・「pictures the GPU holds」・read-back 0・FAILED 無し。1080p60 は shown 584/596・late 12（decode 120 枚/2 s）、
  1080p30 148/150、720p30 88/90、1080p24 148/149。窓（1280x720）でも合成されて写る（shot に動画、shown 543/596・late 53）。
- 開始の止まりの原因と直し: nvrtx の blob の VRAM の消しが CPU の PRAMIN で約 31.7 MB/s（8 MB で 0.25 s）だった。256 KiB 以上の blob を driver の work channel の
  copy engine の fill で消すように変え（失敗すれば以後 CPU の消しに戻り、時間切れの VRAM は返さない）、1080p60 の late は 148 → 8〜12。BUG-353（WS309）と同じ原因。
- 非退行: hang の確かめ（`plan/ws250/tests/rtx-hang-check.sh`、video=0）S0〜S5 が WS250 の記録の値と同じ形。S5 の control の 4.3 s は WS250 の記録（phase004 r3 の 4301 ms）
  と同じで、CE の消しを切った A/B（`nvrtx.clear.cpu=1`）でも同じ（`rtx-s5-check.sh`）。fault の注入（`nvrtx.clear.fault=1`）で、CE error → 以後 CPU の消し、desktop と
  Video Player は生きる。mview 59.9 fps（decode と並行でも 60.2 fps）。
- QEMU（Venus、amd64）: Video Player の presenter は Vulkan Video の無い device で libavcodec の CPU の picture を描き、窓に動画（FAILED 0）。

## 設計の訂正（2026-10-30、計画に無かった依存）

- design.md・p006 の設計は「Video Player・libvulkan・libmedia は変えない」としていたが、Video Player は amd64 で自前の presenter を使わず（`VP_GPU_PRESENT` が arm64 だけ）、
  GPU の picture を頼んでいなかった。Q1 の判断（2026-10-30 00:4x、要約）: ユーザーの決定が動画 app での GPU の道の再生なので videoplayer は WS243 の範囲。→ amd64 でも 1。
- nvrtx の側にも足りない物が 2 つあった: export の識別の op（`GPU_OP_GET_PHYSICAL_DEVICE_PROPERTIES2`）と `GPU_CAP_ALLOCATION_SHARE`。
- VRAM の消しの copy engine 化は nvrtx の memory の core の変更。Q1 の判断（2026-10-30 01:1x、要約）: 普通の技術の判断として WS243 の範囲で扱う。詳細は [p006](phase006/phase.md)。

## 制限・移管

- i915（5330）・amdrdna の実機の Video Player は流していない（`VP_GPU_PRESENT` を amd64 でも 1 にしたので、その GPU でも自前の presenter の道になる）。
- 全画面の GPU の道では keiland-shot が 0 byte（compositor が合成せずに出す間は撮れない）。画面の確かめは窓で撮る。
- driver の work channel が回復で止められた後の作り直し（再 open）は WS250 の回復にも無く、実装していない（app の作成は壊れない形にした）。
- GR の runlist で draw が止まっている間は copy engine の消しも待ち、2 s を越えると以後 CPU の消しに戻る（安全側）。
- BUG-353 の glass maps の作り直しの時間（video=0）は未計測（`plan/ws309/tests/rtx-bug353-pair.sh` は未実施）。
- 今後も使う試験（Q1 に移管を頼む）: `rtx-video-check.sh`・`rtx-p004.sh`・`rtx-s5-check.sh`・`nv12-hashes.py`・`venus-videoplayer.sh`（と config）、host 試験
  `nvdec-host-test`・`wire-host-test`・`plane-header-host-test`・`copy-clear-host-test`。clip は git の外（`build/ws243/clips`、作り方は p006）。

## Phase

| ID | 内容 | 状態 |
| --- | --- | --- |
| [p001](phase001/phase.md) | 設計（design.md）: NVDEC の channel、picture の parameter、DPB と出力、Vulkan Video の object、license、試験の道 | cleared（2026-10-30、設計どおり実装され p004・p006 の実機で確かめた） |
| [p002](phase002/phase.md) | 実装: NVDEC の channel と engine、work channel の copy の object | cleared（2026-10-30、p004 の実機: nvdec-channel ok・copy object） |
| [p003](phase003/phase.md) | 実装: Vulkan Video の object、RESULT_STATUS の query、H.264 の decode、NV12 と readback | cleared（2026-10-30、p004・p006 の実機: hash 4 本一致） |
| [p004](phase004/phase.md) | 実機の debug（readback の道、1080p30 の速さ） | cleared（2026-10-29 q1677-i02 P5） |
| [p005](phase005/phase.md) | 規約の全文の見直し | cleared（2026-10-29 q1677-i02 P5） |
| [p006](phase006/phase.md) | GPU の道（sampled NV12・export、Video Player の presenter、VRAM の消しの copy engine）、RTX 2070 で 1080p60 | cleared（2026-10-30 q1677-i02 P5） |

## 実装だけの担当 P6（2026-10-29、q1667）

- 2026-10-29 01:3x ユーザー「P6は設計が決まっているWSについて、テストなしでひたすら実装だけ進めてテスト未完了でincompleteだけど実装はできていてマージされていない状態のWSを増やす。」
- ユーザー「調査しなくてよい、設計ができているか自明なWSについて、ひたすら書いてください。選択したもののうち、調査が必要なものはスキップします。WSごとにサブのサブエージェントを作ってよいです。」（選択: WS223・WS282〜WS284・WS243・WS296）
- 解釈（Q1）: 試験をせず実装だけを P6 の branch に積み、main に merge しない。Status は incomplete のまま（試験は未実施）。調べが要ると分かれば P6 は飛ばして Q1 に返す。

## 推しで進めた判断（2026-10-29、q1677-i01、ユーザー不在のため Q1 の指示で）

- Q1（原文）:「(1)(2) はユーザーが不在なので推しで進めて: 今は readback で正しさを先に（guardrail の「画像の処理は GPU で」からの一時の例外として ws.md に記録）、SAMPLED NV12 と export の GPU の道は後の Phase（p006 案）に置き、design.md に「本線は GPU の道」と書く。1080p30 の readback の速さは p004 の実機で測ってから判断（cached な system memory の type は WS243 の外の提案として記録だけ）。Q1 がユーザーに後でメッセージで聞く。」
- ~~guardrail の一時の例外~~（2026-10-29 22:4x のユーザーの決定（上）で外した: CPU の readback の道は機能の試験（hash の照合）の 1 段に限る）。
- **WS243 の外の提案（記録だけ、不要）**: readback の buffer を host cached な system memory に置く memory type（executor 全体の変更）。ユーザーの決定（上）で 1080p60 は GPU の道で出すので、この目的には要らない（覚え書きとしてだけ残す）。
- **p004 の結果と判断（2026-10-29 22:3x）**: readback の道の 1080p30 は 3.4 fps（libmedia の CPU の copy が 264 ms/picture、nvrtx の唯一の memory type は VRAM で CPU は BAR1 の WC の窓から約 12 MB/s で読む）。Q1（原文）:「(b) で進めてください。design.md で GPU の道が本線と決めてあり、guardrail「画像の処理は GPU で」に沿うので、p006 を次に。(a) の host cached な memory type は WS243 の外の提案として記録だけ（Q1 がユーザーに聞く）。」 (a) に要る物: executor 全体に host cached な sysmem の memory type（GPU の page table の sysmem の aperture と memory の割当の両方の変更）。
