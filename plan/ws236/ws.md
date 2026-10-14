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

## Phase と見積もり（Q1、2026-10-14。1 LW ≈ 15〜25 分）

| ID | 内容 | 見積もり | 依存 |
| --- | --- | --- | --- |
| ws236-p001 | 調査と設計: VCHIQ の protocol と MMAL の message、参考の source と license（Linux の vchiq・bcm2835-codec は GPL・BSD の dual の部分を確かめる）、firmware の要件、表示までの buffer の道（HVS の plane に直に出すか V3D の texture か）、HEVC をやるかの判断の材料 | 6 LW | — |
| ws236-p002 | kernel: VCHIQ の driver の核（mailbox、共有の slot、service の open・message・bulk の転送）。WS207 p004 と共用 | 10 LW | p001 |
| ws236-p003 | MMAL の `video_decode` の client で H.264 を decode（port の設定、buffer の受け渡し、EOS・flush・seek） | 10 LW | p002 |
| ws236-p004 | libmedia の backend と Video Player への接続（WS202 の `media_*_ops` の表、software への fallback）、可能なら HVS の plane に直に出す | 6 LW | p003 |
| ws236-p005 | 実機の確かめ（1080p の H.264 の mp4 の再生、CPU の使用率、seek、長時間）と直し | 4 LW | p004 |
| ws236-p006 | （任意）HEVC の rpivid の stateless decoder | 15 LW〜（危険大） | p001 の判断 |
| ws236-p007 | （任意）H.264 の encode（MMAL の `video_encode`） | 6 LW | p003 |
| ws236-p008 | 規約の全文の確認 | 2 LW | 全て |

合計: H.264 の decode だけ（p001〜p005・p008）で約 38 LW（実時間で約 10〜16 時間）。HEVC を足すと約 53 LW〜、encode を足すとさらに +6 LW。WS207 p004 と VCHIQ を共用すれば、後に来る方の p002 の分が減る。
