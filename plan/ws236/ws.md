<!-- awesome-plan project=zedbsd record=ws236 -->

# WS236: Raspberry Pi 4（VideoCore VI）の動画の再生の hardware 支援

<!-- awesome-plan-current:start -->
Status: planning（2026-10-14 起票。見積もりだけで、まだ実施しない）
Primary Milestone: MG006
Related Milestones: MG002
Parent: [Master](../master.md)
Related: [WS141](../ws141/ws.md)（VC6 の display と V3D）、[WS202](../ws202/ws.md)（libmedia の H.264・AAC の再生、今は software と i915 の Vulkan Video）、[WS207](../ws207/ws.md) p004（HDMI の音、同じ VCHIQ を使う）
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-14）

「VC6での動画再生支援に対応するWSを作れますか？見積もりをください。」

## 前提（Q1 の理解、未確認の点を含む）

- RPi4 の H.264 の decode（最大 1080p60）は VideoCore の VPU の firmware（start4.elf）の中にあり、ARM 側からは VCHIQ（mailbox と共有のメモリの message の口）越しに MMAL の `video_decode` を使う。Linux では bcm2835-codec（V4L2 の M2M）がこれを包む。H.264 の encode も同じ道にある（Pi 4 まで）。
- HEVC（最大 4Kp60）は別の専用の block（rpivid、stateless）。公開の datasheet は無く、資料は Linux の driver（GPL）だけ。写さずに作るには register の振る舞いを独自に書き起こす必要があり、危険が大きい。
- V3D（WS141）は 3D の GPU で、動画の codec は持たない。Vulkan Video は VC6 では使えないので、libmedia に RPi4 用の backend（`media_vchiq_ops` 仮）を足す形になる。
- VCHIQ の driver は WS207 p004（HDMI の音）と共通にできる。

## 決定（2026-10-14 ユーザー）

「VC6では、Vulkan VIdeoのドライバとして実装してほしいです。V3Dと連携しなかったとしても。可能ですか？HEVCはライセンスがないのでやらないです。エンコードは今回は置いておきます。」

- 形: libvulkan の Vulkan Video（`VK_KHR_video_queue`・`VK_KHR_video_decode_queue`・`VK_KHR_video_decode_h264`）を、i915 と同じく kernel の側の Vulkan の executor で実装する。V3D の device に decode の queue を足せなければ、decode の queue だけを持つ別の VkPhysicalDevice でよい。libmedia は今の `media_vkvideo_ops` の道をそのまま使う。
- HEVC はやらない（license が無い）。encode は今回は外す。

## 危険（Q1 の推測）

- MMAL の `video_decode` は stateful（bitstream を渡すと decoder が自分で DPB を持ち、表示の順に出す）。Vulkan Video は stateless（アプリが SPS・PPS を StdVideo の構造で、slice を bitstream で渡し、DPB の image と参照の slot をアプリが管理し、1 回の decode が 1 枚を decode の順に出す）。橋渡しに、(a) StdVideo の SPS・PPS から NAL を組み直して MMAL に流す、(b) アプリの DPB の image は形だけにする、(c) MMAL の並べ替えの遅れを無くす設定（表示の順でなく decode の順に出す、または POC の対応で戻す）、が要る。(c) が firmware の設定で出来なければ、遅れを吸収する queue を executor に置く。
- 出力の buffer: MMAL（VPU）が書ける memory に VkImage を置く必要がある（RPi4 は CMA 相当の共有の memory）。zero-copy にできなければ 1 回の copy。

## Phase と見積もり（2026-10-14 改訂）

| ID | 内容 | 見積もり | 依存 |
| --- | --- | --- | --- |
| ws236-p001 | 調査と設計: VCHIQ・MMAL の message、参考の source の license、MMAL の decode の順・遅れの設定、Vulkan Video の stateless の API との橋渡しの設計、VkImage の memory、V3D の device に queue を足すか別の device か | 6 LW | — |
| ws236-p002 | kernel: VCHIQ の driver の核（WS207 p004 と共用） | 10 LW | p001 |
| ws236-p003 | kernel: MMAL の `video_decode` の client（H.264、port・buffer・EOS・flush） | 10 LW | p002 |
| ws236-p004 | kernel の Vulkan の executor: VC6 の video の device と decode の queue、video session・parameters、decode の command を MMAL へ（NAL の組み直し、DPB の形だけの扱い、順の調整）、libvulkan の device の列挙 | 12 LW | p003 |
| ws236-p005 | libmedia（`media_vkvideo_ops`）と Video Player での再生、`vkvideo-probe` の試験 | 3 LW | p004 |
| ws236-p006 | 実機の確かめ（1080p の H.264 の mp4、CPU の使用率、seek、長時間）と直し | 5 LW | p005 |
| ws236-p007 | 規約の全文の確認 | 2 LW | 全て |

合計 約 48 LW（実時間で約 12〜20 時間）。p004 の stateful と stateless の橋渡しが一番不確か（+8 LW 程度の振れ）。HEVC・encode は対象外。
