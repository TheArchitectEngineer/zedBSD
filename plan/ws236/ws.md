<!-- awesome-plan project=zedbsd record=ws236 -->

# WS236: Raspberry Pi 4（VideoCore VI）の動画の再生の hardware 支援

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。2026-10-15 ユーザー「1080pは要件から外します。」「720p 30fpsがこのマシンの限界値です。」でゴールを 720p30 にし、T1-690 で 720p30 の shown 289/300。VCHIQ・VIDEO は既定で on）
<!-- retained-after-completion --> tests/ の 4 項目 はその場に残す: src/・userland/・platform/・Makefile・config/ の source か build の file が path を名指す（AGENTS.md §10 の source の comment の掃除の後に消す。2026-10-28 Q1 の方針、q1660）
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: MG002
Parent: [Master](../master.md)
Related: [WS141](../ws141/ws.md)（VC6 の display と V3D）、[WS202](../ws202/ws.md)（libmedia の H.264・AAC の再生）、[WS207](../ws207/ws.md)（HDMI の音、同じ VCHIQ）
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

Pi 4 の VideoCore の VPU の H.264 の decode（VCHIQ と MMAL の `video_decode`）を、libvulkan の Vulkan Video（`VK_KHR_video_queue`・`VK_KHR_video_decode_queue`・`VK_KHR_video_decode_h264`）として kernel の側の Vulkan の executor に実装し、libmedia の `media_vkvideo_ops` と Video Player で再生する。設計は [design.md](design.md)（review は [review-1.md](review-1.md)）。

## ユーザーの決定

- 2026-10-14（原文）「VC6での動画再生支援に対応するWSを作れますか？見積もりをください。」
- 2026-10-14「VC6では、Vulkan VIdeoのドライバとして実装してほしいです。V3Dと連携しなかったとしても。可能ですか？HEVCはライセンスがないのでやらないです。エンコードは今回は置いておきます。」
- 2026-10-15「最新のカーネルでOKです。まずはビデオ再生をテストしてください。」
- 2026-10-15「Pi4ビデオの最適化、進めてください。Video.mp4を遅れなく再生できることをゴールとします。」（p008）
- 2026-10-15「デコーダの出力は、おそらくVideoCore でテクスチャとして使える形式なのでは？それを特別に利用したらどうですか？」（p009）、判断「案 1 を WS236 の Phase で」（p011）
- 2026-10-15（Q1 の依頼「メモリ帯域を計算してみてください」の後）「1080pは要件から外します。」「720p 30fpsがこのマシンの限界値です。」「Pi4は直接的にデコーダを使うなら1080pもギリギリいけるのでしょうが、Vulkanのバッファにデコードする場合はこれが限界なんです。」
- 2026-10-15「1080pや60fpsはもう計測しなくていいです」
- 2026-10-15（クリック）A3（p012 の direct の swapchain）を「revert する」（720p30 では fps が変わらず合成の 1 frame が延びたため）。
- 2026-10-15: VCHIQ・VIDEO を既定で on（`config/ci/config-rpi4.mk` は y）。

## 結果

- VCHIQ の核（p002、main e4c3fce22、WS207 と共用）、MMAL の `video_decode` の client と R1 の probe（p003）、V3D の Vulkan の executor の video decode（p004）、libmedia・Video Player・vkvideo-probe（p005）は main。
- Pi 4 の実機（main 2597b86）で vkvideo-probe の 3 本の全 28 frame が参照と bit 単位で一致。
- p006 の直し（最初の 1 枚、cached の MMAL の memory、step の decode ほか、49a7e41f2）。1080p30 は約 15 fps で未達（uncleared）。
- p010 の decode の depth（kernel の executor の settling list）: T1-671 で depth 1.4〜1.7、probe 一致（cleared）。
- p011 の案 1（decoder の出力を GPU の texture に、表示を GPU で）: T1-687 PASS（全画面で VPU が止まらない）、T1-688。T1-690（Pi 4 実機、QEMU 不使用）で 720p30 の shown 289/300、1080p30 267/300、60p は約 30 fps。
- p012（A3）はユーザーの決定で revert（canceled）。

## 制限・移管

- 1080p・60 fps は範囲の外（2026-10-15 ユーザー）。
- Pi を main の libvulkan・wayland に戻しての 720p30 の確かめ、seek、長時間の再生は未実施。
- HEVC・encode は対象外。
- decoder の出力を HVS の overlay に直に載せる案 2（p009）は設計だけ。
- p007 の規約の全文は WS の外の一括の見直しへ（2026-10-08 ユーザー「規約の整形（各 WS の全文規約の Phase）はベータ3」、2026-10-27 の精査で一括の見直しに読み替え）。
- 試験の stream と probe の data を作る道具（`make-probe-data.py`・`dump-compare.py` と入力の h264）は plan/tools/ へ移す（Q1 の OK の後）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws236-p001 | 調査と設計（VCHIQ・MMAL・Vulkan Video の橋渡し） | in-progress（第 1 版）のまま閉じた。設計は design.md |
| ws236-p002 | kernel: VCHIQ の核 | in-progress のまま閉じた（main e4c3fce22、実機は p003 以降で動作） |
| ws236-p003 | kernel: MMAL の `video_decode` の client と R1 の probe | in-progress のまま閉じた（実機の probe は全 frame 一致） |
| ws236-p004 | kernel の Vulkan の executor の video decode | in-progress のまま閉じた（実機で再生） |
| ws236-p005 | libmedia・Video Player・vkvideo-probe | in-progress のまま閉じた（実機で再生） |
| ws236-p006 | 実機の確かめと直し（1080p） | uncleared（1080p は約 15 fps、後に 1080p は範囲の外） |
| ws236-p008 | 再生の速さ: Video.mp4 を遅れなく | uncleared（T1-663、1080p で約 4〜5 fps。p010・p011 へ） |
| ws236-p009 | decoder の出力を GPU・HVS で直に使う道の調べと設計 | 設計だけ（案 0 は p010、案 1 は p011、案 2 は未着手） |
| ws236-p010 | decode の depth（案 0） | cleared（T1-671） |
| ws236-p011 | 案 1: decoder の出力を GPU の texture に、表示を GPU で | in-progress のまま閉じた（T1-687・T1-690、720p30 で shown 289/300） |
| ws236-p012 | Pi の WSI の direct の swapchain（A3） | canceled（2026-10-15 ユーザーの決定で revert） |
| ws236-p007 | 規約の全文の確認 | WS の外の一括の見直しへ |

## 記録の所在

Phase の計画・結果・試験は削除する（道具は plan/tools/ へ移す）。git の commit `fb1f6afee` の `plan/ws236/` にある。Queue の履歴は [plan/history](../history/index.md)。
