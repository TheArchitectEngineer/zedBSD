<!-- awesome-plan project=zedbsd record=ws141-p015 -->

# ws141-p015: GPU が書いて GPU が読む画像を UIF で持つ（設計）

Parent: [WS141](../ws.md)
Status: planning（設計、design-reviewer と Q1・ユーザーの確認の前）
Phase disposition: normal
Queue: 未割当（Q1）

## 由来

- 2026-10-12 ユーザー（Q1 経由、Guardrail に記録）:「PI4のような低消費電力のモバイルSoCでは、画像をCPUで触ると、体験を損ないます。すべてGPUで処理する必要があります。」「GPUでglassをやってほしかったのですが…GPUでできるならそれがいいです。」→ 毎 frame の経路で画素を CPU で触らない。glass の blur・合成は GPU だけ。
- 2026-10-12 Q1: p013 の (A) の設計を ws141-p015 として書く。glass の blur の段の render target、client の swapchain の image（compositor が import して sample する物）を対象に含め、WS210（P1、app の窓を GPU で描く）の G5 と形を合わせる。kernel・libvulkan に及ぶので design-reviewer にかける。

## 問題（p013 の実測と source）

V3D 4.2 の TMU は raster の 2D の画像を sample できない。bcm2711 の driver は今、全部の画像を memory 上 raster で持ち、sample する時に CPU で raster→UIF に変換した写し（`native-texture.c` `bcm2711_native_texture_copy`）を作る。写しは snapshot（`native-storage.c`）として source の generation が変わるまで使い回す。

- lazy map（agent/p3 3b0d9db5c）の後、compositor の sample 用の画像は snapshot が効く（Pi の 2 秒の計数で snap=36/0/0 など）。それでも残る CPU の変換:
  1. GPU が描いて次の pass で sample する画像（glass の blur の段の render target、scene の写し、cursor の plane の像など）は、描くたびに generation が変わるので、sample のたびに変換する（backdrop の 1/8 の画像で 1 frame に約 5 回）。
  2. client の窓: Keiland の app（libkeiland）は CPU で描いた canvas（map した画像）を GPU の quad で swapchain の画像へ描き、compositor がその swapchain の画像を sample する。canvas は map されているので client の frame ごとに窓全体を変換し、swapchain の画像も commit ごとに compositor 側で窓全体を変換する（1904x1020 で 1 回 約 50ms）。WS210 で app を GPU で描いても、swapchain の画像の変換（commit ごとに窓全体）は残る。
  3. 部分の copy（`vulkan-buffer-copy.c` の行の memcpy）。compositor の scene の写しの取り込み・戻し（`scene.c`、buffer↔image の部分）が毎 frame これを使う。
- 1 frame の時間の残りは GPU の render（全画面で 1 pass 約 90ms）で、これは p013 の性能 counter で別に分ける（この Phase の範囲外）。

## 目的と完了の条件

- GPU が書いて GPU が読む画像（`VK_IMAGE_TILING_OPTIMAL` の色の画像）を memory 上 UIF（XOR 無し）で持ち、render の store・load、texture の sample、copy・clear・blit を全部 GPU（TLB・TMU）で行う。これらの画像について、CPU の raster→UIF の変換と行の memcpy が毎 frame の経路から無くなる。
- 完了の条件: (1) Pi の 2 秒の計数で、compositor の frame の submission の `tex`（CPU 変換）と `xfer`（CPU の copy）が、client の canvas（map された LINEAR の画像、次の (4) まで残る）を除いて 0。(2) 起動時の自己試験（下の「安全の門」）が実機で一致を示す。(3) i915・Venus・llvmpipe の挙動が変わらない（T1）。(4) 段 A4（WSI の swapchain の画像の UIF）は UAPI の承認の後の別の段で、その完了で client の窓の commit ごとの変換も 0。

## 範囲

対象: bcm2711 の render device の画像のうち、`VK_IMAGE_TILING_OPTIMAL`、2D、1 level・1 layer、1 sample、`R8G8B8A8_UNORM`・`B8G8R8A8_UNORM`（今の driver が受ける物と同じ）。

対象外（raster のまま）: `VK_IMAGE_TILING_LINEAR` の画像（host が map する staging、display の scanout の画像、Wayland の WSI で今 LINEAR として共有している画像（A4 で扱う））、buffer。

## 設計

### 1. 画像の layout（kernel、`vulkan-resource.c`）

- OPTIMAL の画像は今の texture の写しと同じ strict UIF（XOR 無し、UB の padding 無し）にする: 幅を 32 px の column、高さを 8 px の UIF block の行に切り上げ、`bytes = columns × block_rows × 1024`（`bcm2711_native_texture_size` と同じ式）。資源の記述に `uif`（真偽）と `block_rows`（= UB の高さ）を足し、`pitch` は raster の画像だけの意味にする。
- memory の要求（`required_bytes`・alignment 4096）は UIF の大きさ。`vkGetImageSubresourceLayout` は Vulkan で OPTIMAL には使えないので、kernel は OPTIMAL に対しては 0 を返す（今は raster の値を返している、libvulkan・app が OPTIMAL の rowPitch に頼っていないことを確かめる）。
- CPU は OPTIMAL の画像の画素に触れない（Vulkan で host が読み書きできない）。`RESOURCE_READ/WRITE` の ioctl が OPTIMAL の画像を含む memory に来た場合の扱いは今と同じ（memory の bytes として扱う）。

### 2. render target（`native-pass.c`・`vulkan-native-pass.c`）

- pass の状態に出力の形（raster か UIF）と UB の高さを足す。Store Tile Buffer General（code 29）と Load Tile Buffer General（code 30）の `Memory Format` を 4（UIF No XOR）、`Height in UB or Stride` を UB の高さにする（raster の時は今どおり 0 と byte の stride）。他の field（output image format・R/B swap・decimate・dither）は今と同じ。
- LOAD_OP_LOAD の load、STORE の store、clear（tile の中で clear して store）は全部この 2 つの packet なので、UIF の出力でも同じ流れ。部分の render area は tile 単位なので UIF でも今どおり。
- CPU の clear（`vulkan-native-execute.c` `clear_target`、render area が部分で LOAD_OP_CLEAR の時）は UIF の画像では使わず、tile の clear＋store で行う（部分の area は tile の境に揃わない所があるので、clear 色の quad を描く。CPU の clear は raster の画像だけに残す）。

### 3. sample（`vulkan-native-draw.c` `prepare_texture_image`）

- UIF の画像は変換せず、texture の record にその画像の memory の GPU 側の address を直接書く（`bcm2711_native_texture_encode` の layout と同じなので record の形は変わらない）。draw は画像の view の参照を持ち、pass の終わりまで memory を解放させない（今の snapshot の users と同じ考え）。
- snapshot と private の変換は LINEAR の画像だけに残る。

### 4. copy・blit（`vulkan-native-image.c`・`vulkan-buffer-copy.c`）

- 同じ大きさ・同じ位置の copy（image↔image の全体と、src と dst の offset が同じ部分）は今の TLB copy（load general で source、store general で destination）のまま、source と destination の memory format をそれぞれ raster・UIF から選ぶ。これで LINEAR→OPTIMAL（compositor の staging→sampled、upload）、OPTIMAL→LINEAR（present の copy で display の native image へ）、OPTIMAL→OPTIMAL が全部 GPU になる。
- offset が違う copy と blit は今の texture の draw（source を sample して quad を描く）で、source が UIF なら変換は無い。
- buffer↔image: buffer は raster として TLB の load・store の相手にする（buffer の行の長さを stride にする）。image の全体か、tile の境に揃う部分は TLB で行う。揃わない部分の image→buffer は、tile が buffer の外の bytes を書くので TLB で直接はできない。この場合は GPU で一時の raster の画像へ TLB で写し、そこから buffer へ揃った行の範囲を CPU で memcpy する案と、compositor の側を変える案（次）がある。設計としては後者を選ぶ。
- compositor の scene の写し（`scene.c`）は buffer でなく OPTIMAL の画像に持ち替える（image↔image の同じ位置の部分の copy＝TLB copy）。これで毎 frame の行の memcpy が消える（compositor の変更、全 platform で同じ Vulkan の使い方なので i915・Venus でも動く）。

### 5. 安全の門（起動時の自己試験）

- V9 の TFU の自己試験と同じ所に、TLB の UIF の store の自己試験を足す: 96x40 の小さな絵を raster に描き、(a) それを CPU の `bcm2711_native_texture_copy` で UIF にした物と、(b) TLB copy（raster を load、UIF で store）した物を比べ、全 byte が一致した時だけ OPTIMAL を UIF にする。一致しなければ log に出し、OPTIMAL も raster（今と同じ）のままにする。高さ 40 は 8 の倍数、幅 96 は 32 の倍数でない列を含むように選ぶ（padding の確かめ）。

### 6. WSI の swapchain の画像（段 A4、UAPI の変更の承認が要る）

- 今、Wayland の WSI（libvulkan `wsi-image.c`）は swapchain の画像を LINEAR で作り、`gpu_image_descriptor`（tiling = `GPU_IMAGE_LINEAR`）で compositor に渡す。compositor は LINEAR の画像として import して sample するので、commit ごとに窓全体の CPU 変換が 1 回ある。
- 案: descriptor の tiling に UIF の値（`GPU_IMAGE_UIF`、`include/uapi/gpu-scanout.h` など、UAPI の追加）を足す。WSI は compositor が UIF の import を受けると答えた時（Wayland の GPU buffer の protocol の能力）だけ swapchain の画像を OPTIMAL＝UIF で作り、compositor（`libkeiland-backend-zedbsd/gpu-buffer-zedbsd.c`）は同じ create info の OPTIMAL の画像として import する（同じ driver・同じ device の opaque な import で layout が一致する）。display に直接出す画像（game mode の scanout）は HVS が UIF を読めないので LINEAR のまま（compositor が scanout に回す時は TLB copy で raster に写すか、LINEAR の swapchain を要求する）。
- WS210（P1）の形との対応: app の W の画像（OPTIMAL）から swapchain への vkCmdCopyImage は、swapchain が UIF なら UIF→UIF の TLB copy になり、compositor の sample の変換が消える（WS210 G5 の前提）。swapchain が LINEAR のままでも TLB copy で、compositor 側の変換が commit ごとに 1 回残る。
- UAPI の追加と Wayland の protocol の能力の追加は、差分を見せてユーザーの承認を得てから。

### 7. 他の platform

- i915・Venus・llvmpipe の kernel・libvulkan の経路は変えない。compositor の scene の写しの image 化（4 の最後）は全 platform で同じ Vulkan の使い方の変更なので T1 の回帰の対象。

## 段（実装の順）

| 段 | 内容 | 主な file | 確かめ |
| --- | --- | --- | --- |
| A1 | OPTIMAL の UIF の layout、render の store・load、sample の直接の参照、CPU clear の除外、自己試験の門 | vulkan-resource.c、native-pass.c、vulkan-native-pass.c、vulkan-native-draw.c、vulkan-native-execute.c、v3d-diagnostic.c | host: layout の式・packet の field（Mesa の XML と照合する既存の native-pass の試験に UIF を足す）・sample の record。実機: 自己試験の一致、計数の tex の減り |
| A2 | copy・blit・buffer↔image の UIF の経路 | vulkan-native-image.c、vulkan-buffer-copy.c | host: 経路の選び分け。実機: compositor の upload・present の copy |
| A3 | compositor の scene の写しを image に | userland/desktop/wayland/scene.c、compose.c | 実機の xfer の 0、T1 の回帰 |
| A4 | WSI の swapchain の画像の UIF（UAPI の承認の後） | include/uapi、libvulkan wsi-image.c・wsi-wayland.c、libkeiland-backend-zedbsd、compositor | 実機の client の commit の変換の 0、T1 |

## 未解決・判断が要る点

- A4 の UAPI（descriptor の tiling の値）と Wayland の protocol の能力の追加: ユーザーの承認。
- OPTIMAL の `vkGetImageSubresourceLayout` を 0 にする時、既存の app・library が OPTIMAL の rowPitch を使っていないか（使っていれば直す）。
- client の canvas（libkeiland、CPU で描いて map した LINEAR の画像）の変換は、この Phase では消えない。WS210（app の GPU 化）か、libkeiland が canvas を staging（LINEAR）→ OPTIMAL の TLB copy で出す小さな変更（compositor の host image と同じ形）で消える。後者は libkeiland の変更なので Q1 の判断。

## 検証の範囲

- host: 既存の native-pass・vulkan-device・display の試験に UIF の場面を足す。aggregate の make check は走らせない。
- 実機（Pi 4）: 自己試験の log、2 秒の計数（tex・xfer・snap）、ユーザーの体感。QEMU（T1）は i915・Venus・llvmpipe の回帰（この Phase の kernel の変更は bcm2711 だけ）。

## design-reviewer の指摘（2026-10-12、未反映、次の generation で設計を直す）

重大:
1. 同じ位置の部分の copy を TLB copy にはできない（tile 単位の load/store が範囲の外の destination を source で上書きする。今の部分の TLB copy は「範囲の外は既に等しい」前提の同期の場合だけ正しい、`vulkan-native-image.c:597-601`）。scene の戻しは古い画素を貼る。→ tile に揃う・画像の端まで・等しさの同期の場合だけ TLB、他は meta の draw（UIF の source を直接 sample、destination の tile を load）。4 と A3 を直す。
2. TLB が断る buffer↔image（部分・複数の region・64 byte に揃わない行・揃わない bufferOffset、`vulkan-native-image.c:680-720`）は CPU の行の copy（`vulkan-buffer-copy.c:527-529`、`image->pitch` で番地を計算）に落ちるので、UIF の画像では壊れる。in-tree の利用: wayland/shot.c:316、libbrowser/paint/vulkan.c:475、libglesv2/texture.c:340・framebuffer.c:707,2996-3006、xserver/vulkan.c:813、libvulkan/wsi-swapchain.c:2164、libvulkan-compat。→ UIF を知る fallback（readback は一時の raster へ TLB→memcpy、upload は TFU か一時の UIF＋meta draw、まれな所は数えた CPU の swizzle）、CPU の span の経路は UIF を断る。
3. A1 だけでは出せない: display の WSI の swapchain の画像は既に OPTIMAL（`wsi-swapchain.c:1226`）なので A1 で compositor の出力の画像が UIF になり、scene の部分 copy・shm の upload・present の copy・cursor の plane が A2/A3 の前に壊れる。pitch=0 でも `copy_whole`・`buffer_image_whole` の 64 の検査を通る。→ 「OPTIMAL＝UIF」の門は A2 と 2 の fallback の後の最後の段。A3（kernel 不要）を先に。7 に display の WSI の swapchain の画像が UIF になることを書く。
高:
4. 自己試験の 96x40 は padding が無い。store だけで load（LOAD_OP_LOAD・UIF→raster）・部分の tile・画像より小さい framebuffer を試していない。padding を含む byte 比較は誤り（CPU は 0 で埋める）。V8 は `cl.c` の builder で runtime の encoder ではない。結果の置き場（`hardware.*_ready`、V7〜V9 の失敗・不許可で off）を決めていない。→ 100x70（2x2 tile、両方の padding）、native pass で raster→UIF→raster の往復、見える画素だけ比較。
5. UB の高さは framebuffer でなく画像から（`vulkan-native-pass.c:440-443` は framebuffer の大きさ）。raster だけの検査に UIF の分岐（`native-pass.c:66-68,243,252`、`vulkan-native-execute.c:195-201,220`）。copy の source にも形と UB の高さ。UB の高さ＝block_rows は推定（自己試験で確かめる）。
6. 直接の sample は texture の entry の形を変える（`vulkan-native-draw.h:21-29`、find_texture、validate_pass、release、不確かな退役の経路も）。`conversion_supported`・TFU は pitch を使うので UIF を除く。
7. A4 の漏れ: `GPU_IMAGE_LINEAR` は `include/uapi/gpu.h:36`。LINEAR 以外を断る所（`gpu.c:3998`（i915 と共通）、`bcm2711/share.c:287`、`libvulkan/dedicated.c:42,50`、`gpu-zedbsd.c:84`）、import の rowPitch と stride の照合（`gpu-buffer-zedbsd.c:529-533`、`memory.c:1479`）、UIF の stride の意味と `same_image`（`share.c:306-322`）、display の import（`display-device.c:1476`）は UIF を断り続ける、game mode は frame ごとに決めるので scanout-rules に「UIF → direct=0」、protocol の能力は bcm2711 の UIF の layout を名指す（汎用の flag にしない）。
中・低: 8 `vulkan-transfer.c:409-452` の重なりの判定が pitch の byte の区間。9 部分の LOAD_OP_CLEAR の quad は新しい shader が要る（1x1 の UIF の clear 色を sample する meta draw が安い）、`clear_target` は UIF を断る。10 1 節の誤り: kernel は既に OPTIMAL の `vkGetImageSubresourceLayout` に ENOTSUP を返し libvulkan が 0 にする（`vulkan-resource.c:546-547`、`resources.c:269-271`）。11 完了の条件 (1) は今の perf 行では測れない（canvas を分けられない、直接 UIF の数と種類ごとの数を足す）、(3) は「T1 の回帰」に。12 alignment 4096 の根拠が無い（小さな画像が太る）。13 OPTIMAL の画像に bind した memory を map して raster で書く app は黙って壊れる（in-tree は見つからず）。
確かめられた点: pass は 1 つずつ準備・実行・解放され、各 job の前に L2T と slice の cache を invalidate するので、前の pass が書いた UIF の画像を次で sample する順は正しい。write log は画素の箱なので UIF でも incremental copy は動く。4096x4096 の UIF の大きさは maxResourceSize と同じ。HAL の変更は要らない。
