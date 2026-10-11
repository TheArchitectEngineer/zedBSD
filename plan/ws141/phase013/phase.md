<!-- awesome-plan project=zedbsd record=ws141-p013 -->

# ws141-p013: VC6 の Keiland の描画の速さ（frame 33 ms → 16 ms）

Parent: [WS141](../ws.md)
Status: planned
Phase disposition: normal
Queue: q936（[P3 の lane](../../agents/P3/queue.md)）
Base: codex/rpi4-gpu-init d27925686（Codex の最後の commit、scene の cache と damage の調整、main に未統合）

## 由来・承認

2026-10-11 ユーザー:「P3はRaspberry Pi 4のVC6ドライバの改善です。ssh kei@10.0.30.2で接続できます。パスワードはkeiです。現在、マウス移動に300ms以上かかっており、再描画範囲や、コマンドの組み立て方を調整しているところでした。これを引き継いで、30fpsの33msが1つめの目標、60fsの16ms以下が最終目標です。」

## 範囲と条件

- Pi 4（10.0.30.2）の Keiland で、マウスの移動の 1 frame を 33 ms 以下（第 1 の目標）、16 ms 以下（最終の目標）にする。測り方（compositor の frame の時間の log、ユーザーの体感）を最初に決めて記録する。
- 範囲: WS141 の V3D の driver（kernel）・libvulkan の V3D の経路・compositor の damage と合成の組み立て。compositor の変更は全 platform の動きを変えないこと（i915・Venus の回帰は T1）。
- HAL の API・toolchain は変えない。GPL の参照の扱いは Guardrail の WS141 の節。
- Pi 4 の kernel の入れ替え・再起動はこの Phase の範囲で P3 が行ってよい（ユーザーの指示による SSH の接続）。

## 測定の方法（2026-10-11 P3、Q1 了承）

- **compositor**: `--log-frames`（Pi の `/etc/keiland/session` に足し、出力は `/tmp/kwl.log`）。frame ごとの `KWL LAT draw/acquired/recorded/queued/submit/shown`（`queued` は P3 が足した vkQueueSubmit の直後の時刻）と `KWL SCENE reuse=`。[kwl-latency.py](../tests/kwl-latency.py) で input→draw、record、vkQueueSubmit、vkQueuePresentKHR、fence、input→shown を scene の再利用の有無に分けて集計する。zedBSD の CLOCK_MONOTONIC は 1 ms 刻み。
- **kernel**: 2 秒ごとの集計行 `v3d: perf ...`（submission の数、pass の種類 render/clear/image、draw 数、staged KiB、submission あたり平均の total・prepare（tex・copy・prog・rec・alloc・map・clean）・exec（bin・render・oclean）・rel・xfer（CPU の buffer copy）と最大）。従来の「250 ms 以上の submit」「100 ms 以上の CL」「33 ms 以上の prepare」の行はこれに置き換えた（prepare の失敗の行は残す）。時刻は scheduler の 1 ms tick の和で、多数の区間の和として平均を推定する。`/var/log/kernel.log` を SSH で読む。serial/console は使わない。
- 合否の値は「マウスを動かしている間の input→shown」（第 1 目標 33 ms、最終 16 ms）。

## 最初の測定（2026-10-11、マウス入力なし、待機中の再描画）

kernel 2274404613/8077312（計測版）・compositor 3165096962/953832（Codex の scene cache＋queued 行）。44 frame: draw→shown 中央値 490 ms。vkQueueSubmit（recorded→queued、kernel の実行が同期で入る）中央値 380 ms、vkQueuePresentKHR（queued→submit）中央値 90 ms（最小 45）。kernel: submission あたり平均 116〜246 ms、うち texture の CPU の raster→UIF 変換 52〜94 ms、GPU render 30〜66 ms、release 14〜74 ms、CPU buffer copy 11〜26 ms。frame ごとに render pass 1 つと present の image copy 1 つ（libvulkan の present が swapchain の画像を scanout 用の画像へ vkCmdCopyImage し、bcm2711 ではそれが全画面の texture 変換＋全画面の描画だった）。

## 修正 1: 全画面の vkCmdCopyImage を TLB copy に（2026-10-11、実機未確認）

同じ大きさ・同じ形式の raster image の全体の copy は、shader と texture を使わず、tile buffer へ source を load して destination へ store する draw 0 の pass にした（`native-pass` に copy の source を足し、`vulkan-native-image.c` に `copy_whole`/`prepare_copy`）。それ以外の copy・blit は従来の texture quad。host: native-pass-host（Mesa XML oracle、copy の load/store の packet と拒否の条件を追加）PASS、vulkan-device-host PASS、arm64 kernel warning 0（3398207561/8081408）。実機へ配置して reboot したが、Pi が戻らず（ping 不通）、ユーザーの電源の確認待ち。

## 実機の結果 1: TLB copy（2026-10-11）

kernel 366458516（fa59e5c8e＋flip の集計）: 待機中の再描画で vkQueuePresentKHR 中央値 90ms（最小 45）→ 7ms、draw→shown 中央値 490 → 263ms。flip の内訳は vblank 待ち 8.5〜11ms、firmware の core clock の上げ 1ms・下げ 1.5〜2.3ms（毎 flip）。

## 実機の結果 2: マウスの測定（2026-10-11、ユーザーが desktop 20 秒＋窓の中 20 秒）

kernel 1595355601（TLB copy、busy%）・compositor 1093875040（main＋Codex の scene cache＋queued 行）。426 frame（log は worktree の build/p3-logs/kwl-mouse1.log、kernel-mouse1.log、commit しない）。
- cursor だけの frame（scene の再利用）357: input→shown 中央値 48ms・p90 67ms（input→draw 11、record 3、vkQueueSubmit 10、present 17、fence 4）。ユーザーの所見「窓の中は 100ms くらいで相対的にスムーズ」。
- 全面の再合成 69: draw→shown 中央値 916ms。kernel は submission あたり（present の copy を含む 2 つの平均で）release 173〜182ms、texture の CPU 変換 95〜180ms、GPU render 94ms、scene の取り込みの CPU copy 30〜49ms、map 25ms、alloc 16〜24ms。busy は 84〜94%。
- 窓の外が数秒になる理由: pointer が画面の端（左右 16px・下 48px・上の bar）や窓の枠・title bar に入ると compositor は dirty（全面の再合成）にし、swapchain が 3 枚なので buffer age の規則で続く 2 frame も全面になる → 1 回の dirty で約 3×0.9 秒。ユーザーの所見「ウィンドウ外では数秒」。

## 修正 2〜5（2026-10-11、実機は未確認）

- TFU（814a6c96e まで）: texture の raster→UIF を V3D の TFU で行う版は、実機で描画が極端に遅く操作不能になった（log は焼き直しで消え、原因は未確定）。起動時の V9 に UIF の自己試験（96x40 の CPU 変換との一致と 256x256 の時間）を足し、結果は log に出すだけで TFU は使わない（4502ffbd9）。
- 画像全体の buffer↔image copy も TLB copy に（vulkan-native-image.c `bcm2711_vulkan_native_buffer_image_create`）。compositor は buffer age だけで全面になる frame も scene cache から全面を復元し cursor だけ描く（compose.c）。814a6c96e。この版で reboot した後 Pi が戻らなかった（原因未確定、reboot の不具合 BUG-288 の可能性もある）。
- flip ごとの firmware clock の上げ下げを、変化が無い時は送らない・合成中は下げない。NC の frame の cache clean を省く（c2e17c674）。display host 試験 PASS（合成中の clock の期待値を 500MHz に直した）。
- private upload の view の再利用の cache（native-storage.c、最大 96MiB、1 view 16MiB まで、4KB と それ以上の 2 本の list、recovery の前に drain）。frame ごとの alloc・map・MMU flush・release を減らす。vulkan-device host に cache の試験を追加して PASS（b53da0069、main 未統合）。
- 以後は Pi で reboot しない（ユーザーが手で電源を入れ直す）。main の 42b5bdbea 以降で image を作り直す。
