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

## 修正 6〜8（2026-10-11、実機未確認、main 未統合）

- 523b18345 kernel: 変換済み texture の snapshot の cache（CPU に一度も map されていない画像だけ、最大 128MiB、LRU、buffer の generation が変わった時だけ再変換、render/display の resource_map で host_mapped にして以後は使わない）。
- 22230e7f4 compositor: CPU で書く画像（shm の窓・矢印・cursor・glass の壁紙・blurred・atlas・tiles・large）を map した LINEAR の staging と map しない OPTIMAL の sample 用の 2 枚にし、書いた所で `kwl_host_image_written`、次の frame の先頭で全体を vkCmdCopyImage。増える memory は 1920x1080 の glass で約 16MB＋shm の窓ごとに窓の大きさ×4 byte。T1-539（Venus・llvmpipe の回帰）を Q1 が依頼済み。
- 60e18a09f kernel: buffer ごとに直近 8 回の書き込みの矩形（generation と image の offset）を記録し、同じ source からの全体の TLB copy は、前回の copy 以後に source が変わった矩形の tile だけを copy する（変化が無ければ copy を省く、perf 行の skipped）。present の swapchain→scanout の copy が cursor の周りの tile だけになる見込み。CPU に map された run と、記録の無い書き込み（RESOURCE_WRITE など）は全体の copy。vulkan-device host に write log の試験を追加して PASS。
- 実機の順（Q1 了承）: 焼き直しの image（main 42b5bdbea）で基準 → b53da0069（storage cache）→ 523b18345＋22230e7f4（snapshot と staging）→ 60e18a09f（incremental copy）。

## 実機の結果 3〜5（2026-10-11）

- B（523b18345 snapshot＋22230e7f4 staging）の image で compositor が起動直後に落ちた。main は kernel の 2 つを戻した。compositor の staging の copy を frame の先頭から submit の時の別の command buffer へ移した（a0b4fb103、frame の途中で作る glyph・cursor の形が copy に間に合わなかった）。
- main の image（kernel 2028059803＝vmunix.good、wayland 3904490197）で 60 秒の操作: 窓のふち・画面のふちで全面の再合成（1 frame 約 0.43 秒）が連続 → shell.c の glass_motion_take の別の dirty 判定が原因、7d318020d で直した。中央の cursor だけの frame は input→shown 中央値 38ms・p90 56ms。
- 7d318020d（1264254195）の実の fps: 動かしている間の cursor だけの frame 613、画面に出た frame の間隔 中央値 35ms（約 28fps）、p10 30ms、p90 59ms。draw→shown 26ms（vkQueueSubmit 7、present 10、fence 3、acquire・record 各 2）＋前の frame の完了から次の draw まで 9ms。
- 診断の起動（snapshot 入り kernel 2714848246＋7d318020d）: compositor は落ちない（B の crash は再現せず、原因は未確定）。snapshot を d4833c730 に再 commit。待機中の再描画でも texture の CPU 変換は submission あたり 25〜88ms 残る。

## 修正 9〜12（2026-10-11、実機未確認）

- aa2659dd3 compositor: 窓のふちで cursor の形が変わる時の dirty（全面の再合成）を cursor の位置の damage に。dock の preview の tile の hover を probe に記録。
- 3d4c6ecea kernel: GPU の job の完了待ちを 1ms の tick の poll から IRQ で worker を起こす形に（v3d-hardware の events_queue）。
- cbfc88b19 kernel: 変化した tile だけの present の copy（60e18a09f）を再適用。
- 93dc73c59・55e8ea3ce・83b8cbd48 kernel（診断だけ）: perf 行に `snap=再利用/変換/map された元/その他`、`mapped_kib`、`largest=WxH<f|m|p>`（最大の変換）、`tiles=render/image`（pass の tile 数）を足す。待機中の変換が何の画像かを数で確かめるため。
- 見直し: client の窓（Wayland の WSI の swapchain の画像）は libvulkan で共有の GPU 専用の allocation で、RESOURCE_MAP されない（host_mapped にならない）と source から読める。「client の窓は host-mapped で snapshot が効かない」という前の見立ては誤りの可能性が高い。client の画像の copy は、上の計数で変換の元を確かめてから作るか決める。

## CPU が画素を触る所（2026-10-12、source の調べ、Pi の ms は未測定）

ユーザーの方針（2026-10-12、Guardrail）: Pi 4 では毎 frame の経路で画素を CPU で触らない。

| 所 | 何 | 頻度 | 大きさ |
| --- | --- | --- | --- |
| bcm2711 `native-texture.c` `bcm2711_native_texture_copy`（`vulkan-native-draw.c` の texture の用意、`native-storage.c` の snapshot） | sample する画像の raster→UIF の CPU 変換。TFU は自己試験が通っても使っていない | sample する画像ごと。snapshot は generation が同じ間は再利用、CPU に map された画像（client の canvas など）は毎 pass | 画像の全体 |
| bcm2711 `vulkan-native-image.c` の copy・blit | 同じ大きさの全体の copy は GPU の TLB copy、それ以外の copy と全ての blit は texture の draw で、元の画像が上の CPU 変換を通る | vkCmdCopyImage・vkCmdBlitImage ごと | 元の画像 |
| bcm2711 `vulkan-buffer-copy.c` | buffer↔image の一部の copy の行の memcpy（scene の cache の部分の取り込み・戻し） | frame ごと（damage の矩形） | damage |
| libkeiland `ui/canvas.c`・`ui/text.c` | client の UI の描画の全部が CPU の software rasterizer（塗り・角丸・影・画像の拡大縮小・文字の合成） | client の再描画ごと | 窓か変化した部分 |
| libkeiland `ui/present.c` | CPU に map した canvas を GPU の quad で sample → map されているので kernel が毎 frame 窓全体を CPU 変換、compositor 側でも commit ごとに窓全体をもう一度変換 | client の frame ごと | 窓 ×2 |
| compositor `shm.c` | wl_shm の行の memcpy、staging→sampled の全体の copy（pitch が 64 byte に揃わなければ CPU 変換） | commit ごと | 窓 |
| compositor `glass.c` | glyph の atlas への追加で 1024x1024 の atlas 全体を書いた印 → 4 MiB の copy と CPU 変換 | 新しい文字ごと | 1024² |
| compositor `backdrop.c` | GPU の blur だが、各 pass の入力（直前の render の出力）が毎回 CPU 変換 | blur のある frame ごと（1/8 の大きさ ×約 5） | 小 |
| compositor `heads.c` の mirror | 出力の全体の blit → 全画面の CPU 変換 | mirror の frame ごと | 全画面 |
| 起動時・壁紙の変更時だけ | 壁紙の拡大縮小・CPU の blur（glass.c）、icon・tile・cursor の形 | 一度 | — |

dock の preview: GPU が窓の画像を小さく sample して描く（CPU の縮小・readback は無い、preview 専用の cache も無い）。窓の画像の変換は snapshot を通常の描画と共有するが、client が commit するたびに窓全体を CPU で変換し直す。

## 性能 counter の結果と hardware blend（2026-10-12、P3）

- 実機（kernel ffb8e5666 の counter 版、libkeiland は P2 の BUG-290 版、ユーザーの 60 秒の操作と Settings の dock→undock。log は worktree の build/p3-logs/boot-pctr/、commit しない）: 866 の render run の平均で 1 run 11.4M cycle（V3D 500MHz で 23ms）、quad 238k → wall で 1 quad あたり 48 cycle。QPU の有効命令の cycle は 1 quad あたり 87（2 slice×4 QPU の容量の約 23%）。TMU の miss は 3%（texture は原因でない）。
- 原因（source と host の確かめ）: (a) kernel の shader compiler の出力の約 7 割が NOP（全命令の後に 2 つ、add と mul を組まない）。host の probe（build/p3-probe/shader-size、commit しない）で quad 75 命令（NOP 49）、blend 付き 120、窓（panel mode 2）288、glass 684、blur 471。(b) blend を shader の中で行い（tile buffer を読んで source-over）、tile の読み書きの区間が scoreboard で順番待ちになる。
- BUG-290: 同じ起動で Settings の dock(1904x1020)→undock(1180x800) は落ちない（Q1 に報告、判定は Q1・P2）。
- Settings の title bar の hover の重さ: client（libkeiland）の窓全体の描き直し。SLOW-FRAME 25 回の平均で draw 102ms、copy 305ms（canvas の行を map した memory へ memcpy、libc の memcpy が 1 byte ずつで canvas は Normal non-cacheable）、vkQueueSubmit 約 180ms。libc は BUG-291（Q1 が P2 へ）。
- 修正 13（ad7ebb2cb、実機未確認）: hardware blend。blend の pipeline の draw は Cfg Bits の Blend enable・Blend Enables=1・Blend Cfg（色と alpha とも ONE / INV_SRC_ALPHA / ADD、RT0）を出し、shader は tile を読まない（software blend の経路と shader の key の field を削除）。draw の bin は 116→121 byte。host: native-state（Mesa XML の oracle で bin の全 byte）・shader・native-pass・clear・vulkan-device PASS、arm64 vmunix warning 0。blend 付きの shader は 45 命令減（quad 120→75、窓 288→243）。Pi の /boot/boot0/vmunix に配置済み（1109485435）、電源の入れ直し待ち。
- 次: (a) compiler の NOP の削減と add/mul の組（bcm2711 の backend だけ、i915 は別の backend で影響なし）。
