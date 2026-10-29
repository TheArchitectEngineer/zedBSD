<!-- awesome-plan project=zedbsd record=ws313-p011 -->

# ws313-p011: Mali の render の node・scheduler・Vulkan の executor・CS の命令の builder

Status: in-progress（実装済み・実機未試験）
Queue: q1675-i02（P7 の配下 D）、q1675-i03 の一部（P7 の配下 D2: render pass の LOAD・display の import）、q1675-i04 の一部（P7 の配下 D3: WLS の dispatch・拡大の blit、続けて clear attachments・depth と sample mask を書く fragment program の DCD・ZSD）、q1675-i05 の一部（P7: FBD の大きさの修正・depth attachment、配下 P7-B: mip level）
WS: [WS313](../ws.md)、設計 [gpu.md](../gpu.md) §11・§13。compiler は配下 C（§12）。

## ユーザーの指示（原文、2026-10-29、Q1 経由）

「Maliのドライバは、このGPUでとりあえずデスクトップを動かせるように、シェーダーコンパイラ、コマンド投入、シェーダ実行などを、一通り実装して、UATとデバッグに備えてください。デスクトップが動かせる範囲の実装でよいですが、余裕があればきちんと実装の対応範囲を他のGPUと同様に広げてください。」
「実機テストなしでコードだけ書いて揃えておいてください。今日の夜、まとめてテストします。」

- 解釈: desktop（Keiland の compositor と libkeiland の描画）に要る範囲を先に。実機の試験は無いので「実装済み・実機未試験」で cleared にしない。判断が要る所は推しで進め、下に記録する。

## 実装（2026-10-29）

| file | 中身 |
| --- | --- |
| `src/drivers/gpu/mali/cs/cs-build.{h,c}` | CS の命令（NOP・MOVE48/32・WAIT・RUN_COMPUTE/IDVS/FRAGMENT・FINISH_TILING/FRAGMENT・ADD32/64・LOAD/STORE_MULTIPLE・BRANCH・SET_SB_ENTRY・SET_EXCEPTION_HANDLER・CALL・JUMP・REQ_RESOURCE・FLUSH_CACHE2・SYNC_ADD/SET/WAIT32/64・STORE_STATE・ERROR_BARRIER・HEAP_SET/OPERATION）の encode、writer、行の copy・fill の loop（front end の LOAD/STORE だけで動く。shader 不要） |
| `src/drivers/gpu/mali/mali-queue.c` | stream の一般化: open（CSG・CS の program と start）・stop（terminate）・push（ring の insert と doorbell）・wait（sync の値、CS の FAULT/FATAL の ack と記録）・report（1 行）。driver の stream（group 0）の self-test: NOP＋SYNC_ADD64 |
| `src/drivers/gpu/mali/render.{h,c}` | render の node（gpu1）: session ごとの AS の table、VA の free run、buffer object（参照の数）、resource・blob（VkDeviceMemory の blob を含む）、CPU の view（uncached RAM）、export・import・display への backing、stage の memory（submit ごとの CS と descriptor） |
| `src/drivers/gpu/mali/sched.c` | group slot 1・stream 0・doorbell 2・AS slot 2。submit のたびに session の table を slot 2 に bind（前の table は disable で cache を clean）、ring に FLUSH・CALL・WAIT・FLUSH・SYNC_ADD64・ERROR_BARRIER、sync の値を 5 s 待つ。FAULT・FATAL・timeout は 1 行＋group の terminate と再 start、session を lost。capset（4、powervr と同じ形）・frame・job の marker（同期なので commit で完了）・recovery（stop・fault・reset・isolate） |
| `src/drivers/gpu/mali/vk/vk.h`・`vk-object.c`・`vk-stream.c` | executor の session、object の登録、transport（reply の領域、trailer、nested stream） |
| `vk-device.c`・`vk-format.c` | instance、physical device（Mali-G310、Vulkan 1.1、limit）、memory（1 heap、device local と host visible+coherent）、queue family 1 つ、format の表（RGBA8・BGRA8・sRGB・R8・RG8・16F・32F・32UI 他）、device、queue と timeline |
| `vk-memory.c` | memory（新規・blob の import・export）、buffer、linear の image（行は 64 byte に揃える）、view、sampler の descriptor、fence、semaphore |
| `vk-layout.c` | shader module、set layout（uniform・storage buffer、combined image sampler、各 binding 1 個）、pipeline layout、cache、pool、set（alloc・free・update）、render pass（1 subpass、color だけ）、framebuffer |
| `vk-pipeline.c` | compute・graphics の pipeline: `mali_shader_compile` で各 stage、code を session の bo に（128 byte 境界）、vertex input・topology・cull・blend・静的な viewport/scissor |
| `vk-command.c` | command pool・buffer、begin/end/reset、vkCmd* の record（bind・viewport・scissor・sets・index・vertex・draw・draw indexed・dispatch・copy buffer/image/buffer↔image・blit・fill・clear color image・clear attachments・barrier・push constants・render pass） |
| `vk-desc.{h,c}` | Valhall の descriptor の pack（SPD、Local Storage、Resource、Buffer、Texture＋Generic Plane、Attribute、Blend と固定機能の式、RGB Render Target、Framebuffer Parameters、Tiler Heap、Tiler Context、Primitive flags） |
| `vk-build.h`・`vk-queue.c` | vkQueueSubmit: 全 command buffer を 1 本の stream に。REQ_RESOURCE・SET_SB_ENTRY、copy・fill・clear は CS の loop、dispatch は RUN_COMPUTE（SRT・FAU・SPD・TSD・workgroup の staging register）、FAU に push constants・buffer の address・range・texture の size、resource table（texture・sampler・attribute・vertex buffer） |
| `vk-draw.c` | render pass: tiler の heap（8 MiB の 1 chunk）・geometry buffer・sample の位置、tiler context、FBD と RT（clear 色）、draw ごとに vertex（position と varying）・fragment の SPD、blend、ZSD、RUN_IDVS の staging register、最後に FINISH_TILING・RUN_FRAGMENT |
| `mali-attach.c`・`mali-mmu.c`・`mali-private.h` | device の lock（watch の PING と submit を直列に）、queue の後に render の開始、AS の unmap・destroy、型 |
| `platform/arm64/mali.mk` | 上の file と C の compiler の file（va-compile・va-encode・va-schedule）、共通の spirv・optimize・vk-codec |
| `tests/mali-cs-host-test.c` | 命令の encode（v10.xml の field の位置から手で詰めた値）、ring の列（panthor の値と照合）、copy・fill・行の loop を小さな interpreter で走らせて memory を照合 |
| `tests/mali-vk-desc-host-test.c` | descriptor の pack と blend の式（replace・straight alpha・premultiplied、MIN の拒否） |

## 実装（q1675-i03、配下 D2、2026-10-29）

| file | 中身 |
| --- | --- |
| `src/drivers/gpu/mali/vk/vk-frame.{h,c}`（新） | frame shader の descriptor の pack（host で走る）: pre-frame の draw（DCD）、depth/stencil（全て通し何も書かない）、sampler（nearest・clamp to edge・正規化しない）、FBD の pre-frame 0 の mode と frame shader DCD の pointer、RT の clean tile write |
| `src/drivers/gpu/mali/vk/vk-preload.{h,c}`（新）・`vk-preload-spirv.h`（新） | LOAD の pre-frame の draw: FragCoord の整数で texelFetch して colour 0 に出す fragment program（GLSL の source と glslc の再生成の方法を配列の傍の comment に）。program は node で 1 回（最初に LOAD の pass が来た時）`mali_shader_compile`、code は session ごとに 1 回 session の bo に写す。texture＋plane・sampler・resource table（4 本）・SPD・blend・ZSD・3 本の DCD を stage に |
| `src/drivers/gpu/mali/vk/vk-draw.c` | `pass_begin` を分けた: TSD を先に、`pass_targets`（RT。CLEAR かつ STORE の target に clean tile write）、`pass_preload`（colour target 0 が LOAD なら pre-frame の draw）、FBD に pre-frame 0（INTERSECT、clean tile の target があれば ALWAYS）と DCD の pointer。LOAD の不足の 1 行は session ごとに 1 度に |
| `src/drivers/gpu/mali/render.{h,c}` | `mali_render`: `preload`（node の program）・`preload_refused`。`mali_session`: `preload_code`・`preload_failed`・`load_warned`・`preload_used`。display の口 `mali_render_scanout_hold`（render_backing が出した page の表の pointer で export を探し、bo の参照を 1 つ取る）・`mali_render_scanout_drop` |
| `src/drivers/gpu/mali/sched.c` | pre-frame の draw を含む submission が FAULT・timeout で失敗したら、node の `preload_refused` を立て 1 行（以後の LOAD は従来の動作） |
| `src/drivers/gpu/mali/display/scanout.{h,c}`（新） | import の検査（host で走る）: RGBA8888・BGRA8888、linear、行が 4 byte の倍数で幅以上、始まりが 4 byte、DEVICE でない、page が 1 本の連続、行が allocation と page の中。外れは ENOTSUP と理由 |
| `src/drivers/gpu/mali/display/display.c` | `import_image`: 検査 → render の node の export の bo を保持 → resource（`bo`・`image`・`physical`・`address` は render の uncached の view）。constraints に SHARED・FOREIGN。present: BLOB は import の exported の layout と一致する時だけ、flip（§5 の (a)、import は dcache の clean 無し）か CPU の copy。read・write・map は import に ENOTSUP。free で bo の参照を返す。拒否は 1 度だけ 1 行 |
| `platform/arm64/mali.mk` | `vk-frame.c`・`vk-preload.c`・`display/scanout.c` |
| `tests/mali-vk-frame-host-test.c`（新） | DCD・ZSD・sampler・FBD の frame shader の語・clean tile の bit を v10.xml の field の位置から手で詰めた値と照合 |
| `tests/mali-scanout-host-test.c`（新） | import の検査の通る形・拒む形（形式・tiling・stride・offset・空・allocation の外・page の外・DEVICE・page 無し・page の隙間） |
| `tests/mali-preload-host-test.c`（新） | 埋め込みの SPIR-V を host の Mali compiler で compile し、pre-frame の draw が前提にする形（fragment、texture 1 本 entry 0 で size を読まない、buffer・FAU 無し、colour 0、FragCoord を読む）を確かめる。引数の path に SPIR-V を書き出す |

## 実装（q1675-i04、配下 D3、2026-10-29）

ユーザー「余裕があればきちんと実装の対応範囲を他のGPUと同様に広げてください。」の範囲。今まで拒否・skip していた場合だけに新しい経路が入る（desktop の経路の stream は変えない。下の判断）。

| file | 中身 |
| --- | --- |
| `src/drivers/gpu/mali/vk/vk-desc.{h,c}` | `mali_desc_wls_bytes`（workgroup の shared の bytes を 128 以上の 2 の冪に）・`mali_desc_workgroup_storage`（`mali_desc_thread_storage` が詰めた TSD に WLS を足す: word 1 の 0..4 に instance の数の log2、8..12 に size scale = log2(instance の bytes)+1、size base 0、word 4..5 に WLS の base） |
| `src/drivers/gpu/mali/vk/vk-queue.c` | dispatch の WLS の拒否を外した。shared の有る program は `workgroup_memory`（core あたりの instance の数・全体の bytes・stage から 4096 揃えで 4 GiB の線を跨がない memory）と `storage_descriptor`（TLS と WLS の TSD）。`mali_vk_build_thread_storage` は `storage_descriptor(…, NULL)` になった（出る語は同じ）。RUN_COMPUTE の staging register の並びを `mali_vk_build_compute` に分けて dispatch と blit で共有（並びは前と同じ）。blit の region が copy（同じ形式・同じ大きさ・どちらも前向き）でない時は `run_blit` → `mali_vk_meta_blit`、ENOTSUP なら従来の 1 行で skip。stream を作り始める時に `meta_used` を 0 に |
| `src/drivers/gpu/mali/vk/vk-meta.{h,c}`（新） | executor の自前の program（番号 0 blit、1 clear の vertex、2〜9 clear の colour location 0〜7）を node で 1 回 compile（`render->meta[]`・`meta_refused`、guard で公開、同時の compile は片方を捨てる）、session ごとに 1 回 code を bo に（`meta_code[]`・`meta_failed`）、形の確かめ（`program_fits`）、FAU の書き込み（`mali_vk_meta_fau`）、空の resource table（`mali_vk_meta_tables`）、blit の 1 region を compute の run に（`mali_vk_meta_blit`） |
| `src/drivers/gpu/mali/vk/vk-meta-spirv.{h,c}`（新） | 3 つの module の SPIR-V（GLSL の source と glslc の再生成の方法を配列の傍の comment に）、clear の fragment の Location の語を差し替える `mali_meta_clear_color_at`（kernel に依らないので host で走る） |
| `src/drivers/gpu/mali/vk/vk-build.h` | `mali_vk_build_compute` の宣言 |
| `src/drivers/gpu/mali/render.h` | `MALI_RENDER_META_PROGRAMS`（10）。`mali_render`: `meta[]`・`meta_refused`。`mali_session`: `meta_code[]`・`meta_failed`・`meta_used`・`wls_warned` |
| `src/drivers/gpu/mali/sched.c` | 自前の program を走らせた submission が失敗したら node の `meta_refused` に立て 1 行（preload と同じ形） |
| `platform/arm64/mali.mk` | `vk-meta.c`・`vk-meta-spirv.c` の 2 行 |
| `tests/mali-vk-desc-host-test.c` | WLS の語の照合（13 checks を追加） |
| `src/drivers/gpu/mali/vk/vk-draw.c`（D3 の続き） | pass の中の vkCmdClearAttachments を `clear_attachments`・`clear_rect` で draw に（通常の `draw()` とは別の関数、同じ IDVS の staging register を全て書く）。`draw()` には、fragment program が `writes_depth` の時に ZSD の Depth source を Shader（3）・DCD の kill と ZS update を Force Late、`writes_sample_mask` の時に DCD の Shader Modifies Coverage（bit 20）を足した。discards の時の bit 20 は足していない |
| `tests/mali-vk-meta-host-test.c`（新） | 埋め込みの SPIR-V を host の Mali compiler で compile し、blit（compute、8×8×1、push 52 byte、storage buffer 2 本 set 0 binding 0・1、長さを読まない）・clear の vertex（position だけ、vertex index、viewport の 6 語が FAU の中）・clear の colour（location 0〜7 に移した各々が その location だけ）の形を確かめる。引数の directory に SPIR-V を書き出す |

## 実装（q1675-i05、P7 と配下 P7-B、2026-10-29、実機未試験）

**FBD の大きさの修正（今夜の試験に効く見込み）**: `vk-desc.h` の `MALI_DESC_FBD_BYTES` を 64 → 128（v10.xml の aggregate "Framebuffer" = Parameters 64 + Padding 64。panvk も RT を pan_size(FRAMEBUFFER) の後に置く）。`pass_begin` は padding の 16 語を 0 で書く。今までは RT を fbd+64 に書いていた（GPU は fbd+128 から読む見込み）。commit 78a87b871。

| file | 中身 |
| --- | --- |
| `vk/vk-surface.{h,c}`（新、前の P7 の packer を P7-B が仕上げた） | depth/stencil の format の表（D16・X8_D24・D32・D24S8、S8 は executor の format に無い）、level の並び（Mesa の pan_layout.c の手順: 1 layer に level を続け、行・level を 64 byte に揃える）、FBD の word 12・13、ZS CRC 拡張（ZS Target・S Target、linear、clean tile write）、Depth/stencil descriptor（stencil の face・mask・reference、depth の test・write・compare・bias、Depth source、Stencil from shader）、DCD の kill と ZS update（Mesa の pan_earlyzs の形）、sampler の LOD 語（8.8 の丸めは llroundf に合わせた） |
| `vk.h`・`vk-format.c`・`vk-layout.c` | format に X8_D24_UNORM・D24_UNORM_S8_UINT（attachment だけ、sample しない）、zs の format に DEPTH_STENCIL_ATTACHMENT の feature。render pass は depth/stencil attachment を 1 つ（`depth`・`zs_format`・stencil の load/store op）、colour 無しの pass は target 1 つ（attachment 無し、internal R8G8B8A8、何も書かない） |
| `vk-pipeline.c` | `depth_state`: depth の test・write・compare、stencil の test と 2 face、raster の depth bias、dynamic の bias・compare mask・write mask・reference の印 |
| `vk-command.c`・`vk-build.h`・`vk-queue.c` | vkCmdSetDepthBias・SetStencilCompareMask・WriteMask・Reference を record し、`mali_vk_draw_set_state` で build に保つ（face の mask で front・back） |
| `vk-draw.c` | depth/stencil attachment の有る pass: FBD・ZS 拡張・RT の順に確保、`pass_depth`（view の level・layer の address・pitch・level の bytes、clear 値の depth の float と stencil、store で write back、LOAD で preload の bit、clear して store する aspect は clean tile write と frame shader の ALWAYS）、framebuffer pointer の ZS の bit。draw ごとに `draw_depth`（pipeline の状態に pass の aspect と program の writes_depth・writes_stencil、dynamic の値）で ZSD と kill/update。depth の clamp を viewport の minDepth・maxDepth に。colour target も view の level・layer に。attachment の無い pass の draw は前と同じ語 |
| P7-B: `vk-memory.c`・`vk-queue.c`・`vk-meta.c`・`vk-preload.c`・`vk-device.c` | image の mipLevels（level 0 から 1 texel まで、最大 14）、`mali_vk_image_level_address`・`mali_vk_image_level_pitch`、subresource layout の level、view の baseMipLevel・levelCount、sampler の minLod・maxLod・bias と mipmap mode（範囲 0 で bias 無しなら従来の MIPMAP_NONE）、texture の plane を layer×level（layer が外、level が内、pan_texture.c の順）、size の query、copy・blit・clear color image・compute の blit を level ごとに、preload の texture を view の level・layer に、maxMipLevels 14、zs の format の DS attachment の usage |
| tests | `tests/mali-vk-surface-host-test.c`（新、155 checks）、`tests/mali-vk-desc-host-test.c`（level の texture、89 checks）、`c5-build-check.sh` に vk-surface の行 |

判断（推しで進めた所）:
- depth・stencil の LOAD は FBD の「Z Preload Enable」「S Preload Enable」（v10.xml に有る field）で。**Mesa はこの bit を使わず、frame shader の pre-frame の draw で読み戻す**。実機で depth の LOAD が効かない・fault するなら、bit を外して frame shader（ZS_EMIT で書く preload の program）にする。desktop の経路に depth は無い。
- kill/update の「program が memory に書く」は false にした（program に印が無い）。storage に書く fragment program と depth test の組み合わせは Mesa と違いうる。
- D24S8 の depth aspect の buffer→image の copy は 4 byte を書くので stencil の byte も上書きする。stencil aspect の copy は log を出して飛ばす。pass の中の vkCmdClearAttachments の depth・stencil の aspect、vkCmdClearDepthStencilImage は未対応（前と同じ）。
- plane の slice stride（word 6）は level の bytes を書く（Mesa は 2D・1 sample で 0）。1 level で今までの値のまま。

確かめ（host と build だけ）: 下の「確かめ」の i05 の行。

## 判断（推しで進めた所）

- **同期の submit**: 1 回の vkQueueSubmit は GPU の終わりを待ってから返る（5 s で timeout）。fence・job の marker・frame の callback はその場で完了できる。scheduler は group 1 つ・slot 1 つの直列で、session の切り替えは AS の table の差し替え（group は動いたまま、ring は空）。速さより確かさ。
- **AS の差し替え**: 動いている group の AS slot の TRANSTAB を、ring が空の間に disable（cache の clean）→ enable で替える。**未確認**: firmware がこれを許すか。だめなら group の terminate・config・start に替える（`mali_queue_stop`・`mali_queue_open` は既にある）。
- **転送は CS の loop**: copy・fill・clear・同じ大きさの blit を front end の LOAD/STORE_MULTIPLE の loop で行う（GPU の上で動き、shader が要らない）。拡大・縮小・形式の変換の blit は走らせず 1 行を出す。
- **render pass の LOAD**（D2 で置き換え。下の D2 の判断）: ~~tile は clear 色で始まり、前の内容を読み戻さない~~。
- **tiler の heap**: 8 MiB の 1 chunk を pass ごとに使い直す。HEAP_SET・heap の context・TILER_OOM の処理は無い（溢れたら FAULT として出る）。
- **tile の大きさ**: 16×16（256 px）、colour buffer は target あたり 1 KiB。hierarchy mask は TILER_FEATURES の levels から Mesa の手順の前半で。
- ~~**workgroup memory（shared）を使う compute** は走らせない（1 行）~~（D3 で実装、実機未試験。下の D3 の判断）。
- **image は linear だけ**（AFBC・tiled 無し）、1 level・1 layer・1 sample。
- **float**: kernel は `-mgeneral-regs-only` なので、viewport の変換と clear 色の unorm8 は整数（16 bit の小数の固定小数点）で計算する。
- **display（gpu0）への出し方**: render の node は image の export の backing（連続の page）を出す。display の node の import は D2 で実装（下）。

### D2 の判断（q1675-i03）

- **compositor の範囲**: Keiland の compositor（`userland/desktop/wayland/`）の LOAD の pass は全て colour attachment 1 本（attachment 0）、format は出力の format（`compose->format`、BGRA8 か RGBA8 の UNORM の見込み）、damage の frame は render area を damage の矩形にする。よって pre-frame の draw は **colour target 0 の LOAD だけ**を扱う。target 1 以上の LOAD は従来どおり（1 行）。
- **INTERSECT（primitive が触れる tile だけ preload）**: Mesa の panvk と同じ。LOAD の target は clean tile write を立てないので、primitive の無い tile は書き戻されず memory の前の内容が残る（preload の帯域が要らない）。CLEAR の target があれば ALWAYS（pan_fix_frame_shader_mode と同じ）。
- **CLEAR の target に clean tile write**（範囲の際の修正）: 今まで RT に clean tile write が無く、CLEAR の pass で primitive の無い tile が clear されずに古い内容のまま残っていた（Mesa の pan_get_clean_tile_info は clear の target に立てる）。LOAD と同じ field なので一緒に直した。DONT_CARE は立てない。
- **program**: compiler の口 `mali_shader_compile` に SPIR-V（glslc で作り C の配列に）。FragCoord は x・y を 1 成分ずつ読む形にした（vec4 で読むと compiler が z・w の LD_VAR_SPECIAL を出し、primitive の無い frame shader で何を読むか分からないため）。compile の結果: 256 byte、61 register（64 の割当）、preload r59・r60、ATEST→BLEND（host の compiler で確かめた）。Mesa の frame shader は ATEST を省いて FORCE_EARLY にするが、ここは ATEST 付きの program に FORCE_EARLY の kill・update（Mesa は ATEST 付きの通常の draw でも FORCE_EARLY を使う）。
- **作る時と置き場**: program は node で 1 回（render の `preload`、guard で公開、同時の compile は片方を捨てる）、boot では compile しない（最初の LOAD の pass の時）。code は session ごとの bo（session の resource として close で消える）。stage に毎回写す案は、同じ address に違う code が来る時の命令 cache が気になるので採らなかった。
- **blend**: pre-frame の draw の blend は通常の draw と同じ `mali_desc_blend`（fixed function、replace、4 成分、memory format は `blend_format`）。Mesa は OPAQUE の mode と round to FB precision・sRGB の bit を使うが、通常の draw と揃えた（通常の draw の blend が正しければ preload も正しい、の形）。
- **DCD の値**（Mesa panvk の cmd_emit_dcd の colour preload から）: allow forward pixel to kill・to be killed、kill と ZS update は FORCE_EARLY、clean fragment write、sample mask 0xffff、render target mask と flags 2 の write mask は target 0、depth 0〜1、FAU 無し、TSD は pass のもの、blend 1 本。DCD は 3 本（pre-frame 0・1・post）を確保し 0 だけ詰める。
- **失敗の時**: compile の失敗・形の不一致は node で 1 度 1 行、code の bo の失敗は session で 1 度 1 行、どれも従来の動作（cleared tile）。pre-frame の draw を含む submission が GPU で FAULT・timeout したら node の preload を止める（1 行）。その session は従来どおり lost になる（それは止められない）。boot の経路は通らない。
- **display の import**: libvulkan は import が ENOTSUP なら copy の route に戻る（`wsi-swapchain.c`、FORMAT_NOT_SUPPORTED は fallback、他の error は失敗）ので、形が合わない物は全て ENOTSUP にした。SHARED・FOREIGN を constraints に出すと、libvulkan は direct（render の node の linear の image に直に描く）か GPU-copy の route を試す。import は render の node の export を bo の参照で保持する（core の pin は resource_destroy までで、plane が見ている間に destroy されると memory が先に消えるため。powervr の lookup と同じ考え）。CPU の copy は render の uncached の view から。flip は §5 の (a) の条件（画面と同じ幅・高さ・pitch・byte 順、16 byte、4 GiB 未満、角と中央の alpha）。render の bo は 4 GiB 未満（MALI_DMA_LIMIT）。
- **範囲の外にした物**: 3（clear attachments・拡大の blit）。compositor は vkCmdClearAttachments を使わない。拡大の blit は複数の head の mirror（`heads.c`）だけ。cursor の 1:1 の形式の変換の blit は出力が B8G8R8A8_UNORM でない時だけ（`cursor-thread.c`）で、今は走らない（1 行）。

### D3 の判断（q1675-i04）

- **desktop の経路を変えない形**: WLS の無い dispatch は `mali_vk_build_thread_storage` → `storage_descriptor(…, NULL)` で前と同じ語（`mali_desc_thread_storage` だけ）、staging register は同じ順の同じ命令（関数に移しただけ）。blit は「同じ形式・src の幅と高さが正・dst が同じ大きさ」の region だけが前の CS の copy の loop に入り、その条件は前の通る条件と同じ（前は src が逆向きで dst も同じだけ逆向きの region が copy の経路に入って幅が負のまま回っていた。それは compute に回る）。clear attachments・vk-draw.c・vk-preload.c は変えていない。stream を作る前に `meta_used` を 0 にするのは session の field だけ。
- **WLS の instance の数**（Mesa の pan_calc_wls_instances の値を手順として）: core あたり = min(dispatch の workgroup の数を各軸で 2 の冪に切り上げた積, ⌈2048 / workgroup の thread の数⌉ を 2 の冪に)。2048 は TLS と同じ「どの Valhall の core の thread の数より大きい」推し（GPU の THREAD_MAX_THREADS・THREAD_FEATURES は読んでいない。mali-gpu.c は D3 の所有の外）。全体（instance の bytes × instance × core の番号の範囲）が 32 MiB を超えたら instance を半分ずつ減らす。Mesa の注記どおり instance が足りない時は hardware が workgroup を待たせるので、少なくても正しさは変わらない（速さだけ）、という前提。**未確認**。
- **WLS の memory**: submission の stage から 4096 byte 揃え、clear しない（shared は workgroup の始めに未定義）。4 GiB の線を跨いだら 1 度だけ取り直す。shared の bytes が 1 MiB を超える program（limit は 16384 なので来ない）は 1 行で submission を失敗させる（前の拒否と同じ結果）。
- **拡大の blit は compute の meta program**: image は linear だけなので src・dst を storage buffer（語の配列）として扱う。push constant に src の角、16.16 の固定小数の歩幅（dst の 1 texel あたり、flip は負）、dst の幅・高さ、pitch と layer の大きさ（語）、src の大きさ（clamp to edge）、flag（linear・R と B の入れ替え）。dst が逆向きなら dst を正の向きにして src の向きを逆に。kernel は float を使えない（`-mgeneral-regs-only`）ので歩幅は整数の除算で kernel が出し、shader も整数だけ（linear は 4 texel を 1/256 の重みで byte ごとに 2 段の lerp、各段で丸め。誤差は 1 以下）。nearest も同じ式で重み 0（4 回同じ texel を読む。帯域より式の 1 本化を採った）。dst の address は region の左上の texel から、src は base layer から、layer は workgroup の z。
- **形式**: RGBA8・BGRA8 の UNORM と sRGB の 4 つの間だけ（順が違えば R と B を入れ替え）。**sRGB は UNORM の byte として扱う**（filter は符号化された値で重み付け。spec は線形の空間での filter を求めるが、desktop の用途（mirror・cursor）では差が小さい推し）。他の形式・3D（z の範囲が 0〜1 でない）は従来の 1 行で skip。
- **glslc の -O 無し**: compiler は OpFunctionCall を lower しない（`instruction with execution semantics is not lowered`。glslc の -O 無しの出力は関数を inline しない）ので、blit の GLSL は関数無し・early return 無しで書いた（compiler の不足として P7 に報告。compiler は A3 の所有）。
- **clear attachments**（D3 の続き）: 色の attachment ごと・矩形ごとに 1 本の draw。vertex program（gl_VertexIndex の bit 0・1 で矩形 (x0, y0, x1, y1) の 4 隅、pixel の座標で w 1）を triangle strip の 4 頂点・1 instance で、viewport の変換は恒等（scale 1・1・1、offset 0・0・0 の float の bit を FAU に）。fragment program は clear の色（record の 4 語、float の bit）を push の FAU から location L に。L は SPIR-V の Location の語を差し替えた location ごとの program（8 本、使う時に node で 1 回 compile）。blend は target L が replace の 4 成分、他の target は color mask 0。DCD1 の render target mask は target L だけ。scissor は矩形と render area・framebuffer の交わり。ZSD は全て通す物、kill は weak early、cull 無し。TSD は pass の物。resource table は空の 4 本（vertex と fragment で共有）。staging register は通常の draw と同じ番号を全て書くので、次の draw に clear の値は残らない。bind された pipeline・set・push・viewport・scissor（build の state）は読みも書きもしない。
  - 範囲: color の aspect で値が colour（tag 0）の物だけ。STORE しない target・render pass の外の target・面積 0 の矩形は何もしない。`baseArrayLayer` が 0 でない矩形（pass は 1 layer）と program が無い時は従来の 1 行（`a clear of attachments inside a pass is not run`、submission で 1 度）で、その矩形だけ skip（同じ record の他の矩形は描く）。
  - 判断: 通常の `draw()` を共有の関数に分けず、clear の発行を別に書いた（desktop の draw の stream を確実に変えないため。register の並びは同じ）。clear の draw は record の順に tiler に入るので、前後の draw との順序は Vulkan のとおり。
- **depth と sample mask を書く fragment program**（A3 の案、D3 の続き）: `writes_depth` で ZSD word 4 の Depth source を 3、DCD0 の kill と ZS update を Force Late（Mesa の prepare_dcd の手順）。`writes_sample_mask` で DCD0 の bit 20。depth の attachment はこの driver に無いので、depth の値は捨てられる（gl_FragDepth の program が拒否されずに走る、が目的）。**discards の時の bit 20 は足していない**（desktop の draw の DCD が変わるため。夜の後の案）。desktop の fragment shader 41 本（`plan/tools/nvrtx/sm75-desktop-extract.py` が出す物）を host の compiler に通して、`writes_depth`・`writes_sample_mask` が全て 0（下の確かめ）なので desktop の draw の語は変わらない。
- **既存の気づき（変えていない）**: `vk-desc.c` の `NO_WORKGROUP_MEMORY` は word 1 に 0x80000000 をそのまま入れている。v10.xml の WLS Instances は 1:0 の 5 bit で modifier が log2、既定の NO_WORKGROUP_MEM（0x80000000）は gen_pack では util_logbase2 で 31（word 1 = 0x1f）になる。今の語は instance の数 log2 0（1 個）・size scale 0・base 0 で、bit 31 は field の外。shared を使わない program には影響しない見込みだが、desktop の経路の語なので変えず P7 への案に置く。

## 夜の serial で見る行（`mali:` で始まる。gpu.md §9 の行の後）

1. `mali: queue: group 0 stream 0 started on an empty ring (… N registers)`。**N が 96 未満**なら executor は `the command stream has N registers, 96 needed` で submit を拒否する（render の node 自体は出る）。
2. `mali: queue self-test: NOP and SYNC_ADD64 ran (sync 0 -> 1, 40 bytes through the ring)`。失敗なら `mali: queue self-test failed (…): sync …, fault …, fatal …` と `mali: queue self-test: group 0 stream 0 insert … extract … active … at … wait … blocked … resource … fault … fatal … group state …`。extract が 0 のままなら doorbell か ring の address（AS 1）の問題、fatal の下の byte が exception type。
3. `mali: queue: group 1 stream 0 started on an empty ring (… ring at 100000, doorbell 2 …)`（render の stream）。
4. `mali: render node ready (stream on group 1, address space 2, ring at 100000)`。失敗なら `mali: render: …`・`mali: render node not registered (…)`。
5. desktop の起動の後: `mali: vk: command frame failed opcode=N error=E bytes=B`（executor が拒否した command。N は uapi/gpu-op.h の番号）、`mali: vk: stage S not compiled (E) at I: reason`（C の compiler の拒否）、`mali: vk: recorded command N is not run (E)`。
6. GPU の失敗: `mali: render: submission K failed (E): stream … bytes …, fault …, fatal …, F faults` と `mali: render: group 1 stream 0 insert … extract … at …`、続けて `mali: MMU fault …`（gpu.md §9）。fault の address と `at`（CS の命令の位置）で、どの命令・どの descriptor かを stage の中で切り分ける。
7. `mali: vk: a pass that loads its attachment draws on a cleared tile`・`a scaling or converting blit is not run`・`a blend the fixed function does not take …`・`a clear of attachments inside a pass is not run` は機能の不足の印（1 度だけ）。D2 の後、LOAD の行は session ごとに 1 度で、target 1 以上の LOAD か pre-frame の draw が作れない時だけ出る。
8. （D2）pre-frame の draw: `mali: vk: the program that loads an attachment is not compiled (E) at I: reason; loads draw on cleared tiles`・`mali: vk: the program that loads an attachment <理由>; loads draw on cleared tiles`（compile の失敗・形の不一致、node で 1 度）、`mali: vk: the code that loads an attachment has no memory (E) …`・`… is not mapped (E) …`（session で 1 度）。pre-frame の draw を含む submission の失敗: 6 の行の後に `mali: render: submission K ran a pre-frame load; later loads draw on cleared tiles`。fault の address が FBD の frame shader の DCD・texture・plane・SPD・pre-frame の code の bo のどれかなら preload を疑う。**行が何も出ず damage の外が消えなければ成功**（目で見る）。
9. （D2）display: `mali: display lease N: shared frames flipped`（import を OSD1 に直に。期待する行）・`shared frames copied`（import を CPU で写す。flip の条件を外れた）・`copied-in frames flipped`・`copied-in frames copied`（従来の copy の route）。import の拒否は `mali: display: an image is not imported (E): the image <理由>; its frames are copied`（1 度）。gpu.md §5 の「display lease N: frames flipped / copied」の行の形が変わった。

10. （D3）WLS の dispatch: 前の `mali: vk: a dispatch with workgroup memory is not run` は出なくなる。shared が 1 MiB を超える program だけ `mali: vk: a dispatch of N bytes of workgroup memory is not run`（session で 1 度、submission は失敗）。shared を使う compute の submission が FAULT したら 6 の行（fault の address が stage の WLS の memory の範囲か見る）。**行が出ず結果が正しければ成功**（compute の出力を照合する試験は無い）。
11. （D3）拡大・縮小・flip・RGBA↔BGRA の blit: `a scaling or converting blit is not run` は、4 byte/px の RGBA8・BGRA8（UNORM・sRGB）以外・3D・program が無い時だけ出る（submission で 1 度）。program の不足は node で 1 度 `mali: vk: the program that scales or converts a blit is not compiled (E) at I: reason; that work is not run` か `mali: vk: the program that scales or converts a blit <理由>; that work is not run`、code は session で 1 度 `mali: vk: the code of the program that scales or converts a blit has no memory (E) …`・`… is not mapped (E) …`。blit を含む submission が失敗したら 6 の行の後に `mali: render: submission K ran the executor's own programs (1); later blits and clears that need them are not run`。mirror（`heads.c`）の拡大の画面・cursor の形式の変換の blit が出れば成功（目で見る）。
12. （D3 の続き）clear attachments: 前の `a clear of attachments inside a pass is not run` は、layer 0 でない矩形と program が無い時だけ出る。program の不足は node で 1 度 `mali: vk: the program that places a clear inside a pass …`（vertex）か `mali: vk: the program that colours a clear inside a pass …`（fragment）、`… is not compiled (E) at I: reason; that work is not run` か `… <理由>; that work is not run`。code は session で 1 度 `mali: vk: the code of the program … has no memory (E)`・`is not mapped (E)`。clear を含む submission の失敗は 6 の行の後に `mali: render: submission K ran the executor's own programs (M); …`（M の bit 1 が vertex、bit 2+L が location L）。desktop（compositor）は vkCmdClearAttachments を使わないので、desktop の起動では新しい行は出ない見込み。

## 確かめ

- build: `cd /home/awe/zedBSD-worktrees/p7-ws313b && timeout 1200 make -j8 ZEDBSD_CONFIG=config/ci/config-odroid-c5-keiland.mk BUILD=build/pd build/pd/vmunix` → exit 0、warning 0（2026-10-29、`grep -c " warning:" build/pd/build.log` = 0、C の未 commit の compiler の版を含む worktree で）。
- host: `cc -std=c11 -Wall -Wextra -Werror -Isrc -Iinclude tests/mali-cs-host-test.c src/drivers/gpu/mali/cs/cs-build.c` → `all checks passed (61)`。`tests/mali-vk-desc-host-test.c src/drivers/gpu/mali/vk/vk-desc.c` → `all checks passed (47)`（出力は build/pd/mali-check/）。
- 未実施: 実機（全部）、QEMU（S7D・Mali の machine が無い）、libvulkan の regression（libvulkan は変えていない）、executor の wire の host 試験（command buffer から stream の組み立てを偽の memory で回す試験は書いていない）。

- （D2）build: `cd /home/awe/zedBSD-worktrees/p7-ws313c && timeout 1800 make -j6 ZEDBSD_CONFIG=config/ci/config-odroid-c5-keiland.mk BUILD=build/d2 build/d2/vmunix > build/d2.log 2>&1` → exit 0、`grep -c ' warning:' build/d2.log` = 0（2026-10-29 14:30 頃、A2 の未 commit の compiler の変更を含む worktree で）。`git diff --check` 0。
- （D2）host（出力は build/d2-host/）:
  - `cc -std=c11 -Wall -Wextra -Werror -Isrc -Iinclude tests/mali-vk-frame-host-test.c src/drivers/gpu/mali/vk/vk-frame.c src/drivers/gpu/mali/vk/vk-desc.c` → `all checks passed (49)`。
  - `cc -std=c11 -Wall -Wextra -Werror -Isrc -Iinclude tests/mali-scanout-host-test.c src/drivers/gpu/mali/display/scanout.c` → `all checks passed (19)`。
  - `cc -std=gnu11 -DKERN_USER_ABI_LP64 -Iinclude -Isrc -I. -idirafter libc/include -Wall -Wextra -Werror tests/mali-preload-host-test.c src/drivers/gpu/mali/compiler/va-compile.c src/drivers/gpu/mali/compiler/va-encode.c src/drivers/gpu/mali/compiler/va-schedule.c src/drivers/gpu/compiler/spirv.c src/drivers/gpu/compiler/optimize.c -lm` → `program: 256 bytes, 61 registers, preload 1800`、`all checks passed (15)`。書き出した SPIR-V は comment の GLSL を `glslc --target-env=vulkan1.0 -fshader-stage=frag`（shaderc 2025.2）で作った物と `cmp` で一致。
  - その SPIR-V を compiler の host 試験（`tests/mali-compiler-host-test.c` を `mali-compiler-desktop-test.sh` と同じ flag で build、`--trials 50`）に通して `PASS`（全ての語が ISA.xml の表で decode でき、1 lane の模型が IR の参照と一致）。listing に LD_VAR_SPECIAL が無い（0 個）。
  - 既存の `tests/mali-cs-host-test.c` → `all checks passed (61)`、`tests/mali-vk-desc-host-test.c` → `all checks passed (47)`（変えていないことの確かめ）。
- （D2）未実施: 実機（全部。pre-frame の draw が GPU で動くか、FBD の frame shader の field、INTERSECT と clean tile write の振る舞い、OSD1 の flip の import）、QEMU（無い）、libvulkan の route の選び方の実際（SHARED を出した後の direct・GPU-copy・copy の別は夜の 9 の行で見る）、executor の wire の host 試験。

- （D3）build: `cd /home/awe/zedBSD-worktrees/p7-ws313d && timeout 1800 make -j6 ZEDBSD_CONFIG=config/ci/config-odroid-c5-keiland.mk BUILD=build/d3-k build/d3-k/vmunix > build/d3-k.log 2>&1` → exit 0、`grep -c ' warning:' build/d3-k.log` = 0（2026-10-29 16:17、A3 の未 commit の compiler の変更を含む worktree で）。`git diff --check` 0。
- （D3）host（出力は build/d3-host/）:
  - `cc -std=c11 -Wall -Wextra -Werror -Isrc -Iinclude tests/mali-vk-desc-host-test.c src/drivers/gpu/mali/vk/vk-desc.c` → `all checks passed (78)`（WLS の 13 を含む）。
  - `cc -std=gnu11 -DKERN_USER_ABI_LP64 -Iinclude -Isrc -I. -idirafter libc/include -Wall -Wextra -Werror tests/mali-vk-meta-host-test.c src/drivers/gpu/mali/vk/vk-meta-spirv.c src/drivers/gpu/mali/compiler/va-compile.c src/drivers/gpu/mali/compiler/va-encode.c src/drivers/gpu/mali/compiler/va-schedule.c src/drivers/gpu/compiler/spirv.c src/drivers/gpu/compiler/optimize.c -lm` → `all checks passed (130)`。blit 3072 byte・60 register・preload 0e80・FAU 18 語（buffer は FAU 14・16）、clear の vertex 384 byte・61 register・preload 1800・FAU 10 語、clear の colour 0〜7 各 128 byte・61 register・preload 1000・FAU 4 語。書き出した SPIR-V は glslc の出力と `cmp` で一致、comment の GLSL を取り出して glslc し直した物も一致。
  - 書き出した 4 本（blit・vertex・colour 0・colour 3）を `tests/mali-compiler-host-test.c` と `tests/mali-compiler-model.c`（`mali-compiler-desktop-test.sh` と同じ flag、asan・ubsan）で `--trials 50` → 4 本とも `PASS`（ISA.xml の表で decode でき、1 lane の模型が IR の参照と一致）。
  - blit の式（GLSL と同じ整数の式）を Python で書き直して手で確かめた: 2×2→4×4 の nearest、x の flip、2→4 の linear（0・0x40・0xbf・0xff）、4→2 の linear、R・B の入れ替え。試験の file にはしていない。
  - 既存: `tests/mali-vk-frame-host-test.c` → 49、`tests/mali-cs-host-test.c` → 61、`tests/mali-preload-host-test.c` → 15（変わらないことの確かめ）。
- （D3）未実施: 実機（全部: TSD の WLS の語、instance の数の振る舞い、blit の compute の run）、QEMU（無い）、`c5-build-check.sh` の全体（P7 が流す）。
- （D3 の続き）build: 同じコマンド → exit 0、`grep -c ' warning:' build/d3-k.log` = 0（2026-10-29 16:25 頃、A3 の未 commit の compiler の変更を含む worktree で）。`git diff --check` 0。
- （D3 の続き）host（出力は build/d3-host2/）: `tests/mali-vk-meta-host-test.c`（上と同じ cc の行）→ `all checks passed (130)`（clear の vertex と location 0〜7 の colour の形を含む）。`tests/mali-vk-desc-host-test.c` → 78、`tests/mali-vk-frame-host-test.c` → 49（変わらないことの確かめ）。
- （D3 の続き）desktop の draw が変わらないことの確かめ: `python3 -I plan/tools/nvrtx/sm75-desktop-extract.py <worktree> build/d3-desk/spv` で出した 60 本のうち fragment の 41 本を host の Mali compiler に通し（小さな probe、build/d3-desk/）、`writes_depth`・`writes_sample_mask` が 41 本とも 0（build/d3-desk/flags.txt）。
- （D3 の続き）未実施: clear の draw の host 試験（vk-draw.c は kernel に依るので、stream と descriptor を偽の memory で組み立てる試験は無い。program の形だけ確かめた）、実機（clear の draw が GPU で動くか、location 1 以上の fragment program の ATEST、恒等の viewport の y の向き）。

- （i05、2026-10-29）`sh plan/ws313/tests/c5-build-check.sh i05c` → ALL PASS（5 つの vmunix の warning 0、host: vk-desc 89・vk-surface 155・vk-frame 49・meta 125・compiler 37 行・desktop 60/60 他）。desktop の listing 67 本は前と SAME。FBD の修正の commit だけで odroid-c5・odroid-c5-keiland の vmunix warning 0、vk-frame 49・vk-desc 78。
- （i05）未実施: 実機（全部: FBD の 128 byte、ZS 拡張と ZSD、kill/update、depth の preload の bit、level の plane の並び、sampler の LOD）、QEMU（無い）、vk-draw.c の depth の経路の host 試験（kernel に依るので無い。packer は vk-surface の試験）。

## 残り（夜の結果で直す順）

1. self-test と render の stream の起動（上の 1〜4）。
2. compute の dispatch（`vkdemo` の compute、C の compute shader）: RUN_COMPUTE の task increment（今は 1）、TSD、FAU の pointer の形。
3. 三角形: RUN_IDVS の staging register、tiler context・heap、FBD・RT、blend の memory format（BGRA を RGBA 順にしている、未確認）、ZSD。
4. compositor: ~~LOAD の pass の pre-frame の shader~~（D2 で実装、実機未試験）、~~clear attachments~~（D3 で draw、実機未試験）、~~拡大の blit、cursor の形式の変換の blit~~（D3 で compute の blit、実機未試験）、AFBC 無しの性能、target 1 以上の LOAD。
5. 改善: 非同期の submit（ring に複数の job、IRQ で完了）、TILER_OOM、~~shared memory~~（D3 で WLS、実機未試験）、~~mip・depth~~（i05、実機未試験）、WLS の instance の数を GPU の THREAD_MAX_THREADS・THREAD_FEATURES から。

## P7 への案（変えていない）

- ~~（D3）`plan/ws313/tests/c5-build-check.sh` に `tests/mali-vk-meta-host-test.c` の行~~（P7 が足した、2026-10-29。引数無しで流す）。
- ~~（D3）`vk-desc.c` の `NO_WORKGROUP_MEMORY` の語~~: **P7 が v10.xml の log2 の形（word 1 = 0x1f）に直した**（2026-10-29、q1675-i04）。判断: Mesa（panvk・gallium）が v10 の実機で使う語は gen_pack の util_logbase2(0x80000000) = 31 で、今までの 0x80000000 は field としては「instance 1 個・size scale 0・base 0」という Mesa が出さない語だった。どちらも G310 の実機では未試験だが、資料の上で正しく実機で使われている形に合わせる方を推しとした。全ての TSD（compute の dispatch、pass の draw、pre-frame の DCD が指す TSD）の word 1 が変わるので、**夜に最初の compute・draw で TSD の fault（`mali: render: submission K failed` の fault の値が TSD の descriptor を指す）が出たら、まずこの 1 語（`vk-desc.c` の `NO_WORKGROUP_MEMORY`）を 0x80000000U に戻して比べる**。`tests/mali-vk-desc-host-test.c` の 2 か所も 0x1fU に。
- ~~（D3）compiler（A3）: OpFunctionCall の inline~~（i05 で実装、Mali だけ feature を立てる）。
- （D3 の続き）夜の後: discards の program の DCD に Shader Modifies Coverage（bit 20）を足すか（Mesa の prepare_dcd は discard を coverage の変更に数える。desktop の draw の DCD が変わるので今は足していない）。

- ~~`display/display.c` の `import_image`~~（D2 で実装、実機未試験）。
- （D2）gpu.md §5 の route の行の形（`display lease N: shared|copied-in frames flipped|copied`）と §13 に pre-frame の draw・clean tile write の 1 行を足すこと（gpu.md は D2 の所有の外）。
- libvulkan: 変更なし（powervr と同じ capset 4・vendor の印 SDBZ・flags 7 を返す）。
