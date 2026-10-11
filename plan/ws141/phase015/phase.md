<!-- awesome-plan project=zedbsd record=ws141-p015 -->

# ws141-p015: GPU が書いて GPU が読む画像を UIF で持つ（設計）

Parent: [WS141](../ws.md)
Status: planning（設計の第 2 版、design-reviewer の指摘を反映、Q1・ユーザーの確認の前）
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

## 目的と完了の条件（第 2 版、2026-10-12 P3、review の指摘を反映）

- GPU が書いて GPU が読む画像（`VK_IMAGE_TILING_OPTIMAL` の色の画像）を memory 上 UIF（XOR 無し）で持ち、render の store・load、texture の sample、copy・clear・blit、buffer↔image を GPU（TLB・TMU・必要なら TFU）で行う。これらの画像について、CPU の raster→UIF の変換と行の memcpy が毎 frame の経路から無くなる。
- 完了の条件:
  1. Pi の 2 秒の計数（下の B0 で足す数）で、compositor の frame の submission の「map されていない画像の CPU 変換」と「UIF の画像の CPU の fallback」が 0。map された LINEAR の画像（client の canvas）の変換は別の数で出し、この条件から外す（A4・WS210 の範囲）。
  2. 起動時の自己試験（5 節）が実機で全部一致する。
  3. i915・Venus・llvmpipe の挙動が変わらないこと（T1 の回帰）。
  4. 段 A4（WSI の swapchain の画像の UIF）は UAPI の承認の後の別の段で、その完了で client の窓の commit ごとの変換も 0。

## 範囲

対象: bcm2711 の render device の画像のうち、`VK_IMAGE_TILING_OPTIMAL`、2D、1 level・1 layer、1 sample、`R8G8B8A8_UNORM`・`B8G8R8A8_UNORM`（今の driver が受ける物と同じ）。**display の WSI の swapchain の画像（`wsi-swapchain.c:1226`、OPTIMAL）も対象に入る**ので、compositor の出力の画像（render target・present の copy の元・scene の写しの元・cursor の plane の像）も UIF になる（review 3）。

対象外（raster のまま）: `VK_IMAGE_TILING_LINEAR` の画像（host が map する staging、display の scanout の native image、Wayland の WSI で今 LINEAR として共有している画像（A4 で扱う））、buffer。

## 設計

### 0. 有効にする門（review 3）

- kernel に `uif_optimal`（space ごとではなく hardware の 1 つの真偽）を置く。真になる条件は (a) build の既定（段 A1・A2 の間は偽、A3 で真に変える）と (b) 起動時の UIF の自己試験（5 節）が全部一致したこと、(c) V7〜V9 の自己試験のどれかが失敗・不許可でないこと、の全部。偽なら OPTIMAL の画像は今と同じ raster（今の動きのまま）。
- 画像の layout は作る時（`vkCreateImage` の kernel の側）に決め、資源の記述に記録する。門の値が後から変わっても（reset など）、既にある画像の layout は変えない（reset は門を偽にしない。自己試験は起動時の 1 回だけ）。
- 全ての経路（1〜4 節）は資源の記述の `uif` で分岐し、門は「新しく作る OPTIMAL の画像を UIF にするか」だけを決める。これで A1・A2 の kernel を Pi に置いても動きは変わらず、host 試験は門を真にした fixture で全経路を通せる。

### 1. 画像の layout（kernel、`vulkan-resource.c`）

- UIF の画像は今の texture の写しと同じ strict UIF（XOR 無し、UB の padding 無し）: 幅を 32 px の column、高さを 8 px（32 bpp の UIF block の高さ）の行に切り上げ、`bytes = columns × block_rows × 1024`（`bcm2711_native_texture_size` と同じ式）。資源の記述に `uif`（真偽）と `block_rows` を足す。`pitch` は raster の画像だけの意味にし、UIF の画像では 0 にする。
- memory の要求の alignment は 1024（UIF block の境界。今の raster の 64 から 4096 には上げない、review 12）。自己試験は UIF の画像を 1024 の倍数で 4096 の倍数でない番地に置いて確かめる。TMU の texture の base の field と TLB の store の番地の下位の bit が 1024 の境界で足りることは、この自己試験と Mesa の XML（field の bit の範囲）で確かめる。
- `vkGetImageSubresourceLayout`: kernel は既に OPTIMAL に ENOTSUP を返し、libvulkan が 0 にしている（`vulkan-resource.c:546`、`resources.c:269`）。変更は無い（review 10、前版の誤りを削除）。
- CPU は UIF の画像の画素に触れない。OPTIMAL の画像に bind した memory を map して raster として書く app は Vulkan の規則の外で、UIF の後は黙って壊れる（in-tree には無いことを review が確かめた。review 13）。`RESOURCE_READ/WRITE` の ioctl は今と同じく memory の bytes として扱う。

### 2. render target（`native-pass.c`・`vulkan-native-pass.c`・`vulkan-native-execute.c`）

- `struct bcm2711_native_pass` に出力の形（raster か UIF）と出力の UB の高さ、copy の元の形と元の UB の高さを足す（review 5）。UB の高さは framebuffer ではなく**画像の** `block_rows` から取る（`vulkan-native-pass.c:440-443` は framebuffer の大きさを使っているので、画像の記述から取る形に変える）。
- Store Tile Buffer General（29）・Load Tile Buffer General（30）の `Memory Format` を UIF No XOR、`Height in UB or Stride` を UB の高さにする（raster は今どおり byte の stride）。UB の高さ＝`block_rows` は推定で、自己試験の往復で確かめる。
- raster だけを前提にした検査に UIF の分岐を足す: `native-pass.c` の pitch の検査（66-68 行、copy の元も）と `validate_images` の大きさ（`pitch × height` → UIF は `bytes`）、`vulkan-native-execute.c:195-201,220`。
- LOAD_OP_LOAD・STORE・tile の clear＋store は同じ 2 つの packet なので UIF でも同じ流れ。部分の render area は tile 単位の load で外側を保つので今どおり。
- CPU の clear（`clear_target`、部分の area の LOAD_OP_CLEAR）は UIF の画像を断る。UIF の画像の部分の clear は、1x1 の UIF の clear 色の texture（4 byte を CPU で用意、画像の画素の処理ではない）を sample する meta の draw で、area の矩形に描く（既存の texture の meta の shader を使い、新しい shader は作らない。review 9）。全体の clear は今どおり tile の clear＋store。

### 3. sample（`vulkan-native-draw.c`・`vulkan-native-draw.h`）

- UIF の画像は変換せず、texture の record にその画像の memory の GPU の番地を直接書く（record の形は `bcm2711_native_texture_encode` と同じ）。
- `struct bcm2711_vulkan_native_texture_copy`（`vulkan-native-draw.h:21-29`）に「直接」の形（`direct_view`: 画像の memory の view を 1 つ hold、`pixels`・`snapshot`・`convert` は空）を足す（review 6）。これに合わせて直す所: `find_texture`（同じ画像の再利用の判定）、`validate_pass`（直接の view も pass の出力との重なりを検査する。出力と同じ画像を sample する feedback は今と同じく断る）、release（直接の view の hold を外す）、不確かな退役（uncertain completion）の経路（view の hold を pass の退役まで保つ、snapshot の users と同じ扱い）。
- `conversion_supported` と TFU の経路は `pitch` を使うので、UIF の画像を最初に除く。snapshot と private の変換は LINEAR の画像だけに残る。

### 4. copy・blit（`vulkan-native-image.c`・`vulkan-buffer-copy.c`・`vulkan-transfer.c`）

image↔image（vkCmdCopyImage）:

- **TLB copy は「範囲の外の destination を上書きしても正しい」場合だけ**（review 1）: (a) 全体の copy（今の `copy_whole`、元と先の形が違ってよい: raster→UIF、UIF→raster、UIF→UIF）、(b) 同じ位置の部分の copy で、範囲が tile（64 px）の境で始まり、tile の境か画像の端で終わる物、(c) 今の incremental copy（`sync`、範囲の外が既に等しいと write log で分かる場合）。
- それ以外の部分の copy（位置が違う、tile に揃わない）と全ての blit は meta の draw（元を sample し、先の tile を load して範囲だけ描く）。元が UIF なら変換は無い。元が raster なら今どおり snapshot・変換（LINEAR の元は A4 まで残る）。8 bit の UNORM の copy は sample→書き戻しで bit が変わらない（`meta->raw`、今と同じ）。

buffer↔image（vkCmdCopyBufferToImage・vkCmdCopyImageToBuffer、review 2）:

- 全体の copy で行が 64 byte に揃う物は今の TLB の経路（`bcm2711_vulkan_native_buffer_image_create`）で、image の側の形を UIF・raster から選ぶ。
- UIF の画像の upload（buffer→image）で TLB に乗らない物（部分・複数の region・揃わない行・揃わない bufferOffset）: GPU の 2 段にする。(1) buffer の該当の行を一時の raster の画像に見立てて（行の長さ＝stride、bufferOffset を base）TFU で一時の UIF の画像へ変換、(2) 一時の UIF を sample する meta の draw で先の範囲に描く。TFU の base・stride の制約（64 byte など）に合わない物は、まれな fallback として CPU の UIF の swizzle（画素ごとの番地の計算、`native-texture.c` の式）で書き、perf 行の `uiffb` に数える。
- UIF の画像の readback（image→buffer）で TLB に乗らない物: render target を buffer の範囲にした meta の draw（frame の大きさ＝範囲、base＝bufferOffset、stride＝行の長さ）。tile の store は frame の外を書かないことを自己試験で確かめる（5 節の (d)）。番地・stride が TLB の制約に合わない物は、一時の raster の画像へ TLB で写してから行の memcpy（CPU は画素を変換しない写しだけ、`uiffb` に数える）。
- `vulkan-buffer-copy.c` の CPU の行の経路（`image->pitch` で番地を計算する所、527-529 行）は UIF の画像を ENOTSUP で断る。断った場合に上の経路へ回すのは `vulkan-native-image.c` の側。
- in-tree の利用（review が挙げた所）: `wayland/shot.c:316`（全体、TLB）、`libvulkan/wsi-swapchain.c:2164`（readback、全体、TLB）、`libbrowser/paint/vulkan.c:475`・`libglesv2/texture.c:340`・`framebuffer.c:707,2996-3006`・`xserver/vulkan.c:813`・`libvulkan-compat`（部分の upload・readback を含む → 上の GPU の 2 段か fallback）。A1 の host 試験でこれらの形（部分・複数 region・揃わない行）を fixture にする。

重なりの判定（`vulkan-transfer.c:409-452`、review 8）: pitch の byte の区間の判定は raster だけに使い、UIF の画像が絡む時は「同じ memory の区間に重なれば重なり」とする（保守的）。

compositor の scene の写し（`wayland/scene.c`）: buffer をやめ、出力と同じ形・大きさの OPTIMAL の画像に持ち替える（image↔image の同じ位置の部分の copy）。UIF の後は、tile に揃う所は TLB、揃わない所は meta の draw（元の UIF を直接 sample）になり、毎 frame の行の memcpy が消える。i915・Venus でも同じ Vulkan の使い方なので動く（T1 の回帰）。**raster の間（門が偽）はこの変更で Pi が遅くなる**（揃わない部分の copy が meta の draw＋元の CPU 変換になる）ので、Pi に置くのは門を真にする A3 と同時にする。

### 5. 安全の門（起動時の自己試験、`v3d-diagnostic.c`、review 4）

- 100x70 の絵（64 px の tile で 2x2、UIF の column の padding（100→128）と block の行の padding（70→72）の両方を含む）を、**runtime の native pass の encoder**（`native-pass.c`、`cl.c` の builder ではない）で:
  (a) raster→UIF（TLB で raster を load、UIF で store）。UIF の bytes を CPU の `bcm2711_native_texture_copy` の結果と、**見える画素の番地だけ**比べる（padding は CPU が 0 で埋めるが TLB は書かない・別の値を書くので比べない）。
  (b) UIF→raster（UIF を load、raster で store）の往復で、元の raster と見える画素が全部一致。
  (c) UIF を sample する 1 枚の quad を raster に描き、元と一致（直接の sample の record の確かめ）。
  (d) 画像より小さい frame（例 40x24、base を中ほどの番地）の raster の store で、frame の外の bytes が変わらない（4 節の readback の前提）。
  (e) 部分の tile（first/last の tile を 1 つ）だけの UIF の store で、他の tile の bytes が変わらない。
- UIF の画像は 1024 の倍数で 4096 の倍数でない番地に置く（1 節）。
- 結果は `hardware->uif_ready` に置き、(a)〜(e) の全部と V7〜V9 の成功で真。一つでも失敗・不許可なら偽にして、失敗した項目と最初に違った番地を 1 行の log に出す。門（0 節）はこの値と build の既定の積。

### 6. WSI の swapchain の画像（段 A4、UAPI の変更の承認が要る、review 7）

- 今、Wayland の WSI（libvulkan `wsi-image.c:80,281`）は共有の画像を LINEAR で作り、descriptor（`include/uapi/gpu.h:36` の `GPU_IMAGE_LINEAR`）で compositor に渡す。compositor は LINEAR として import して sample するので、commit ごとに窓全体の CPU 変換が 1 回ある。
- 案: descriptor の tiling に bcm2711 の UIF の layout を名指す値（`GPU_IMAGE_BCM2711_UIF`、汎用の「tiled」の flag にはしない）を足す。WSI は compositor が UIF の import を受けると答えた時（Wayland の GPU buffer の protocol の能力、これも bcm2711 の UIF を名指す）だけ共有の画像を OPTIMAL＝UIF で作る。
- 直す所（LINEAR 以外を断る所と、stride の照合）: `gpu.c:3998`（i915 と共通の import の検査）、`bcm2711/share.c:287`、`libvulkan/dedicated.c:42,50`、`libkeiland-backend-zedbsd/gpu-zedbsd.c:84`、import の rowPitch と stride の照合（`gpu-buffer-zedbsd.c:529-533`、`memory.c:1479`）、UIF の stride の意味（UIF では stride を 0 にし、`block_rows` を descriptor の別の field で渡すか、幅・高さから決める）と `same_image`（`share.c:306-322`）。
- display の import（`display-device.c:1476`、HVS の scanout）は UIF を断り続ける（HVS は UIF を読めない）。game mode は frame ごとに直の scanout かを決めるので、`scanout-rules` に「UIF の buffer → direct=0（合成に戻す）」を足す。
- WS210（P1）の形との対応: app の W の画像（OPTIMAL、UIF）から swapchain への全体の vkCmdCopyImage は、swapchain が UIF なら UIF→UIF、LINEAR なら UIF→raster の TLB copy（どちらも GPU）。A4 の後は compositor の sample の変換も消える（WS210 G5 の前提）。atlas の頁の全体の copy（staging LINEAR→OPTIMAL）は raster→UIF の TLB copy。
- UAPI の追加と protocol の能力の追加は、差分を見せてユーザーの承認を得てから。

### 7. 他の platform

- i915・Venus・llvmpipe の kernel・libvulkan の経路は変えない。compositor の scene の写しの image 化（4 節の最後）は全 platform で同じ Vulkan の使い方の変更なので T1 の回帰の対象。
- bcm2711 では門が真になると display の WSI の swapchain の画像が UIF になり、compositor の出力の render・present の copy（UIF→raster の TLB copy）・cursor の plane の像の copy（UIF→LINEAR の native image、全体の TLB copy）がその上で動く。これらは A1・A2 の host 試験で UIF の fixture を通す。

## 段（実装の順、第 2 版）

| 段 | 内容 | 主な file | 確かめ |
| --- | --- | --- | --- |
| B0 | 計数: perf 行に `uifs=`（直接の sample の数）、`conv=mapped/other`（変換を map された画像とそれ以外に分ける）、`uiffb=`（UIF の CPU fallback の数）を足す。UIF の自己試験（5 節）を足し、結果を log に出すだけ（門は偽） | v3d-hardware.h、vulkan-native-job.c、v3d-diagnostic.c、native-pass.c | host: 自己試験の比較の関数（padding を除く）。実機: 自己試験の結果 |
| A1 | kernel の UIF の全経路（layout、render の store・load、直接の sample、部分の clear の meta の draw、copy・blit の選び分け、buffer↔image の GPU の経路と fallback、重なりの判定、CPU の行の経路の拒否）。門は build の既定で偽 | vulkan-resource.c、native-pass.c、vulkan-native-pass.c、vulkan-native-execute.c、vulkan-native-draw.c/h、vulkan-native-image.c、vulkan-buffer-copy.c、vulkan-transfer.c | host: 門を真にした fixture で全経路（native-pass の Mesa XML の照合に UIF、vulkan-device の copy の選び分け、in-tree の部分 copy の形）。実機: 門が偽で今と同じ |
| A2 | compositor の scene の写しを OPTIMAL の画像に | wayland/scene.c、compose.c | host build、T1 の回帰（i915・Venus・llvmpipe） |
| A3 | 門の既定を真に。Pi に A2 の compositor と一緒に置く | v3d-diagnostic.c（既定） | 実機: 自己試験の一致、計数の `conv=…/0`・`uiffb=0`・`xfer` の減り、ユーザーの体感 |
| A4 | WSI の swapchain の画像の UIF（UAPI の承認の後） | include/uapi/gpu.h、libvulkan wsi-image.c・dedicated.c、share.c、gpu.c、libkeiland-backend-zedbsd、compositor、scanout-rules | 実機の client の commit の変換の 0、T1 |

## 未解決・判断が要る点

- A4 の UAPI（descriptor の tiling の値・UB の高さの渡し方）と Wayland の protocol の能力の追加: ユーザーの承認。
- client の canvas（libkeiland、CPU で描いて map した LINEAR の画像）の変換は、この Phase では消えない。WS210（app の GPU 化）で消える（WS210 の W の画像は OPTIMAL）。
- UB の高さ＝`block_rows` と、UIF の base の 1024 の alignment は推定で、自己試験の (a)(b)(c) で確かめる。違えば layout の式を直してから門を真にする。

## 検証の範囲

- host: 既存の native-pass・vulkan-device・display の試験に UIF の場面を足す（門を真にした fixture）。aggregate の make check は走らせない。
- 実機（Pi 4）: 自己試験の log、2 秒の計数（`uifs`・`conv`・`uiffb`・`xfer`）、ユーザーの体感。QEMU（T1）は i915・Venus・llvmpipe の回帰（kernel の変更は bcm2711 だけ、compositor の scene の変更は全 platform）。

## review の指摘の反映（第 2 版）

| 指摘 | 反映 |
| --- | --- |
| 1 同じ位置の部分の TLB copy は範囲の外を上書き | 4 節: TLB は全体・tile に揃う・sync の 3 つだけ、他は meta の draw |
| 2 TLB が断る buffer↔image が CPU の行の経路で壊れる | 4 節: TFU＋meta の draw の upload、frame を範囲にした readback、数える fallback、CPU の行の経路は UIF を断る |
| 3 A1 だけでは出せない | 0 節の門（build の既定）、段を B0→A1（門は偽）→A2→A3（門を真）に、7 節に display の swapchain |
| 4 自己試験の不足 | 5 節: 100x70、往復・sample・小さい frame・部分の tile、見える画素だけ、runtime の encoder、`uif_ready` |
| 5 UB の高さの出所・raster だけの検査 | 2 節 |
| 6 直接の sample と texture の entry | 3 節 |
| 7 A4 の漏れ | 6 節 |
| 8 重なりの判定 | 4 節の最後 |
| 9 部分の LOAD_OP_CLEAR | 2 節: 1x1 の UIF の texture の meta の draw、`clear_target` は UIF を断る |
| 10 `vkGetImageSubresourceLayout` | 1 節: 変更なし |
| 11 完了の条件の測り方 | 完了の条件と B0 の計数 |
| 12 alignment 4096 | 1 節: 1024、自己試験で確かめる |
| 13 OPTIMAL の memory を map する app | 1 節に記録（規則の外、in-tree に無し） |

## design-reviewer の指摘（2026-10-12、第 1 版への指摘。第 2 版で反映、上の表）

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

## design-reviewer の指摘（2026-10-12、第 2 版への指摘、未反映。p013 の compiler の短縮の後に第 3 版で直す）

解消: 前回の 3・5・10・13。残り・新規:
- 重大 1: 「tile（64 px）に揃う同じ位置の部分の TLB copy」も範囲の外を上書きする。tile が 256 を超えると supertile（1904x1020 で 128 px、4096² で 256 px）にまとめ、選んだ supertile の全 tile を load・store する（`native-pass.c:114-120,439`、`vulkan-native-image.c:441-444`）。→ (b) を消して全体と sync だけにするか、supertile の境で判定し extent の一致も条件に。自己試験 (e) は 2x2 tile で検出できないので 17x16 tile を超える fixture を。
- 高 2: upload の GPU の経路が本番で無効の TFU（`tfu_uif_ready` を真にする所が無い）に頼る。→ TLB（frame＝範囲、buffer を raster で load、一時の UIF に store）→ meta の draw にするか、TFU を有効にする調査を依存に。
- 高 3: A2 は門が偽の時（自己試験の失敗・V7〜V9 の不許可）に遅くなり、merge の間の main の image も遅い。→ A2 と A3 を 1 つの merge に。compositor が UIF の有無で scene を buffer か image か選ぶ（能力の知らせ方、UAPI・wire が要るなら承認）。
- 高 4: 重なりの「同じ memory なら重なり」は UIF の画像への複数 region の copy・src==dst の重ならない copy を断る。→ 同じ resource は画素の矩形、memory を共有する別の resource だけ区間で。
- 高 5: 自己試験に guard の bytes（allocation の後ろ）・画像より小さい framebuffer の UIF の store・1024 の境界の load と sample を。(c) の sample は shader・bin・直接の sample（A1）が要り、boot の diagnostic には space が無い（`v3d-diagnostic.c:22-23,377-415`、`attach.c:140-150`）→ (c) を A1 の後へ、B0 の範囲を明記。
- 中: CPU へ回す判断は `vulkan-native-job.c:158-185`（job は event ごとに pass 1 つ）で、複数 region・pass→退役→memcpy・TFU→draw の 2 段には job loop の作り直し・一時の画像の予算と quarantine の所有が要る（A1 の file に `vulkan-native-job.c`）。3 節の「feedback は今と同じく断る」は誤り（今は検査が無く snapshot で動いている、新しい拒否として書き、meta の copy の src==dst を巻き込まない）。完了の条件 1 は perf が hardware に 1 つで client と混ざる（session ごとに数えるか client を止めて測る）。部分 LOAD_OP_CLEAR の meta の draw は `prepare_meta_texture` が resource を要する（1x1 の storage の所有・予算・挿す位置が未記述）。門の名前 `uif_optimal`／`uif_ready` の不一致、V9 の TFU の結果を門に含めるか。
- 低: pitch を代入する他の所（`vulkan-native-pass.c:775-785`、`vulkan-native-image.c:220,229,437`）、CPU の fallback の write log（`bcm2711_v3d_memory_written`）、frame＝範囲の readback の 64 byte の制約、in-tree の一覧の漏れ（`shm.c:1026`、`cursor-plane.c:533-559`、`heads.c:1172-1197`、`backdrop.c:388`、`notes/render.c:304`、`libegl/vulkan.c:1037,1380`、`wsi-swapchain.c:2611`）、alignment 1024 の性能（page cache の bank、推測）。
- 確かめられた点: `vulkan-resource.c:546` の ENOTSUP、門は画像より先に決まる、vkCmdClearColorImage は全体の tile の clear だけ、display の swapchain は OPTIMAL、Wayland の WSI と keiland の buffer は LINEAR、scene の写しは 1 region、texture の record は strict UIF、write log は画素の箱、HAL の変更は不要。
