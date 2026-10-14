<!-- awesome-plan project=zedbsd record=ws236-p009 -->

# ws236-p009: decoder の出力を GPU・HVS で直に使う道の調べと設計

Parent: [WS236](../ws.md)
Status: 設計済み（実装はユーザーの判断待ち、2026-10-15 P3。code は書いていない）
Phase disposition: normal
実行者: P3（worktree `/home/awe/zedBSD-worktrees/p3`、branch `agent/p3-ws236c`）

## ユーザーの問い（原文、2026-10-15）

「デコーダの出力は、おそらくVideoCore でテクスチャとして使える形式なのでは？それを特別に利用したらどうですか？」

## 結論（先に）

- 半分は当たり。VPU の decoder の出力は Pi 4 の SDRAM の連続の memory に置け、**HVS（display の scaler）はそれを NV12（linear）でも SAND128 でも直に読んで、YUV→RGB と拡大縮小を hardware でする**。一方 **V3D の TMU は YUV も SAND も 2D の raster（linear）も sample できない**（UIF などの tile の形の R8・RG8・RGBA8 は sample できる）。V3D で使うには TFU（V3D の中の texture の変換器）で raster→UIF に 1 回並べ替える必要がある。TFU は GPU の hardware なので「CPU で画素を触らない」は守れる。
- 今の遅さの主因は decoder の形式ではなく、**(1) 4 枚重ねが kernel で効いていない（depth 1.0、原因の見当は下の 0）** と **(2) 表示の道が全部 CPU**（kernel の place 5 ms、readback 2.5 ms×2、libmedia の copy 11〜14 ms、videoplayer の NV12→RGB と縮小 約 60 ms／枚）。
- 推す順: **案 0（depth の直し、小）→ 案 1（videoplayer が decode の NV12 の image を V3D で sample して描く。compositor は変えない）→ 後で案 2（全画面などで HVS の overlay に NV12 を直に載せる）**。案 1 で毎 frame の CPU の画素の仕事が全部消える。

## T2 の実機の数（T1-663、2026-10-15、main の `plan/agents/T2/results-20261015-t1-663.md`）

- Video.mp4 shown 34・late 115。1080p30 は 10 秒の動画に約 15 秒（shown 53〜56・late 244〜247）。probe 3 本は全 frame 一致。
- kernel `v3d: video:` 2 秒に 35〜44 decodes、**depth 1.0**、frame 31〜41 ms、place 約 5 ms、readback cached 約 2.5 ms、waits 0。
- libmedia `Vulkan Video:` 「4 in flight」（これは設定の数で、実際に飛んでいる数ではない）、submit 4〜5 ms、wait 28〜32 ms、copy 10.7〜14 ms。
- videoplayer `DRAW` picture 約 60 ms／枚（CPU の NV12→RGB と縮小）、rest 約 5 ms／枚。

## 0. depth 1.0 の原因の見当（読みだけ、直すのは別の Phase）

- libvulkan の `vkQueueSubmit` は fence が `VK_NULL_HANDLE` の submission に **私的な native の fence を付ける**（`userland/desktop/libvulkan/queue.c` の `queue_enqueue`、「A queue retains one native fence per concurrent fence-less submission」、`queue_private_fence` で `fence = private_fence->fence`）。
- kernel は fence の付いた execution を、その command buffer の decode の frame が image に入るまで終えない（`src/drivers/gpu/bcm2711/vulkan-queue.c` の 796〜807 行、`bcm2711_vulkan_video_settle_commands` が EINPROGRESS → `*done` にしない）。p008 はこれを「vkvideo-probe のように host が image を map して読む使い方」のために入れ、libmedia の decode は fence を持たないので待たないと見ていたが、libvulkan が私的な fence を付けるので**全ての decode の submission が frame を待つ**。
- 同じ session の execution は順に進む（同 file 746 行「earlier executions of the session already ran」）ので、次の decode（A_k+1）の execution は A_k の frame が来るまで始まらない → VPU に渡る unit は常に 1 つ（depth 1.0）、libmedia の wait 28〜32 ms ≒ kernel の frame 31〜41 ms。
- 直しの候補（どれも小さい、0.5〜2 LW）: (a) libvulkan が私的な fence を wire で印し、kernel は application の fence の時だけ frame を待つ。(b) execution の「順の上での終わり」と「fence の signal」を分け、fence だけを frame の後に signal する（後の execution は image に触る時に settle するので順の意味は保てる。Vulkan の意味に一番近い）。(c) kernel の fence の settle をやめ、vkvideo-probe を copy の command で読む形に直す（今の probe は decode の出力の image の memory を `vkMapMemory` して直に読む、`userland/tests/vkvideo-probe/main.c` 681 行、なので probe の変更が要る）。推すのは (b)、最小は (a)。
- 効果の見込み: VPU が pipeline で回り、decode は 1080p30 に届く見込み（VPU の H.264 は 1080p60 の能力）。ただし表示の CPU の 60 ms が残るので late はこれだけでは減らない。

## 1. VPU の decoder の出力の形式と memory の共有

| 項目 | 調べた事実 | 出典・確度 |
| --- | --- | --- |
| 今の形 | output の encoding は NV12（`rpi4-mmal.c` 1367〜1386 行、拒まれたら I420）。frame は VCHIQ の bulk（VPU の DMA）で decoder の cached の bounce buffer（`dma_alloc`、1 GiB の下、連続）へ来て、kernel の CPU が補正しながら image へ写す（place）。 | tree |
| MMAL の他の encoding | I420（firmware の既定）、NV12、YUVUV128（fourcc `SAND`、128 byte 幅の列に Y の行と UV の行を持つ形、Pi 4 の native の形）、OPAQUE（`OPQV`、VPU の中の画像の handle で、VPU の他の component（ISP・resize）にしか意味が無い） | Raspberry Pi userland の `interface/mmal/mmal_encodings.h`（BSD-3）。video_decode が Pi 4 の H.264 で SAND を受け付けるかは**未確認**（PORT_INFO_SET で `SAND` を試す probe が要る、1 LW＋Pi） |
| 2〜15 が 1 小さい | firmware の NV12 への変換で起きる（p006）。limited range の表示では Y < 16 は全て黒、Cb・Cr の 2〜15 は色域の外なので、**表示（HVS・GPU の変換）では見た目に差が出ない**。差が要るのは bit 単位の readback（vkvideo-probe・libmedia の CPU の道）だけ。SAND で出せばこの変換を通らない見込み（未確認）。 | p006 の dump の数え |
| zero-copy（VCSM） | MMAL の ZERO_COPY（parameter 0x4）は VPU が ARM の memory を shared memory の service（VCHIQ の別の service）で import して直に書く形。今の bulk も VPU の DMA が ARM の物理 page へ直に書くので、VCSM で消えるのは bulk の 1 回の DMA（推定 数 ms の遅れ、CPU の負荷ではない）だけ。別の VCHIQ の service の実装（6〜10 LW）に見合わない。 | design.md §6、userland（BSD-3） |
| memory の共有 | Pi 4 の SDRAM は ARM・VPU（bus の 0xC0000000 の窓で 1 GiB の下）・V3D（自分の MMU で物理 page を写す、`mmu.c`）・HVS（連続の物理 address）が全て同じ物を読み書きできる。VPU・V3D・HVS は ARM の cache を snoop しないので、CPU が触らない限り cache の保守は要らない（今の MMAL の memory の規則と同じ）。V3D の Vulkan の memory は「1 GiB の下の連続」（design §7）なので、**VPU の bulk の宛先を NV12 の image の memory そのものにできる**（place が消える）。 | tree・design.md §7 |

## 2. V3D 4.2 の TMU・TFU が直に扱える形式と、zedBSD の V3D に足す大きさ

| 項目 | 事実 | 出典 |
| --- | --- | --- |
| TMU の sample | R8・RG8・RGBA8 などは sample でき、linear の filter 可（Mesa の v3dv の format 表で R8_UNORM→R8、R8G8_UNORM→RG8、filter true）。**2D の raster は sample できない**（v3dv「Raster format is only supported for 1D textures」、sample には optimal＝UIF・LT が要る）。YUV・SAND の texture の型は無い。 | Mesa 25.0.7 `src/broadcom/vulkan/v3dv_formats.c`・`v3dvx_formats.c`（MIT） |
| YCbCr | v3dv の `VK_KHR_sampler_ycbcr_conversion` は plane ごとに R8・RG8 の texture にして shader で変換する（driver の hardware の機能ではない）。Mesa の gallium は SAND128 の NV12 を shader の blit（UBO から address を計算して読む）で R8・RG8 の UIF に並べ替えてから sample する（`v3d_blit.c` の sand8）。 | Mesa（MIT） |
| TFU（raster→UIF） | TFU は raster の R8・RG8・RGBA8 などを UIF に並べ替え、mipmap も作れる（`tfu_supports_tex_format`）。zedBSD は TFU を既に動かしている（`v3d-job.c` の `run_tfu`、RGBA8 の raster→UIF、起動の自己試験 `tfu_uif_ready`）。 | Mesa（MIT）、tree |
| TFU の YUV 入力 | TFU は入力の format に SAND128・SAND256 を持ち（Mesa の `v3d_tfu.h` の ICFG）、chroma の address・U の plane の address・YUV→RGB の係数の register（COEF0〜3、COEF0 の bit 31 で既定の係数の代わりに使う）を持つ（Linux の `v3d_regs.h` の名前、GPL、値は事実）。**raster の YUV 入力の選び方と係数の bit の配置は公開の資料に無い**。Mesa はこの道を使っていない。 | Mesa（MIT）、Linux（GPL、名前と register の位置だけ） |
| 今の zedBSD | sampled image は RGBA8・BGRA8 だけ（`vulkan-query.c` 394 行）。NV12 は decode の出力・DPB・TRANSFER_SRC だけ（同 405 行）。texture は描く時に raster→UIF を CPU か TFU で作る（`vulkan-native-draw.c` の `conversion_supported`・`prepare_conversion`、`native-storage.c` の snapshot）。 | tree |

足す物（WS141 の file、案 1 の B-2）:
- `vulkan-query.c`: R8_UNORM・R8G8_UNORM の sampled（optimal）、NV12 の image に SAMPLED と plane の view（`VK_IMAGE_ASPECT_PLANE_0/1` の R8・RG8 の view、Vulkan 1.1 の multi-planar の規則）。
- `vulkan-resource.c`・`vulkan-native-image.c`: plane の view の offset・pitch・大きさ。
- `native-texture.c`: texture の state の型を R8・RG8 にも（今は RGBA8 だけ）、return の形。
- `vulkan-native-draw.c`・`native-storage.c`: snapshot（raster→UIF）を 1 byte・2 byte の画素にも（UIF の utile は 8bpp で 8×8、16bpp で 8×4）。TFU の type を R8・RG8 に。CPU の予備の道も同じ形で。
- shader の compiler: 2 枚の texture の sample と 3×3 の行列は今の compositor の shader と同じ種類の演算で足りる見込み（要確認）。
- 見積もり: 8〜12 LW（Pi の TFU の 8・16 bpp の確かめを含む）。
- 変種 B-1（TFU の YUV→RGBA8 の hardware の変換）: 動けば texture の型の追加が要らず shader も RGBA8 を 1 枚読むだけ（WS141 −3〜4 LW）。ただし raster YUV の入力の設定と係数の配置が非公開で、Pi で bit を試す probe が要る（1〜3 LW、当たらない危険あり）。案 1 の始めに 1 回だけ試し、だめなら B-2 にする。

## 3. HVS の plane が YUV を直に読む道

| 項目 | 事実 | 出典 |
| --- | --- | --- |
| HVS の YUV | pixel の format に YCbCr 4:2:0 の 2 plane（NV12）・3 plane、4:2:2 があり、tiling に linear・64B・128B（SAND128）・256B がある。YUV の plane は CSC（YUV→RGB の係数、Y・Cb・Cr の offset）を通り、Y と chroma をそれぞれ PPF・TPZ で拡大縮小する。「YUV の変換は横の拡大縮小を有効にする必要がある」（1:1 でも scaler を通す）。 | Linux `vc4_regs.h`・`vc4_plane.c`（GPL、WS214 の範囲の規則で値は事実として使える。名前・構造は写さない） |
| WS214 の今 | p003b の overlay（plane 1・2、primary の上・cursor の下、unscaled と scaled・LBM、帯域の予算 1.5 GB/s 仮）は **RGBA の 4 byte の画素だけ**。p004（libvulkan の overlay）は実装・build 済み、Pi の受け入れ待ち。p005（compositor が載せる）は案。 | `plan/ws214/` |
| 足す物 | kernel（WS214 の display）: overlay の list に YUV 4:2:0 の 2 plane（pointer 2・pitch 2、chroma の scaling の word、CSC の係数（BT.601・709、limited range））、SAND128 の列の pitch、帯域の予算を 1.5 byte／画素で。libvulkan: VK_KHR_display の overlay の plane の format に G8_B8R8_2PLANE_420_UNORM、client の NV12 の image を overlay に出す。compositor: video の surface を overlay に載せる判断（全画面、または窓が覆われず角の丸み・glass が掛からない時）と、だめな時の合成への戻り。 | — |
| 合成を通す時 | 窓の一部が覆われる・glass・角の丸みがある時は HVS に載せられず、compositor が NV12 を sample する必要がある（compositor の shader の YCbCr の版と WS141 の R8・RG8）か、client が案 1 の RGBA の swapchain に戻る（どちらに戻るかを伝える protocol が要る）。 | — |
| game mode の直の scanout | backend の zedBSD の tree の直の口（GPU_DISPLAY_*、guardrail の例外）は primary に client の buffer を出す。NV12 は扱えず、例外は「app が明示に頼む game mode」だけ。動画の全画面に使うには例外の広げ（ユーザーの判断）が要り、libvulkan の overlay（WS214）で同じことが規則の中でできるので、推さない。 | guardrail の表（BUG-223、2026-10-06） |

## 4. 組み合わせの案

copy の数は 1080p の 1 枚あたり。「CPU」は毎 frame CPU が画素を触る物（guardrail の「画像の処理は GPU で」に反する物）。

| 案 | 流れ | 無くなる | 残る | 変える file と WS | guardrail・libvulkan だけの規則 | 見積もり | 危険 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 0（前提） | 今の流れのまま、decode を本当に 4 枚重ねる | VPU の待ちの直列（frame 31〜41 ms を 1 枚ずつ） | 表示の CPU の全部 | libvulkan `queue.c`（(a)）か kernel `vulkan-queue.c`・`vulkan-video.c`（(b)） / WS236（libvulkan は WS141・Q1 の確認） | 変わらない | 0.5〜2 LW | (b) は fence と queue の順の扱いの変更で、他の submission（WSI）への影響を見る |
| **1（推す）** | VPU →（bulk）→ NV12 の VkImage の memory（place なし）→ videoplayer が同じ VkDevice で plane の R8・RG8 の view を sample し、shader で YCbCr→RGB と縮小して自分の swapchain に描く → compositor（今のまま RGBA の窓を合成） | place 5 ms（CPU）、readback 2.5 ms×2（CPU）、libmedia の copy 11〜14 ms（CPU）、videoplayer の変換と縮小 約 60 ms（CPU）。**毎 frame の CPU の画素の仕事が 0** | VPU の bulk（VPU の DMA）、TFU の raster→UIF（Y 2 MB・UV 1 MB、GPU）、全面の quad の draw（GPU）、compositor の client の image の sample（今の TFU の snapshot、GPU） | WS141: 上の 2 の「足す物」。WS236: `vulkan-video.c`（bulk を image へ直に、補正は buffer への readback の時だけ、image の SAMPLED の usage）、`rpi4-mmal.c`（output の buffer を caller の memory に）。WS202: libmedia の API に GPU の picture（VkImage と plane の view、表示が終わるまで DPB の slot を busy に、fence）と、VkDevice を app と共有。WS122: videoplayer の描画を Vulkan に（UI の bar・menu は内容が変わった時だけ RGBA の texture へ上げて重ねる） | 合う（CPU の画素なし、compositor は変えない、videoplayer は libvulkan の普通の API だけ） | WS141 8〜12、WS236 2〜3、WS202 4〜6、WS122 6〜10、計 **20〜31 LW**（B-1 が当たれば −3〜4） | kernel の shader compiler の対応、TFU の 8・16 bpp の UIF の実機、V3D の executor の取り合い（compositor の frame は今 33 ms 級、WS141 p013）、libmedia の API の変更が libbrowser にも及ぶ、place をやめると sample される値で 2〜15 の −1 が残る（見た目は同じ、要判断） |
| 2（後で） | VPU → NV12（または SAND128）の image → client の buffer（kl_gpu_buffer_v1 に NV12）→ compositor が import → libvulkan の overlay の plane → HVS が CSC と拡大縮小 | 案 1 の全部に加えて TFU・draw・compositor の sample（**毎 frame の GPU の仕事も 0**） | VPU の bulk、HVS の走査の読み（帯域 1080p60 で約 190 MB/s） | WS214（kernel の YUV の plane、libvulkan の overlay の NV12、compositor の載せる判断と戻り）、WS236・WS202（NV12 の image の export）、WS122（video の surface と UI の surface を分ける）、protocol（kl_gpu_buffer_v1 の 2 plane の format） | 合う（overlay は VK_KHR_display の経路、game mode の例外は不要） | WS214 の p004・p005 の後に 15〜25 LW。戻りの道に案 1 の WS141 の部分も要る | WS214 の p004 の Pi の受け入れ・p005 が未着手、HVS5 の YUV の word と CSC の値（GPL の事実、WS214 の規則の中）、LBM と帯域、frame の時刻と commit の合わせ、窓の効果（角・glass）との両立 |
| 3（推さない） | 案 2 を backend の直の scanout（game mode の例外）で | 案 2 と同じ | 同じ | backend の zedBSD の tree、kernel の直の口に NV12 | 例外の範囲（game mode だけ）を動画に広げる判断が要る | 8〜12 LW | 規則の例外を広げる、全画面だけ |
| 4（推さない） | MMAL で video_decode → firmware の ISP へ tunnel し、RGBA を窓の大きさで受ける | 変換と縮小の CPU | RGBA 8 MB の bulk、窓の大きさが変わるたびの port の作り直し | WS236 | Vulkan Video の出力（coded の NV12）と合わない。libmedia の vkvideo の道から外れる | 6〜10 LW | Vulkan Video の決定（2026-10-14）と合わない |
| 5（推さない） | VCSM の zero-copy | bulk の DMA 1 回 | ほか全部 | WS236（新しい VCHIQ の service） | 合う | 6〜10 LW | 効果が小さい |

## 推す案と理由

1. **案 0 を先に**（別の Phase、0.5〜2 LW）。VPU の能力を使い切るのに要り、案 1・2 のどちらでも前提。
2. **案 1 を本筋に**。理由: (a) 毎 frame の CPU の画素の仕事（今 約 80 ms／枚）が全部消え、guardrail「画像の処理は GPU で」に合う。(b) compositor・WS214 の進みに依らず、窓・全画面・効果のどれでも同じ道で動く。(c) 足す物（R8・RG8 の texture、plane の view）は Vulkan の普通の機能で、compositor・libbrowser も後で使える。(d) ユーザーの問いの「VideoCore で texture として使う」を、V3D が読める形（TFU で 1 回並べ替え）で実現する。
3. **案 2 は後で**（WS214 の p004・p005 が Pi で通った後）。全画面の動画で GPU も止められ、電力に一番良いが、依存が長く、戻りの道に案 1 が要る。
4. SAND の出力の probe（1 LW＋Pi）は案 2 の時に行う（HVS は SAND を直に読む。案 1 の V3D は SAND を sample できず、Mesa のような shader の並べ替えが要るので、案 1 は NV12 のまま）。

## ユーザーに要る判断

1. 推す順（案 0 → 案 1 → 後で案 2）でよいか。案 2 を先に狙うか。
2. 案 1 で place（kernel の CPU の写しと補正）をやめ、GPU が sample する値では firmware の「2〜15 が 1 小さい」を直さない（見た目は同じ。`vkCmdCopyImageToBuffer` の readback だけ直す）ことを許すか。許す時は vkvideo-probe（image の memory を map して bit 単位で照らす）を copy の command で読む形に直す（WS236 の試験、約 1 LW）。
3. libmedia の API に GPU の picture を足し（WS202、libbrowser にも及ぶ）、videoplayer の描画を Vulkan に作り直す（WS122）ことを、WS236 の Phase として行うか、別の WS（WS141・WS202・WS122 にまたがる）を立てるか。
4. TFU の YUV 入力（非公開の設定）を Pi で試す probe（変種 B-1）を行うか。

## 確かめたこと・未実施

- 読んだ物: tree（`rpi4-mmal.c`・`vulkan-video.c`・`vulkan-queue.c`・`v3d-job.c`・`vulkan-query.c`・`vulkan-native-draw.c`、libvulkan `queue.c`、libmedia `vkvideo.c`・`vkvideo-runtime.c`・`engine.c`、videoplayer `media.c`、compositor `import.c`）、`plan/ws236/design.md`・phase008、`plan/ws214/`、guardrail、Mesa 25.0.7（MIT、`/home/awe/zedBSD-worktrees/p1/build/mesa-tools/mesa-25.0.7`）、Linux の `v3d_regs.h`・`vc4_regs.h`・`vc4_plane.c`（GPL、WS141 の temp、事実だけ）。
- host の probe は行っていない（Pi の firmware と hardware の振る舞いの問いで、host では答えが出ない）。
- 未実施（Pi が要る）: video_decode が `SAND` を受け付けるか、TFU の 8・16 bpp の raster→UIF、TFU の YUV 入力、HVS の YUV の plane。depth 1.0 の見当は読みだけで、実機・host では確かめていない。
