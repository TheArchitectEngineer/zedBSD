<!-- awesome-plan project=zedbsd record=ws141-p013 -->

# ws141-p013: VC6 の Keiland の描画の速さ（frame 33 ms → 16 ms）

Parent: [WS141](../ws.md)
Status: in-progress（2026-10-18 Q1: ws.md の表に記録済みの判定に合わせた。表: in-progress（hardware blend・compiler の NOP 削減は実機で見た目 OK・速い、tile buffer の f16 は実機待ち））（旧: planned）
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

## 修正 14: kernel の shader compiler の dual issue・NOP の削減・並べ替え（2026-10-12 P3、設計）

承認: 2026-10-12 ユーザー「進め方はおすすめでいいです」（残りの描画の最適化）、Q1 の割り当て（ws211-p011 §2.5 の残りの提案）。範囲は kernel の bcm2711 の shader compiler（`qpu.c/h`・`shader*.c/h`）だけ。display・gpu.c（P2 の WS214）、HAL の API、toolchain、Vulkan の wire は変えない。Mesa の compiler の source は写さず、hardware の事実（命令の形式・待ちの規則）は MIT の Mesa の decoder・validator の記述と公開の文書から取り、自分の模擬で確かめる。

### 今（main 40caa547d の source、host の probe）

- 1 つの source の演算が 1 語。add と mul の ALU は片方しか使わない。uniform の読み込み（ldunifrf）・varying・ldtmu・thread switch も 1 語ずつ。
- 待ちの NOP: SFU（recip・rsqrt）の後に 2、null の宛先（m6）への flag の push の後に 2（「VPM の transaction」と扱っていたため、不要）、thread switch の delay slot 2 つ、varying の 1、終わりの last-thrsw の組の後と終わりの thrsw の後。
- 出力: last-thrsw の組の後に色を 4 つの register へ mov し、2 つの vfpack で TLB へ書き、終わりの thrsw と NOP 2 つ。
- 数（fragment、語/純粋な NOP/ALU 演算/uniform）: glass（panel 0）178/17/119/32、dark glass（7）202/19/138/35、blur（6）154/13/72/39、shadow（1）92/11/58/18、image（2）91/11/55/13、ring（4）87/9/57/16、solid（3）81/9/52/15、text（5）47/5/24/8、quad 29/5/12/2、batch 93/18/57/4。全 shader の表は確かめの節。

### 方針

lowering は今のまま 1 演算ずつ出すが、語に encode せず「演算の記録」（1 ALU 演算か 1 signal、register は thread の窓へ写した番号、消費する uniform）に溜める。fragment の program は、溜めた記録から依存の DAG を作り、list scheduling で 1 語に add の演算・mul の演算・signal を 1 つずつ組む。NOP の記録は捨て、必要な待ちは依存の最小距離で表す。結果は compiler の中の検証器（値の版の模擬）で確かめ、通らなければ今と同じ逐次の出力（記録ごとに 1 語、NOP 込み）に戻す。vertex（coordinate・vertex）の program は逐次の出力（VPM の規則を広げない。1 draw の頂点は少ない）。

### 1 語に組める条件（hardware の事実と、安全側の自分の規則）

- 枠: add の ALU に 1 演算、mul の ALU に 1 演算、signal 1 つ。mov は両方の ALU で行える（mul の mov、add では `or x, x`、どちらも bit を保つ）。fmul・fmov（unpack）は mul だけ、他は add だけ。
- 読み出しの port: 物理 register の read port は A と B の 2 つ。2 つの演算の source の異なる register は合わせて 2 つまで、同じ register は port を共有する。small immediate は port B を使うので、使えば異なる register は 1 つまで、small immediate の値は 1 つだけ。fmin/fmax/fadd は mux の順で opcode を区別するので、port を割った後に順を合わせる（可換なので入れ替える）。
- signal と small immediate: small immediate と組める signal は無し・ldvary・ldtmu だけ（signal 15・14・31）。宛先付きの signal（ldunifrf・ldtmu・ldvary）は cond の field を宛先に使うので、その語は predicate と flag の push を持てない。
- cond の field: 片方の ALU の predicate か push、add の predicate＋mul の push、mul の predicate＋add の push、両方の predicate は表せる。両方の push は不可。
- peripheral は 1 語に 1 つ: TMU の書き込み・ldtmu・wrtmuc・SFU・TLB の書き込み（validator の「Only one of [TMU, SFU, TSY, TLB read, VPM]」と scheduler の peripheral の規則）。uniform の消費（ldunifrf・wrtmuc）も 1 語に 1 つ。
- 待ち（依存の最小距離、語の数）:
  - ALU・signal（ldunifrf・ldtmu・ldvary の rf）の結果: 次の語から読める（RAW 1）。
  - SFU（recip・rsqrt・exp・log・sin）の結果: 3 語後から読む（今の NOP 2 と同じ）。SFU の後の 2 語は宛先を読み書きしない、他の SFU を始めない（SFU を program の順の鎖にし距離 3）。
  - ldvary の C 係数（r5）: 2 語後から読む（RAW 2）。
  - flag の push: 次の語の predicate から（RAW 1）。null 宛先（m6）は peripheral ではなく、NOP は要らない。
  - 書き込みの順（WAW）: 着地が program の順になる距離（ALU 後の SFU は 1、SFU 後の ALU は 3）。
  - 読んだ後の書き込み（WAR）: ALU の書き込みは同じ語でよい（読みは語の始め、書き込みは終わり）。signal の書き込みと flag は安全側で次の語から。
- 順序の鎖（program の順、距離 1）: TMU の書き込み・wrtmuc・ldtmu・sample の thrsw・last-thrsw の組・TLB の書き込み。sample の thrsw の後の ldtmu は 3（switch の後）。last-thrsw は 2 語続けて置き、TLB の書き込みは 2 つ目の 3 語後から（scoreboard の lock の後）。
- thread switch をまたげない演算（switch で失われる・別 thread に着く物）: flag の読み書き、r5 の読み書き（ldvary）、SFU。program の順で switch の前の物は thrsw の語より前、後の物は switch の 3 語後から（各 thrsw への辺）。よって thrsw の語と delay slot 2 つには、これら以外の ALU 演算と ldunifrf だけが入る（validator の「SFU・ldvary は delay slot に置かない」）。rf は thread ごとに残る。
- 終わり（thrend）: 終わりの thrsw は last-thrsw の組の 2 つ目から 3 語以上後。thrsw の語と後の 2 語は rf を書かない（ALU の非 magic の宛先、宛先付き signal）、rf0〜2 を読まない、ldvary・SFU を置かない、後の 2 語は uniform を消費しない。TLB の書き込み（vfpack m7）はこの 3 語に入れてよい。

### 並べ方

1. 本体: last-thrsw の組・TLB の書き込み・終わりの thrsw を除く記録を list scheduling（優先度は sink までの最長の距離、同じなら program の順）。語ごとに、依存の満ちた記録を優先度の順に試し、枠・port・cond・signal・peripheral の条件を encoder で試して入る物を入れる。何も入らない語は NOP。
2. 終わり: last-thrsw の組の位置 p を、hard な依存（ldtmu・switch をまたげない演算）を満たす範囲で試し、各 p で TLB の書き込みを p+4 以降の最初の入る語へ置き、終わりの thrsw を規則を満たす最初の語に置く。全体の語数が最小の p のうち最も後の物を選ぶ（scoreboard の lock を遅く取る）。
3. uniform の列は語の順で作り直す（uniform は記録に付いているので、並べ替えても対応は保たれる）。

### register の選び方

- 式の register は最も前に空いた物から（LRU）。直後に同じ register を使い回すと WAR・WAW の偽の依存で並べ替えが止まるため。同時に生きる値の数は変わらないので、4 thread に入るかは変わらない。
- 全幅の定数（uniform）の materialize と sqrt・fract・fneg・fabs の作業 register は、空きが 4 より多い時は空いた式の register を一時に使い（source の命令ごとに返す）、無ければ今の scratch（rf28〜31）。scratch の使い回しの鎖を切るため。sample の結果 4 つの分は残す。
- 出力: 色の mov をやめ、vfpack が出力の register を直接読む（定数の出力は materialize、異なる small immediate 2 つは片方を register へ）。

### 正しさの確かめ

- compiler の中の検証器: 並べた語を順に模擬し、各演算の各 source が program の順の最後の書き手の値（版）を見ること、書き込みの着地が program の順であること、上の待ち・peripheral・switch・thrend の規則を確かめる。通らなければ逐次の出力。
- host: `plan/ws141/tests/qpu-machine.c`（新規、試験用）: Mesa の decoder で語を読み、add と mul を同じ語で同時に（読みは語の始め、書き込みは着地の時）実行し、SFU・r5・flag の着地の時刻、着地前の読み（assert）、thread switch（accumulator・flag を壊す、ldtmu・TLB の時期）、thrend の規則、peripheral の数を模擬する。
  - `shader-host-test.sh`: 全部の Keiland の shader と variant を、SPIR-V の解釈（sqrt は native と同じ式）と模擬で、ランダムの入力の多数の試行で比べる。app の shader（compile できる物全部）も。命令数の表を出す。
  - `glass-shader-host-test.sh`: glass（light・dark・generic）の SPIR-V の解釈と、compile した native の模擬の結果の一致を、同じ格子（3,856,896 fragment）で。
  - `qpu-format-host-test.sh`: 組んだ語（両 ALU・cond の組み合わせ・port の共有・signal と small immediate）を Mesa の oracle で decode し、意味と repack の一致。
- Pi（焼き直しの後、BUG-294）: 見た目が同じこと、`v3d: gpu` の cycles/quad・qvalid/quad が下がること。

### 実装（2026-10-12 P3、agent/p3 eaad6484e〜f7cabf7c9）

- `qpu.c/h`: `bcm2711_qpu_encode_bundle`（add・mul・signal・thread switch を 1 語に、read port の共有、small immediate、cond の組み合わせ、switch と ldunif/ldunifrf/ldtmu/ldvary/wrtmuc の合成 signal）。1 演算の `bcm2711_qpu_encode` はこれを使う。`bcm2711_qpu_operation_unit`・`_sources`。
- `shader.c`・`shader-private.h`: 演算を記録（register は thread の窓へ写し済み、消費する uniform を記録に付ける）し、`bcm2711_shader_emit` で fragment は schedule、通らなければ・vertex は逐次（従来どおりの語）。register の LRU、一時の scratch の借用（空きが 4 より多い時）、全幅の定数を register に残して再利用（空きが 12 より多い時、要る時に手放す）。`binary->scheduled`。
- `shader-lower.c`・`shader-output.c`: 一時の register の借用、thread switch の役割（sample・last の組・end）、出力の色の mov を廃止（vfpack が出力を直接読む）。
- `shader-schedule.c`（新規）: 記録の分類、依存の辺（RAW・WAW・WAR、SFU の鎖、TMU/switch/TLB の順序の鎖、switch をまたげない演算）、優先度（sink までの最長距離）の list scheduling、sample の switch を launch の 2 語前まで前へ（launch が delay slot に入る）、last-thrsw の組・TLB・thread end の最短の配置、短命の値の accumulator r0〜r3 への移動（registers のみと、寿命 4・12・24・1024 記録の 5 通りを試し最短を採る）、値の版の模擬による検証器、語と uniform の列の出力。
- 事実の確かめで規則を変えた所（設計の節からの変更）: TLB の書き込みと thread end は last の組の **1 つ目**の switch の 3 語後から（Mesa の scoreboard の規則、2 つ目は印にすぎない）。r5 は 1 語遅れて着地するので、次の ldvary は前の r5 の読み手と同じ語でよい。wrtmuc と TMU の座標の書き込み（TMUC 以外）は 1 語に組める（4.2 の peripheral の例外）。switch の前の accumulator・flag の作業は switch の語と delay slot に置いてよい（switch が効くのは delay slot の後。SFU と ldvary は前の語のまま）。
- `platform/arm64/vmunix.mk` に `shader-schedule.c` を 1 行追加。`plan/ws210/tests/spike/count.sh`（P1 の WS210）は link の source の列に `shader-schedule.c` が要る（Q1 へ連絡済み）。

### 命令数（fragment、host、語/純粋な NOP の語/両 ALU を組んだ語）

| shader | 前（40caa547d） | 後（f7cabf7c9） | 減 |
| --- | --- | --- | --- |
| glass（panel 0） | 178 / 17 / 0 | 93 / 2 / 25 | −48% |
| dark glass（panel 7） | 202 / 19 / 0 | 106 / 2 / 32 | −48% |
| blur（panel 6、5 tap） | 154 / 13 / 0 | 70 / 1 / 5 | −55% |
| shadow（panel 1） | 92 / 11 / 0 | 51 / 1 / 5 | −45% |
| image（panel 2、窓の絵） | 91 / 11 / 0 | 51 / 3 / 6 | −44% |
| solid（panel 3） | 81 / 9 / 0 | 46 / 1 / 4 | −43% |
| ring（panel 4） | 87 / 9 / 0 | 50 / 1 / 5 | −43% |
| text（panel 5） | 47 / 5 / 0 | 23 / 1 / 0 | −51% |
| quad（壁紙・client の絵） | 29 / 5 / 0 | 17 / 2 / 0 | −41% |
| batch | 93 / 18 / 0 | 51 / 4 / 8 | −45% |
| generic panel（2 thread） | 611 / 95 / 0 | 293 / 5 / 84 | −52% |
| app: keiland ui・files・imageview・vkdemo | 29 | 17 | −41% |
| app: monitor shape（2 thread） | 411 | 203 | −51% |
| app: monitor glyph | 45 | 27 | −40% |
| app: notes fill / texture | 51 / 48 | 28 / 27 | −45% / −44% |
| app: terminal | 65 | 41 | −37% |
| app: egltest | 62 | 33 | −47% |
| app: mview | 43 | 19 | −56% |

- uniform（ldunifrf）の数も減る（glass 32→29、blur 39→34、monitor shape 21→10）: 定数の再利用。thread 数（4/2）はどれも変わらない。vertex の program は逐次のまま（語数は前と同じ）。
- 見込み: ws211-p011 §2.1 の「費用 ≒ 層の画素 × 命令数 / 32」（QPU の演算で詰まる）なら、窓の中の draw の cycles/tile が約 4〜5 割減る。実機の値は未測定。

### 確かめ（host、2026-10-12 P3）

- `plan/ws141/tests/shader-host-test.sh`（書き直し）: tree の全 shader（Keiland の quad・panel の generic と 8 variant・batch、app の 13 組、R/B の入れ替えの両方、coordinate・vertex も）を compile し、新規の `plan/ws141/tests/qpu-machine.c`（Mesa の decoder で語を読み、両 ALU を語の始めに読んで着地の時刻に書く、SFU は 2 語後・r5 は 1 語後に着地、着地前の読み・飛行中の書き込みの重なり・switch をまたぐ accumulator と flag・switch の窓の SFU/ldvary/ldtmu・lookup の無い switch・scoreboard 前の TLB・thrend の窓の rf の書き込みと rf0〜2 の読み・peripheral の数・SFU の間隔を失敗にする 1 lane の模擬）で走らせ、SPIR-V の解釈（sqrt は native と同じ x×rsqrt(x)）と tile の half の **bit の一致**。Keiland は決定的な入力 32 試行＋乱数 96 試行、app は乱数 96 試行（ゴミで埋めた register、varying は値をそのまま運ぶ形）。PASS。命令数の表もここで出る。
- `plan/ws211/tests/glass-shader-host-test.sh`: 3,856,896 fragment の格子で、glass（light・dark・generic）の native の program を模擬し、SPIR-V の解釈と全 channel が bit 一致。PASS（2 分半）。
- `plan/ws141/tests/qpu-format-host-test.sh`: 組んだ語（port の共有、3 つ目の register・異なる small immediate の拒否、add の mov の OR、add/mul の predicate と push の全組み合わせ、両方の push の拒否、switch と 6 種の signal の合成、small immediate や TILE との合成の拒否、ALU の取り違えの拒否）を Mesa の oracle で decode・repack。PASS。
- 故意の誤り（scheduler の内部の検証器を外して）: SFU の待ち・ldtmu の待ち・r5 の待ち・switch をまたぐ演算・WAR・WAW・scoreboard 前の TLB・thrend の窓・SFU の間隔・accumulator の segment・sample の switch の 3 語前の置き・TLB を 1 つ目の switch の 2 語後・accumulator の WAR、を 1 つずつ入れて模擬が全部検出。「SFU を switch の窓に」「switch の前の作業の下限を 1 語緩める」の 2 つは今の shader では起きず未検出（検証器の規則とは別に模擬の規則はある）。内部の検証器も WAR・switch・TLB・SFU の鎖の誤りで逐次へ戻すことを確認（着地の時刻の表は scheduler と共有なので、その誤りは host の模擬だけが見つける）。
- `vulkan-device-host-test.sh`・`v3d-hardware-host-test.sh` PASS。rpi4 の vmunix（`make -j16 -o …/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/vmunix`）warning 0、`build/arm64/vmunix` cksum 1265685035 8163328。
- `plan/tools/style-check.py`: 変えた kernel の file と試験の file で指摘 0。

### 未実施・Pi で見ること

- 実機（Pi 4）は未確認（焼き直し待ち、BUG-294）。host の模擬は hardware の事実（Mesa の validator・scheduler に記された規則と公開の命令の形式）に基づくが、実機の timing の未知の規則があれば画面の崩れ・GPU の hang として出る。
- Pi で見る点: (1) 起動して Keiland の描画が前と同じ（glass の色・blur・影・窓の絵・文字・cursor・dark の外観、app の canvas）。(2) GPU の hang・`v3d:` の error の行が無い。(3) `v3d: gpu` の draw の cycles/tile、cycles/quad、qvalid/quad が下がる（見込み 4〜5 割）。(4) KWL LAT の input→shown。
- 崩れた時の切り分け: 変える所は fragment の compiler だけなので、vmunix を前の版に戻せば元に戻る。

## 修正 15: 共通の IR の上の CSE・DCE・定数の畳み込み・簡約（2026-10-12 P3）

承認: 2026-10-12 ユーザー「進め方はおすすめでいいです」（残りの描画の最適化）、Q1 の割り当て（修正 14 の続き）。2026-10-12 Q1 の指示で、pass は bcm2711 の中ではなく共通の IR（`src/drivers/gpu/compiler/`）の上に置き、bcm2711 だけの物（register・accumulator・dual issue）は bcm2711 の中のまま。Mesa の NIR は写していない（規則と構造は自分で書き、ir.h の意味と模擬で確かめた）。

### 実装

- `src/drivers/gpu/compiler/optimize.c`・`optimize.h`（新規）: `drv_gpu_shader_optimize(ir, options, optimized)`。parse の後の IR の private な写しを書き換え、命令の列だけを新しく持つ（他の列は ir と共有、呼び手が kern_free）。扱えない program（表に無い演算、loop・値の再定義、上限超え）は手を付けず、呼び手は元の IR を lower する。失敗は ENOMEM だけ。
  - 共通部分式（CSE）: 純粋な演算（定数・LOAD_PUSH・LOAD_UBO・LOAD_INPUT・SAMPLE・算術・比較・論理）を hash で引き、同じ class・同じ意味の field・同じ source の先の結果を使う。定数は CONST/ICONST/BOOL を bit で 1 つの class に。可換は FEQ・FNEU と整数・bit・Boolean の演算だけ（FADD・FMUL・FMIN・FMAX は NaN が 2 つの時にどちらの payload を返すかが順で変わるので可換に数えない。host の試験で FMUL の入れ替えが NaN の payload を変えるのを確かめて外した）。
  - 死んだ code（DCE）: 後ろから、backend が要る出力の slot の最後の store を根に。後の store に上書きされる store と、要らない slot の store を消す。slot の外（point size などの builtin、component ≥ 4）の store は残す（backend の拒否がそのまま立つ）。
  - 畳み込みと簡約（`simplify` の時だけ）: bit・整数・Boolean の演算（NOT(NOT x)、x&~0、x&0、x|0、x^0、x^x、x+0、x−0、x−x、shift の 0 と mod 32、定数どうし）、SELECT の定数の条件と同じ両腕、FNEG・FABS（符号 bit、FNEG(FNEG x)、FABS(FNEG x)、FABS(FABS x)）、正確な I2F/U2F（有効 bit が 24 以内）、普通の値（NaN・denormal でない、FMIN/FMAX は ±0 の組を除く）の FMIN・FMAX・比較。浮動小数の算術の定数どうしは畳まない（kernel に浮動小数が無く、target の丸めを確かめられない。tree の shader には定数どうしの算術は無い: glslang が畳んでいる）。
  - 浮動小数の恒等式: x*1.0、x+(−0.0)、x−(+0.0) を x に。結果を読むのが全部 FADD・FSUB・FMUL の時だけ（それらは x を同じ入力の扱い（denormal の flush・signalling NaN の quiet）で読むので区別できない）。出力・比較・bit の move・texture の座標・特殊関数が読む時は残す。x+0（−0 の扱い）、x*0（NaN）、結合則の並べ替え、FMAX(x,x)（signalling NaN を quiet する）はしない。
  - skip の block と texture の guard: `runs_every_block` の backend（bcm2711: 全 block を走らせ全 channel を sample する）では印と guard を落とす。そうでない backend（i915）では、印か guard のある program は手を付けない（skip されうる block をまたいで値を共有すると、block が計算しなかった値を読むため）。
- `src/drivers/gpu/bcm2711/shader.c`: parse の後に `optimize_options`（fragment: 全 slot・simplify、coordinate: position、vertex: position＋key の varying、どちらも simplify なし＝ CSE・DCE だけ）で pass を通し、4→2 thread の試み（`compile_stage` に分けた）を書き換えた program で行う。書き換えた program を native の compiler が ENOMEM 以外で拒否したら元の IR で compile し直す（元の結果と diagnostic を保つ）。`binary->optimized`（`shader.h`）で、書き換えた program から lower した物かを示す。
- 一覧: `platform/arm64/vmunix.mk`（compiler/optimize.c）、`plan/ws210/tests/spike/count.sh`、`plan/ws141/tests/shader-host-test.sh`・`vulkan-sources.sh`、`plan/ws211/tests/glass-shader-host-test.sh`。

### i915 では使わない（今は）理由

- i915 の compiler は入出力の interface を命令から読む（`i915_compile_interface`: LOAD_INPUT の列が fragment の入力・payload の配置、STORE_OUTPUT が varying）。DCE が読まれない LOAD_INPUT を消すと fragment の入力の列（SBE の配置）が変わる。設計上は許される（「一部を任意の順で読んでよい」）が、i915 の側の見直しと 5330 の実機・QEMU の確かめ（T1）が要り、VC6 の Phase の範囲の外。
- i915 は skip の block と texture の guard を性能に使う（ws075-p021・p023）。pass はそれらのある program に手を付けない作りなので、効く program は限られ、効果は測っていない。
- 繋ぐのは小さい: `render/pipeline-prepare.c` の `i915_pipeline_compile_stage` で parse の後に `options = {runs_every_block = 0, simplify = 1, needed = 全 slot}` で通し、失敗時は元の IR で compile。加えて `platform/amd64/vmunix.mk` と `plan/ws031/tests/run-vk-host-tests.sh` の executor の一覧に `compiler/optimize.c`。i915 の担当（WS075）の後続として提案する。

### 命令数（host、語、前 = 修正 14 の f7cabf7c9 の上の main bed49895e、後 = 修正 15）

| shader | 前 | 後 | 減 | uniform |
| --- | --- | --- | --- | --- |
| glass（panel 0、frag） | 93 | 85 | −9% | 29→25 |
| dark glass（panel 7、frag） | 106 | 98 | −8% | 32→28 |
| generic panel（frag、2 thread） | 293 | 254 | −13% | 104→71 |
| blur（panel 6） | 70 | 69 | −1% | 34→32 |
| shadow・image・solid・ring・text（panel 1〜5） | 51・51・46・50・23 | 同じ | 0% | 1 ずつ減 |
| monitor shape（frag、2 thread） | 203 | 175 | −14% | 10 |
| terminal・egltest・notes fill（frag） | 41・33・28 | 39・32・27 | −3〜5% | |
| quad・batch・ui・files・imageview・mview（frag） | 17・51・17・17・17・19 | 同じ | 0% | |
| panel の coordinate | 85 | 63 | −26% | 22→14 |
| panel の vertex（mode 0・1・3・4・7） | 90 | 76 | −16% | 24→18 |
| quad の coordinate・vertex | 75・80 | 63・78 | −16%・−2% | |
| mview の coordinate | 166 | 115 | −31% | |
| libGL 1・8 の coordinate | 532・1638 | 147・147 | −72%・−91% | |
| libGL 1・8 の vertex | 561・1667 | 505・1436 | −10%・−14% | |

- thread 数はどれも変わらない（4 は 4、2 は 2）。fragment の減りが小さいのは、修正 14 の scheduler が消えた演算の多くを空いた ALU の枠に既に組んでいたため。glass の IR は 138→119 命令（重複の push の読み・1−t の 3 回・x*1.0 の 6 回・3 つ重なる FMUL）。

### 確かめ（host、2026-10-12 P3）

- `plan/ws141/tests/shader-host-test.sh`: PASS。tree の全 shader・variant・R/B を、qpu-machine.c の模擬と SPIR-V の解釈（書き換え前の IR）で bit 一致（Keiland は決定的 32＋乱数 96、app は乱数 96）。lookup の数の比較は「書き換え後 ≤ 元」に（generic panel は 8 → 6: 重複と読まれない sample）。全 program が `binary->optimized == 1`（fallback を通っていない）を確かめる。足した `verify_optimizer`: 手で組んだ IR（各規則の当たる例と、当たってはいけない近い例: +0 の和、−0 の差、出力が読む x*1.0、FMIN の両順、FSUB の両順、NaN・±0 の FMIN/FMAX・比較、不正確な I2F、mod 32 の shift ほか）を書き換えの前と後で解釈し、4096 回の乱数（±0・±1・quiet NaN・±inf・denormal・整数の bit を含む）で全出力の bit が一致、残る命令の数で解釈に見えない規則（出力が読む恒等式、vertex で簡約しない、FMIN の順）を確かめる。skip の block と guard: `runs_every_block = 0` は手付かず、1 は印・guard・重複が消える。
- `plan/ws211/tests/glass-shader-host-test.sh`: PASS（3,856,896 fragment、light 85・dark 98・generic 254 語、全 channel bit 一致）。lookup の比較を「≤」に（WS211 の試験の 1 行）。
- `plan/ws210/tests/spike/count.sh` PASS、`plan/ws141/tests/vulkan-device-host-test.sh` PASS。
- 故意の誤り（optimize.c を 1 つずつ変えて shader-host-test を走らせる、28 種）: FSUB・FMUL・FMIN・ISUB を可換に、push の offset・UBO の location を無視、恒等式の定数を 0.5 に、+0 の和・−0 の差を消す、読み手の制限を外す、vertex でも簡約、sample の結果の取り違え、DCE が第 1 source だけを生かす、要る slot・最後の store を落とす、skip の印を無視、FABS(FNEG) を copy、定数の FNEG で符号を反転しない、SELECT の腕の取り違え、x&0 を x に、x^x を 1 に、ASR を論理 shift に、不正確な I2F を畳む、U2F を符号付きに、±0 の FMAX を畳む、NaN の比較を畳む、FGE を > に → 27 種を検出。検出しないのは「shift の数を mod 64 に」だけ（host の x86 の shift が 5 bit で mask するので結果が同じ。規則は mod 32 のまま）。
- rpi4 の vmunix（`make -j16 -o …/build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/vmunix`）warning 0、`build/arm64/vmunix` cksum 994222987 8183808。
- `plan/tools/style-check.py`: 新しい・変えた kernel の file と shader-host-test.c で指摘 0（glass-shader-host-test.c の指摘は前からの物で、変えたのは 1 行）。

### 未実施・Pi で見ること

- 実機（Pi 4）は未確認（焼き直しの後）。修正 14 と同じ点（見た目が同じ、`v3d:` の error・hang が無い、`v3d: gpu` の cycles/quad）に加え、vertex の program が短くなった（coordinate −26%）ので bin の段の時間。
- 崩れた時の切り分け: `drv_gpu_shader_optimize` の呼び出しを外す（shader.c の 1 か所）と修正 14 の出力に戻る。
- i915 への接続は上の理由で未実施（WS075 への提案）。
