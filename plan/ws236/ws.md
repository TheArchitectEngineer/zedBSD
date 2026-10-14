<!-- awesome-plan project=zedbsd record=ws236 -->

# WS236: Raspberry Pi 4（VideoCore VI）の動画の再生の hardware 支援

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-14 ユーザー「WS236を実装します。」、P3。code freeze の後だが「完了しなかったらDropが可能」の条件でベータ2 に向けて着手。driver は既定 off で入れ、実機の確かめが金曜 16 日の夕方のリリースまでに済まなければベータ3 へ）
Primary Milestone: MG006
Related Milestones: MG002
Parent: [Master](../master.md)
Related: [WS141](../ws141/ws.md)（VC6 の display と V3D）、[WS202](../ws202/ws.md)（libmedia の H.264・AAC の再生、今は software と i915 の Vulkan Video）、[WS207](../ws207/ws.md) p004（HDMI の音、同じ VCHIQ を使う）
Resume point: p006（P3、2026-10-15、agent/p3-ws236 a7d79ec3e）: frame 0 は start の後の最初の picture を firmware が次の slice まで持つため、非参照の灰色の push picture を送って出す。1〜15 が 1 小さいのは firmware の NV12 の変換で、写す時に補正。遅さは uncached の 3 MB の読み 2 回と executor の塞がりと見て、MMAL の memory を cached・readback を cached の写しから・decode を step 化、計測の行を足した。BUG-314 の理由の log も。host 試験・build（warning 0）済み、T の実機待ち（[phase006](phase006/phase.md) の依頼）。p007 は試験の C の書き方と通しの見直しが残り。
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-14）

「VC6での動画再生支援に対応するWSを作れますか？見積もりをください。」

## 前提（Q1 の理解、2026-10-14 の作成時。決定と設計で置き換わった点は design.md が正）

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
| [ws236-p001](phase001/phase.md) | 調査と設計（[design.md](design.md)、in-progress: 第 1 版、review 待ち）: VCHIQ・MMAL の message、参考の source の license、MMAL の decode の順・遅れの設定、Vulkan Video の stateless の API との橋渡しの設計、VkImage の memory、V3D の device に queue を足すか別の device か | 6 LW | — |
| [ws236-p002](phase002/phase.md) | kernel: VCHIQ の driver の核（WS207 p004 と共用、in-progress: 実装・host 試験済み、main に merge 済み e4c3fce22、実機は p003 の probe と一緒） | 10 LW | p001 |
| [ws236-p003](phase003/phase.md) | kernel: MMAL の `video_decode` の client（H.264、port・buffer・EOS・flush）と R1 の probe（in-progress: 実装・host 試験済み、test-wait（T1 の実機の probe）） | 10 LW | p002 |
| [ws236-p004](phase004/phase.md) | kernel の Vulkan の executor（in-progress: 実装・build 済み、実機は p005 の依頼と一緒）: VC6 の video の device と decode の queue、video session・parameters、decode の command を MMAL へ（NAL の組み直し、DPB の形だけの扱い、順の調整）、libvulkan の device の列挙 | 12 LW | p003 |
| [ws236-p005](phase005/phase.md) | libmedia（`media_vkvideo_ops`）と Video Player での再生、`vkvideo-probe` の試験（in-progress: probe の直し済み、libmedia は無変更、test-wait（T1）） | 3 LW | p004 |
| [ws236-p006](phase006/phase.md) | 実機の確かめ（1080p の H.264 の mp4、CPU の使用率、seek、長時間）と直し（uncleared 2026-10-15: 画は実機で全 frame 一致、1080p は約 15 fps で速さが未達） | 5 LW | p005 |
| [ws236-p008](phase008/phase.md) | 再生の速さ: Video.mp4 を遅れなく（in-progress、P3、2026-10-15 ユーザー「Pi4ビデオの最適化、進めてください。Video.mp4を遅れなく再生できることをゴールとします。」）: VPU へ複数の unit を重ねる pipeline、place・readback の短縮 | 8 LW | p006 |
| [ws236-p009](phase009/phase.md) | decoder の出力を GPU・HVS で直に使う道の調べと設計（設計済み、実装はユーザーの判断待ち、P3、2026-10-15 ユーザー「デコーダの出力は、おそらくVideoCore でテクスチャとして使える形式なのでは？それを特別に利用したらどうですか？」）: 推す順は 案 0（depth 1.0 の直し、libvulkan の私的な fence が decode を 1 枚ずつにしている見当、0.5〜2 LW）→ 案 1（videoplayer が NV12 の image を V3D で sample して描く、R8・RG8 の texture と TFU、20〜31 LW）→ 後で案 2（HVS の overlay に NV12） | 調べ 2 LW | p008 |
| [ws236-p007](phase007/phase.md) | 規約の全文の確認 | 2 LW | 全て |

合計 約 48 LW（実時間で約 12〜20 時間）。p004 の stateful と stateless の橋渡しが一番不確か（+8 LW 程度の振れ）。HEVC・encode は対象外。

## 引き継ぎ（2026-10-15 夜、Q1、サーバの再起動の前）

- p006 の直し（最初の 1 枚を押し出す灰色の I slice、1〜14 の +1、cached の MMAL の memory、step の decode、cost の log、BUG-314 の EIO の log）は main に merge 済み（49a7e41f2）、build・host 試験のみ。
- 実機の試験（T1-653、phase006 の「T への依頼」）は未実施: 新しい kernel で Pi が起動 54 秒で停止（BUG-317）。ユーザーは「最新のカーネルでOKです。まずはビデオ再生をテストしてください。」、Pi の SD を書き直し中。
- 再開: ユーザーの知らせの後、最新の main の CI の kernel（config/ci/config-rpi4.mk）で phase006 の手順 3〜7 を流し、結果をユーザーに伝える。止まる地点: Pi が応答しなくなったら止めてユーザーに聞く。
- 残り: libmedia の uncached の読み（WS202・WS141）、seek、p007（規約）。

## 実機の結果（2026-10-15、Q1）

- Pi 4 の実機（main 2597b86）で、vkvideo-probe の 3 本の全 28 frame が参照と bit 単位で一致（frame 0 も出る）。probe の hash の不一致は probe の tile の判定の不具合で、Q1 が直した。
- Video Player の 1080p30 は約 15 fps（shown 76・late 224）。VPU の frame を 1 枚ずつ約 32 ms 待つのが主因（cached の写しで readback は 2.5 ms に）。次は複数の picture を同時に渡す pipeline（新しい Phase の候補、ユーザーの判断で）。
- 詳細は [phase006](phase006/phase.md) の「実機の結果」。
