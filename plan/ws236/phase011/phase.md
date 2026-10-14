<!-- awesome-plan project=zedbsd record=ws236-p011 -->

# ws236-p011: 案 1: decoder の出力を GPU の texture に、表示を GPU で

Parent: [WS236](../ws.md)
Status: in-progress（2026-10-16、P3。段 a は T1-674 で結果済み（B-2）、段 b は T1-677 で PASS、段 c・d・e は T1-681 で GPU の道が動いた（色も正しい）、速さ（21 fps）が未達 → executor の VPU 待ちを譲る直しと presenter の 2 frame 重ね、T2 の依頼 4 待ち）
Phase disposition: normal
実行者: P3（worktree `/home/awe/zedBSD-worktrees/p3`、branch `agent/p3-ws236e`）
依存: [ws236-p009](../phase009/phase.md)（案 1 の設計）、[ws236-p010](../phase010/phase.md)（decode の depth、cleared）

## ユーザーのゴールと判断（原文と要旨）

- ゴール（2026-10-15 夜）:「Piでデコーダから変換なしでGPUのテクスチャにして、60fps出るようになること。」
- 判断（2026-10-16、クリック）: 案 1 を WS236 の Phase で進める（WS141・WS202・WS122 の file も WS236 の範囲として触ってよい）。
  GPU が直に sample する値では firmware の「2〜15 が 1 小さい」を直さない（bit 単位の読み戻し＝vkvideo-probe の時だけ補正）。
  TFU の YUV 入力の probe をする（Pi で試し、使えればその道を使う）。

## 目標と合格

- 目標: Pi 4 の Video Player で、decode の NV12 の image を CPU で画素を触らずに V3D で sample し、YCbCr→RGB と縮小を GPU で行って表示する。
- 合格（Pi 4 実機、T2）:
  1. 1080p の H.264（`plan/ws236/tests/high-1080p30-10s.mp4`・`baseline-1080p30-10s.mp4`、Video.mp4 があれば）を最後まで、stream の速さで再生（late がほぼ 0）。`VIDEOPLAYER DRAW` の picture の時間が 1 枚 数 ms。
  2. 毎 frame の CPU の画素の仕事が無い: kernel の place（補正の写し）、libmedia の copy、videoplayer の CPU の変換が通らない（log の行で確かめる）。
  3. vkvideo-probe の 3 本が引き続き全 frame で参照と一致（readback の補正は残す）。
  4. 5330（i915）の Video Player の道と、Pi の他の Vulkan の client（compositor）を壊さない。

## 段

| 段 | 内容 | 主な file | 状態 |
| --- | --- | --- | --- |
| a | TFU の probe（起動の診断、boot の `v3d.tfu_probe=1` の時だけ）: (1) 1・2 byte の texel の raster→UIF が CPU の配置と合うか、512x480 R8・256x240 RG8 の時間（1080p の見積もり）、(2) YUV 入力の候補の設定（入力の format 0〜10、NV12 と I420 の分け方）で RGBA8 を書くか、BT.601・709・full のどれと合うか | `v3d-tfu-probe.c`（新規）、`v3d-diagnostic.c`、`v3d-job.c`・`.h`（TFU の job に chroma・U plane の address）、`native-texture.c`（1・2・4 byte の UIF の大きさと texel の offset）、`stage.c` | 済み（T1-674: R8・RG8 は使える、YUV 入力は当たらず → B-2） |
| b | VPU の bulk を NV12 の image の memory へ直に（place を無くす）。補正（2〜15 の +1）は buffer への readback の時だけ | `vulkan-video.c`、`rpi4-mmal.c`・`.h`、`vulkan-buffer-copy.c`、`vkvideo-probe`（`--sampled`） | T1-677 PASS（全 frame 一致、differing 0） |
| c | V3D: R8・RG8 の sampled image、NV12 の image に SAMPLED と plane の view（PLANE_0 を R8、PLANE_1 を RG8）、snapshot（raster→UIF）を 1・2 byte の texel に（TFU と CPU の予備）。a の YUV が当たればその道（B-1）を選ぶ | `vulkan-query.c`・`vulkan-resource.c`・`vulkan-input.c`・`.h`・`native-texture.c`・`native-state.h`・`vulkan-native-draw.c`・`.h`・`native-storage.c`・`.h`、`vulkan-video.c`（video の format の SAMPLED と plane の view の flag） | 実装・build・host 試験済み（実機は依頼 3） |
| d | libmedia: GPU の picture（decoder の image の memory を opaque fd で export、picture が image を持つ間はその slot に decode しない）。VkDevice は共有せず、app が fd を import する | `userland/desktop/libmedia/`、`userland/desktop/media-app/decoder.c`・`.h` | 実装・build・host 試験済み（実機は依頼 3） |
| e | videoplayer: 自前の presenter（KL_PRESENT_NONE、arm64 だけ）。picture の 2 plane を shader で YCbCr→RGB と縮小、bar・notice は内容が変わった時だけ overlay へ | `userland/desktop/videoplayer/` | 実装・build（rpi4・amd64 warning 0）、実機は依頼 3 |

## 段 a: TFU の probe（2026-10-16、P3）

### 作った物

- `src/drivers/gpu/bcm2711/v3d-tfu-probe.c`（新規）: boot の診断（V9 の UIF の自己試験の後、V10 の前）で、boot の行に `v3d.tfu_probe=1` がある時だけ走る。全ての結果は `v3d: tfu-probe` で始まる kernel の log の行。fault した試行は engine を reset して次へ（reset の失敗だけが診断の失敗）。既定（token なし）では何も変わらない。
  - plane: R8（texel 1 byte）を 96x40（部分の column と block の行）と 512x480、RG8 を 48x20 と 256x240。入力の stride は texel 単位（Mesa の v3dv と同じ）。全ての texel・channel を `bcm2711_native_texture_offset` の場所と比べ、`mismatches=`・最初の違い・出力の後ろ 64 byte の書き過ぎ・`us=` を出す。
  - yuv: 64x32 の 8 色（16x16 の領域、Y・Cb・Cr は範囲の端から離した値）。入力の format（ICFG の bit 18〜21）0（raster、NV12 と I420）、1（SAND128、1 列）、2（SAND256、1 列）、3〜10（公開の意味なし）を、出力 RGBA8（type 4）・UIF で。各領域の中心の画素を出し、BT.601 limited・BT.709 limited・BT.601 full と、R が先・B が先の並びで ±6 に入る数を数え、8 つ全部なら `MATCH` の行。
- `v3d-job.c`・`.h`: TFU の job に `chroma`・`u_plane`（と大きさ）を足し、register 0x410・0x418 に書く（今までは 0 固定。既存の job は 0 のまま）。0 でなければ 64 byte の整列と span を確かめる。
- `native-texture.c`・`native-state.h`: `bcm2711_native_texture_size_texel`（1・2・4 byte の texel の UIF の大きさ）と `bcm2711_native_texture_offset`（texel の byte の offset）。`bcm2711_native_texture_size` は 4 byte の版を呼ぶ（値は前と同じ）。段 c でも使う。
- `stage.c`・`bcm2711-private.h`: `bcm2711_stage_tfu_probe()`（token `v3d.tfu_probe=1`）。
- `platform/arm64/vmunix.mk`: `v3d-tfu-probe.c` を bcm2711 の source に。`plan/ws141/tests/v3d-hardware-host-test.*`: 新しい file と stage の stub。

### 確かめ（host と build）

| 確かめ | 結果 |
| --- | --- |
| `sh plan/ws141/tests/v3d-hardware-host-test.sh build/p3/ws236e-v3dhw` | PASS（probe は off の道） |
| `make -j16 BUILD=build/p3/rpi4 ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/p3/rpi4/vmunix` | `-Werror` で通る、`arm64 vmunix check: PASS` |

### T2 への依頼（Pi 4 実機 kei@10.0.30.2、QEMU 不要、約 15 分）

1. build（agent/p3-ws236e の段 a の commit、または Q1 の merge の後の main）: `make -j16 BUILD=build/<T>/rpi4 ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/<T>/rpi4/vmunix`（warning 0）。差し替えは kernel だけ。
2. Pi: `/boot/boot0/vmunix` を退避して差し替え、boot の行に `v3d.tfu_probe=1` を足し（`v3d.perf=1` と同じ置き方）、sync、reboot。
3. 起動後 `dmesg | grep -E 'tfu-probe|V9 TFU|V10'` を保存して返す（全行）。
4. 合否（この段）: probe が `v3d: tfu-probe end` まで進み、`V10 ... post-reset noop complete` が出て起動が続く（desktop が出る）。結果の数（mismatches・us・MATCH の有無）は合否ではなく、段 c の道を選ぶ材料。
5. 終わったら boot の行から `v3d.tfu_probe=1` を外す（kernel は残してよい。token が無ければ前と同じ振る舞い）。
6. 止め所: Pi が応答しなくなったら止めて報告（boot の途中の probe の reset で止まった見込み。前の kernel に戻す）。

### T2 の結果（T1-674、2026-10-16、Pi 4 実機、main に merge 済みの 98b1e481d）

- 起動: `tfu-probe end`、V10 の reset の後の noop complete、desktop まで起動（PASS）。結果 `plan/agents/T2/results-20261016-t1-674.md`・`t1-674-probe.log`。
- plane: R8 96x40・512x480、RG8 48x20・256x240 の全て `mismatches=0`・`overrun=0`（hardware の UIF の配置は `bcm2711_native_texture_offset` と一致）。時間 512x480 R8 105 us、256x240 RG8 56 us → 1080p の Y（2 MB）約 0.9 ms、UV（1 MB）約 0.45 ms の見込み。
- yuv: format=0（raster、NV12・I420）は全 0 の書き込み、format=1・2（SAND の 1 列）は領域 0・4 の中心だけ Y を 4 byte に複製した灰色（80808080・b4b4b4b4、他の 6 つは 0）、3・9・10 は一部だけ書き、4〜8 は全 0。`matches 1/1` は灰色（Cb=Cr=128）の領域が全ての行列に合うだけで、色の変換は起きていない。→ **YUV→RGBA の変換は既定の設定では出ない（B-1 は使わない）**。非公開の bit（係数の register の使い方、別の入力の bit）を探すのは見積もりの外なので打ち切り、**B-2（R8・RG8 の 2 枚を TFU で UIF に、YCbCr→RGB は shader）** を選んだ。

## 段 b・c: VPU の bulk を image へ直に、NV12 の plane を R8・RG8 の texture に（2026-10-16、P3）

### 約束（Vulkan から見た振る舞い）

- **SAMPLED の usage を持つ NV12 の decode 出力の image（以下「sampled の picture」）は、firmware の値をそのまま持つ**（2〜15 の 1 小さい値を直さない、ユーザーの判断 2026-10-16）。`vkCmdCopyImageToBuffer` の readback は値を直して書く（`bcm2711_vulkan_video_image_raw`）。SAMPLED の無い picture（今の libmedia・vkvideo-probe の既定）は前と同じ（kernel が直して写し、readback は写しのまま）。
- sampled の picture の作り方: `flags` に `MUTABLE_FORMAT`・`EXTENDED_USAGE`（他の flag は不可、NV12 以外は 0 のまま）、usage に `SAMPLED`（と `TRANSFER_SRC`）。plane の view: `aspectMask` = `PLANE_0` で format `R8_UNORM`（輝度）、`PLANE_1` で `R8G8_UNORM`（R に Cb、G に Cr、幅・高さは半分）。shader が sample すると R8 は (Y, 0, 0, 1)、RG8 は (Cb, Cr, 0, 1)。
- `vkGetPhysicalDeviceFormatProperties` は `R8_UNORM`・`R8G8_UNORM` に `SAMPLED_IMAGE`・`FILTER_LINEAR` を返す（plane の view の format として。R8・RG8 の image 自体は作れない）。NV12 自体の feature は変えない（YCbCr の sampler は無い）。
- 描く前に decode の frame が image に入っていることはアプリが fence で確かめる（decode を含む submission の fence は自分の frame の後に signal、p010）。描画の draw は decode の飛んでいる frame を待たない（copy・blit は待つ）。

### 作った物

- `rpi4-mmal.c`・`.h`: decode ごとの「target」（`drv_rpi4_mmal_decoder_target`・`untarget`）。frame の message が来た時、その pts に target があり、frame が NV12 で、buffer の先頭から輝度、pitch と chroma の offset が target と同じ、大きさが収まる時は、VCHIQ の bulk の受け先を target の物理 address（image の memory の連続の run、1 GiB の下、CPU からは uncached）にする（cache の invalidate なし）。合わない frame は bounce に受けて log（4 行まで）。bulk が終わらなかった target は `kept` を返し、呼び手はその run を永久に保つ。frame に `direct`。
- `vulkan-video.c`: decode の送りの前（window を開く前）に sampled の picture の run を `bcm2711_buffer_retain` して target を登録（`prepare_direct`・`begin_decode`）、flight が run を持ち、frame の受け取り・失われ・時間切れ・reset で target を外して run を release（`release_direct`）。direct の frame は place（CPU の写し）なしで、bounce の buffer はすぐ返し、held の写しを作らない。direct でない sampled の picture は補正なしで写す。frame が入った image の run に `bcm2711_v3d_memory_written`（texture の snapshot を作り直させる、`mark_written`）。
- `vulkan-buffer-copy.c`: sampled の picture の uncached の readback は `drv_rpi4_mmal_copy_samples` で直しながら写す。
- `vulkan-resource.c`・`vulkan-input.c`・`.h`・`vulkan-query.c`: 上の作り方・plane の view（view の `plane` 1・2）・format の feature。
- `native-texture.c`・`native-state.h`: texture の record に texel の形（`BCM2711_NATIVE_TEXEL_RGBA8`（0、既定）・`R8`・`RG8`）、type（R8 0、RG8 2、RGBA8 4）と swizzle（R8 (r,0,0,1)、RG8 (r,g,0,1)）、`bcm2711_native_texel_layout`、CPU の予備の変換 `bcm2711_native_texture_copy_texel`。
- `native-storage.c`・`.h`: snapshot の key に texel の byte 数（`bcm2711_native_snapshot_get_texel`、前の関数は 4 byte で呼ぶ）。1・2 byte は部分の更新をせず毎回全部を変換（picture は全部が変わる）。4 byte の CPU の変換は前の速い写しのまま。
- `vulkan-native-draw.c`・`.h`: texture の元を `native_texture_source`（image、offset、大きさ、pitch、texel）にし、view の plane から作る（`texture_source`）。TFU の変換（`prepare_conversion`）は texel の type と texel 単位の stride。同じ pass の中の再利用は (image, offset) で。
- `userland/tests/vkvideo-probe`: `--sampled`（sampled の picture を作り、map せず、decode の command buffer の後ろで両 plane を `vkCmdCopyImageToBuffer` で image と同じ配置の buffer に写して hash・dump する）。
- 試験: `plan/ws236/tests/texture-plane-host-test.c`・`run-texture-plane-test.sh`（新規、1・2・4 byte の UIF の offset と CPU の変換、R8・RG8・RGBA8 の record）。

### 確かめ（host と build、2026-10-16）

| 確かめ | 結果 |
| --- | --- |
| `make -j16 BUILD=build/p3/rpi4 ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/p3/rpi4/vmunix build/p3/rpi4/bin/vkvideo-probe` | warning 0、`arm64 vmunix check: PASS` |
| 同じ tree を VCHIQ・VIDEO=n で（build の中の一時の config） | warning 0、PASS |
| `sh plan/ws236/tests/run-texture-plane-test.sh` | PASS |
| `plan/ws141/tests/native-state-host-test.sh`・`vulkan-device-host-test.sh`（31 行 PASS、前と同じ）・`v3d-hardware-host-test.sh` | PASS |
| `run-mmal-wire-test.sh`・`run-h264-test.sh`・`run-vchiq-wire-test.sh`・`run-render-settle-test.sh` | all PASS |
| `plan/tools/style-check.py`（変えた file、HEAD との差） | 新しい指摘 0 |

- 未実施: Pi 実機（下の依頼）。plane の sample は段 d・e の videoplayer で見る。

### T2 への依頼 2（Pi 4 実機、QEMU 不要、約 20 分）: 段 b の direct の受けと readback の補正

1. build（agent/p3-ws236e の段 b・c の commit、または merge 後の main）: `make -j16 BUILD=build/<T>/rpi4 ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/<T>/rpi4/vmunix build/<T>/rpi4/bin/vkvideo-probe`（warning 0）。
2. Pi: kernel を差し替えて reboot（`v3d.tfu_probe=1` は付けない）。`build/<T>/rpi4/bin/vkvideo-probe` を `/home/kei/ws236/vkvideo-probe-p011` として置く（前の probe は残す）。
3. 前と同じ 3 本（`p-baseline-64`・`i-high-352-cqm`・`pb-high-352-pyramid`）を、新しい probe で (a) 今までどおり（`--expect=… --time --dump=/tmp/vk-<s>.dump`）と (b) `--sampled` を付けて（`--dump=/tmp/vks-<s>.dump`）各 1 回。終了 code と全出力。
4. 1080p: `vkvideo-probe-p011 --sampled --time /home/kei/ws236/pb-high-1080.h264`（`--expect` なし、hash の行と時間）と、同じく `--sampled` なしで 1 回（hash が同じ順に同じであること）。
5. host で dump-compare: (a)(b) の dump を `python3 -I plan/ws236/tests/dump-compare.py <dump> <outdir>` で、`differing samples` を報告（期待: 全 frame 0）。
6. `dmesg | grep -E 'rpi4-mmal: frame .* not its target|kept for good|v3d: video:|v3d: (EIO|execution failed)'` を保存。
7. 合否: (a)(b) とも rc 0・全 frame MATCH・differing 0、1080p の hash が `--sampled` の有無で同じ、`not its target's layout` の行が無い（あれば frame の pitch・chroma の値を報告）、`kept for good`・`execution failed` が無い。Video Player は今の main のまま（libmedia は変えていない）で、1 本（`baseline-1080p30-10s.mp4`）再生して落ちないこと（回帰）。

### T2 の結果（T1-677、2026-10-16、Pi 4 実機、main 78354757f）: 段 b PASS

- 3 本を (a) 今まで通り・(b) `--sampled` とも全 frame MATCH、dump-compare 56 件 differing 0、1080p の hash は `--sampled` の有無で同じ。dmesg に `not its target's layout`・`kept for good`・`execution failed` 無し。Video Player は前と同等（shown 39・late 261）で落ちない。結果 `plan/agents/T2/results-20261016-t1-677.md`。
- `--sampled` の decode time が長い（1080p 33→60 ms）のは probe の測り方: `--sampled` は decode の command buffer の後ろで両 plane を uncached の image から CPU が補正しながら読む copy をし、その時間が decode time に入る。GPU の道（段 e）は readback をしない。

## 段 d・e: libmedia の GPU の picture と videoplayer の自前の presenter（2026-10-16、P3）

### 形（決めたこと）

- **VkDevice は共有しない**。libmedia は今のまま自分の VkInstance・VkDevice で decode し、decode の image（slot）の memory を `VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT` で export する。videoplayer は自分の device に fd（dup）を import し、同じ大きさの NV12 の image（`MUTABLE_FORMAT`・`EXTENDED_USAGE`、SAMPLED）に bind して plane の view（R8・RG8）を作る。kernel の中では同じ物理の run なので、decode の `bcm2711_v3d_memory_written` が videoplayer 側の texture の snapshot を作り直させる。理由: libmedia の device は stream ごとに作り直され、app の swapchain は file を跨いで続くので、device の共有は持ち主が合わない。
- **同期**: libmedia は picture を渡す前に decode の fence を待つ（p010: decode を含む submission の fence は自分の frame が image に入ってから signal）。videoplayer は frame の fence を待ってから前の picture を解放する（1 frame ずつ、Image Viewer と同じ）。
- **slot の持ち**: picture は自分の slot を最後の参照が消えるまで持ち（`media_gpu_slots`、held の bit）、runtime の busy に足すので、その slot に次の decode は入らない。slot の数 = 参照 + reorder + 飛んでいる数 + app が持つ数 + 1（上限 17、超える時は飛んでいる数を 2 まで減らし、それでも超えれば readback に戻る）。
- **arm64 だけ**: videoplayer の presenter と GPU の picture は `__aarch64__`（Pi 4）の build だけ（`VP_GPU_PRESENT`）。amd64（i915・5330）の videoplayer は前のまま（KL_PRESENT_VULKAN、CPU の変換、libmedia は readback）。libbrowser は GPU の picture を頼まないので前のまま。
- **overlay**: bar・notice・CPU で縮小した picture（libavcodec の予備の道）は CPU の canvas に描き、内容が変わった時だけ（入力・状態、CPU の picture の毎枚、bar の表示中は 250 ms ごと）linear の BGRA の image へ写して premultiplied alpha で重ねる。GPU の picture で bar が隠れている間は overlay を描かない（CPU の画素の仕事 0）。

### 作った物

- libmedia（WS202 の file）:
  - `media-decoder.h`: `struct media_gpu_picture`（identity・slot・fd・memory の大きさと type・image の大きさ・窓・colour・full_range）、`media_decoder_gpu_pictures(hold)`、`media_frame_gpu()`、`MEDIA_COLOUR_*`。`media-private.h`: ops に `picture_gpu`、`media_decoder_gpu_hold()`。`decoder.c`: その実装。`aac.c`: ops の初期化子。
  - `picture.h`・`.c`: `struct media_gpu_slots`（context と picture が共有、held の bit、fd を最後の参照で close）、`media_gpu_slots_create/hold/held/unref`、画素を持たない pool（`media_picture_pool_create_gpu`）、最後の unref で slot を返す、`media_picture_gpu()`、GPU の picture の `media_picture_scale` は ENOTSUP。
  - `vkvideo-runtime.c`・`.h`: `media_vkvideo_runtime_open` に reorder、GPU の判定（`vkvideo_gpu_capabilities`: hold の要求、device の external memory、video の format が SAMPLED と plane の flag を持つこと、slot の数）、instance に `VK_KHR_external_memory_capabilities`、device に external memory の 2 つ、image に flag と `VkExternalMemoryImageCreateInfo`、export の allocation と `vkGetMemoryFdKHR`（`vkvideo_gpu_export`）、decode を自分の fence で submit（`vkvideo_submit_gpu`、readback なし）、busy に held、`media_vkvideo_runtime_gpu()`。`vkvideo.c`: reorder を渡す、GPU の時は画素なしの pool、ops に `media_picture_gpu`。
- media-app（`decoder.c`・`.h`）: `app_frame_gpu()`。
- videoplayer（WS122 の file）:
  - `present.c`（新規、Image Viewer の presenter から）: instance（surface・wayland surface・properties2・external memory capabilities）、device（swapchain、import できれば external memory の 2 つ）、swapchain・pass・targets、picture の pipeline（2 sampler、push 64 byte: 大きさと色の 3 行）と overlay の pipeline、import の表（identity が変わったら全部捨てる）、`present_colour`（BT.601・709・2020、limited・full）。
  - `shaders/picture.vert`・`picture.frag`・`overlay.frag`・`regenerate.py`・`shaders.h`（glslc と spirv-val）。
  - `main.c`: arm64 で KL_PRESENT_NONE と presenter、`media_decoder_gpu_pictures(VP_GPU_HOLD)`（import できる時）、`vp_draw_gpu`・`vp_fit`・`vp_bar_visible`、`PRESENT`・`PRESENTED … gpu=` の行。`media.c`・`videoplayer.h`: ring の上限（presenter の時は 3）、presenter の型。`Makefile`: `present.c`。
- 試験: `plan/ws236/tests/shader-compile-host-test.c`・`run-shader-compile-test.sh`（新規、videoplayer の shader を bcm2711 の compiler で compile、picture.frag が 2 枚の texture を読む）。`plan/ws202/tests/host-vkvideo-runtime.c`（GPU の判定と、fence 付きの decode・slot の持ちの確かめを足した）、`host-native-playback.c`・`host-app-fallback.c`・`plan/tools/media/host-codec.c`（stub と ops の初期化子）。

### 確かめ（host と build、2026-10-16）

| 確かめ | 結果 |
| --- | --- |
| rpi4: `make -j16 BUILD=build/p3/rpi4 ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/p3/rpi4/vmunix build/p3/rpi4/dynamic/libmedia.so build/p3/rpi4/bin/videoplayer build/p3/rpi4/bin/vkvideo-probe` | warning 0、`arm64 vmunix check: PASS` |
| amd64: `make -j16 BUILD=build/p3/amd64 ZEDBSD_CONFIG=config/ci/config-amd64.mk build/p3/amd64/bin/videoplayer build/p3/amd64/dynamic/libmedia.so` | warning 0 |
| `sh plan/ws236/tests/run-shader-compile-test.sh` | PASS（picture.frag 39 命令、texture の uniform 2、binding 0x3、push 64 byte） |
| `sh plan/ws202/tests/run-host-native-playback.sh`（runtime の GPU の確かめを含む、ASan/UBSan） | PASS |
| `run-host-app-fallback.sh`・`run-host-nv12-readback.sh`・`plan/tools/media/run-host-codec.sh` | PASS |
| `plan/tools/style-check.py`（変えた file、HEAD との差） | 新しい指摘 0 |

- 未実施: Pi 実機（下の依頼 3）、5330（i915）の videoplayer の実機（amd64 の道は変えていないので build だけ）、libbrowser の build。

### T2 への依頼 3（Pi 4 実機、QEMU 不要、約 30 分）: GPU の道の再生

1. build（agent/p3-ws236e の段 d・e の commit、または merge 後の main）: `make -j16 BUILD=build/<T>/rpi4 ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/<T>/rpi4/vmunix build/<T>/rpi4/dynamic/libmedia.so build/<T>/rpi4/bin/videoplayer`（warning 0）。
2. Pi: kernel を差し替えて reboot、`/lib/libmedia.so` と videoplayer（今の置き場所）を差し替える（前の物は退避）。
3. desktop の session で `XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 videoplayer --timeout-s=25 /home/kei/ws236/high-1080p30-10s.mp4 2>/tmp/vp-high.log`、同じく `baseline-1080p30-10s.mp4`、`/home/kei/Video.mp4`（あれば、`--timeout-s=15`）。再生の途中（約 3 秒）で `keiland-shot` の PNG を 1 枚ずつ。別の ssh で `top -b -n 3 -d 2`。
4. 報告: 各 log の `PRESENT`・`Vulkan Video:`（特に `pictures the GPU holds, N slots` か `pictures are read back` の理由）・`MEDIA backend=`・`PRESENTED`・`FRAMES`・`DRAW`・`CLOCK`・`DONE`・`FAILED`・`NOTICE` の行、PNG、top、dmesg の `v3d: video:`・`v3d: (EIO|execution failed)`・`rpi4-mmal: frame .* not its target`。
5. 合否: (a) `PRESENT own=1 gpu_pictures=1`、`pictures the GPU holds` の行が出る、(b) 1080p30 の 2 本が最後まで、`FRAMES … late` がほぼ 0（目安 10 以下）・shown 290 以上、(c) `DRAW picture_us` が 1 枚 数 ms（前は約 60000）、(d) PNG の色が正しい（testsrc2 の色の帯・文字が見え、緑や紫に偏らない。前の CPU の道の PNG と見比べ）、(e) `FAILED`・`execution failed` が無く、videoplayer・compositor が落ちない、(f) videoplayer の CPU の使用率が前より下がる（記録）。
6. 止め所: Pi が応答しなくなったら止めて報告。画が出ない・色が違う時は PNG と log を返す（解析は P3）。

### T2 の結果（T1-679、2026-10-16、Pi 4 実機、main 18c4fa5d3）: FAIL → 直した

- `PRESENT own=1 gpu_pictures=1 hold=6` の後、`vkGetPhysicalDeviceVideoFormatPropertiesKHR count failed (-1000023000)` と `no sampled NV12 decode images; pictures are read back`。全て readback の道（high shown 98・late 194、`DRAW picture_us` 約 50 ms/枚）。落ちない、`execution failed` 無し。PNG は Pi の image に keiland-shot が無く未実施。結果 `plan/agents/T2/results-20261016-t1-679.md`。
- 原因: libvulkan の `vkGetPhysicalDeviceVideoFormatPropertiesKHR`（`userland/desktop/libvulkan/video.c`）が usage を DECODE_DST・DPB・TRANSFER_SRC に限り、SAMPLED を含む照会を kernel に送らずに `VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR` を返していた。
- 直し（95c47d971）: libvulkan の mask に SAMPLED を足す（可否は kernel が答える。i915 は今まで通り拒む）。WS083 の host 試験の「手元で拒む usage」の例を COLOR_ATTACHMENT に。rpi4・amd64 の libvulkan warning 0、`plan/ws083/tests/run-host-libvulkan-video.sh`・`plan/ws202/tests/run-host-nv12-readback.sh` PASS。
- 再試験は依頼 3 と同じ手順に `/lib/libvulkan.so` の差し替えを足す。
- uname の版は `/etc/os-release`（userland）から来るので kernel の新旧は分からない（kernel の banner は版だけ）。kernel は `/boot/boot0/vmunix` の cksum で確かめる。
- Pi で PNG を撮るには、keiland-shot を arm64 の dynamic の規則（`platform/common/userland-dynamic.mk`）に足し、`ZEDBSD_TEST_SCREEN_CAPTURE := y` の試験の config で compositor（`bin/wayland`）と keiland-shot を作って差し替える必要がある（共有の build の file なので Q1 の判断）。

### T2 の結果（T1-680、2026-10-16、Pi 4 実機、main 7d03555d8）: FAIL → 直した

- 照会は通り `pictures the GPU holds, 17 slots (references 4, reorder 2, flights 4, held 6)`（baseline 14）。続いて `vkGetMemoryFdKHR failed (-4)` と `a context of pictures the GPU holds failed (1004); pictures are read back`（libmedia が readback の context で作り直した）。全て readback の道（high shown 98・late 194）。落ちない。結果 `plan/agents/T2/results-20261016-t1-680.md`。
- 原因: libvulkan の `vkGetMemoryFdKHR` は fd に device と driver の UUID を刻むため `vulkan_physical_identity`（`GPU_OP_GET_PHYSICAL_DEVICE_PROPERTIES2` に `VkPhysicalDeviceIDProperties` を連ねた照会）を送るが、bcm2711 の kernel はこの opcode を扱わず、`VK_ERROR_DEVICE_LOST`（-4）になっていた（Venus の host だけが答えていた。native の V3D で OPAQUE_FD の allocation の export を使うのは今回が初めて）。
- 直し: `src/drivers/gpu/bcm2711/vulkan-query.c` に `identity_query`（chain が properties2 → ID properties → 終わりの照会だけに答える。device の UUID・driver の UUID は固定の 16 byte、LUID なし、続けて普通の properties）。どの context も同じ値を返すので、libmedia（export）と videoplayer（import）の照合が合う。`plan/ws141/tests/vulkan-device-host-test.c` に libvulkan と同じ要求と読みで 2 回照会して同じ UUID になる確かめを足した（`ws236 p011 native device identity for exported allocations: PASS`、全 32 行 PASS）。rpi4 の vmunix warning 0。
- Pi の PNG: `userland/tests/keiland-shot/Makefile` の platform に rpi4 を足し（arm64 の basic の command の規則で作れる。共有の `userland-dynamic.mk` の変更は不要だった）、`plan/ws236/tests/config-rpi4-shot.mk`（`ZEDBSD_TEST_SCREEN_CAPTURE := y` と keiland-shot）で compositor（`bin/wayland`、`KWL SHOT` を持つ）と `bin/keiland-shot`（aarch64）が build できることを確かめた。

### T2 の結果（T1-681、2026-10-16、Pi 4 実機、main 4cd126b74）: GPU の道は動いた、速さが未達

- `pictures the GPU holds, 17 slots`、`MEDIA backend=vulkan-video`、kernel の `place 0〜13 us`・`readbacks 0`、`DRAW picture_us` 45〜1095 us（前 約 225 ms）。PNG（keiland-shot、1280x720）の色は正しい（カラーバー・灰の四角・市松・時刻、Q1 も目視）。`execution failed` 無し、落ちない。
- `PRESENTED … gpu=0` は最初の frame（picture がまだ無い）の行で、GPU の道でないことを意味しない（今回の直しで `picture=` を足し、DRAW の行に GPU の picture を描いた frame 数 `gpu=` を足した）。
- 速さ: high 1080p30 は shown 210・late 90／10 s（約 21 fps）、baseline shown 201・late 99。`DRAW rest_us`（present と fence の待ち）27〜52 ms が支配的。kernel の video は 2 秒に 52〜73 decode、depth 1.4〜1.7、frame 37〜50 ms。

## 速さの直し（2026-10-16、P3、Q1 の依頼「presenter の 2 frame 重ねと律速の切り分け」）

### 見立て

1 frame の時間 = videoplayer の loop の待ち（再生中は固定 10 ms）＋ `rest`（present の後に fence を待つ 27〜52 ms）。fence の待ちは V3D の executor で compositor と decode と videoplayer の仕事が順に回る時間で、その中に **decode の step が VPU を待つ 2 ms**（flight が満ちた時の `collect` と、settling だけの session の番の待ち）が含まれる。2 秒に 250 step ほどあり、executor の時間の 1〜2 割を VPU 待ちで潰していた見込み。

### 直し

- kernel（`render-worker.c`・`.h`、`vulkan-video.c`）: executor は step の前に「他の session に execution が並んでいるか」（`worker.contended`）を見る。並んでいる時は、settling だけの session の番も decode の step（flight が満ちた時・staging 待ち・copy の前の settle・reset）も VPU を待たずに見るだけにして、次の番にまた見る（`poll_ms`）。並んでいない時は前と同じ 2 ms。試験: `plan/ws236/tests/render-settle-host-test.c` に (e)（他の session の execution が並ぶ間の settle は wait なし、無くなれば wait あり）を足した。
- videoplayer（`present.c`・`main.c`・`videoplayer.h`）: presenter が 2 frame を GPU に持つ（slot ごとの command buffer・fence・acquire の semaphore・picture の quad の頂点）。frame は 2 つ前の frame だけを待ち、overlay を書く時だけ全部を待つ。置き換えた picture はそれを描いた frame が済むまで `retired` に置く（`vp_present_done`）。libmedia に頼む hold を 7 に（ring 3＋reader 1＋表示 1＋GPU 上の frame 2）。再生中の loop の待ちを次の picture の時刻まで（10 ms 固定をやめる、presenter の時だけ）。DRAW の行に `gpu=`・`acquire_us`・`submit_us`・`wait_us`。
- 試験の film: `plan/ws236/tests/make-test-mp4.sh` に 1080p60（High、level 4.2、10 s、600 frame）を足し、`high-1080p60-10s.mp4`（1.2 MB）を作った。

### 確かめ（host と build）

| 確かめ | 結果 |
| --- | --- |
| rpi4 の vmunix・videoplayer、amd64 の videoplayer | warning 0、vmunix check PASS |
| `sh plan/ws236/tests/run-render-settle-test.sh` | 全 PASS（(e) を含む） |
| `plan/ws141/tests/vulkan-device-host-test.sh` | 32 行 PASS |
| style-check（変えた file、HEAD との差） | 新しい指摘 0 |

### T2 への依頼 4（Pi 4 実機、約 30 分）

1. build（この commit か merge 後の main）: `build/<T>/rpi4/vmunix`・`dynamic/libmedia.so`・`bin/videoplayer`（libvulkan は T1-680 のまま）、compositor と keiland-shot は T1-681 の capture 入りの物。
2. Pi: kernel・videoplayer・libmedia を差し替え、boot の行に `v3d.perf=1` を足して reboot（executor の時間の内訳を取る）。`high-1080p60-10s.mp4` を Pi の `/home/kei/ws236/` に置く。
3. 再生（`--timeout-s=15`）: `high-1080p30-10s.mp4`・`baseline-1080p30-10s.mp4`・`high-1080p60-10s.mp4`。各 1 回、再生の 5 秒目ころに keiland-shot。
4. 報告: 各 log の `Vulkan Video:`（slots・flights）・`PRESENTED`・`FRAMES`・`DRAW`（gpu・acquire_us・submit_us・wait_us）・`CLOCK`・`DONE`・`FAILED`、dmesg の `v3d: perf`・`v3d: video:`・`v3d: tfu`・`execution failed`、PNG。
5. 合否: (a) 1080p30 の 2 本が shown 290 以上・late 10 以下、(b) 1080p60 の shown（目標 590 以上、記録）、(c) DRAW の gpu が frames と同じ、(d) 落ちない・`execution failed` 無し、(e) PNG の色。終わったら `v3d.perf=1` を外し、compositor を CI の物に戻す。

### presenter の 2 frame 重ねの検討（2026-10-16、P3、Q1 の依頼）

- 今: 1 frame ずつ（acquire → 記録 → submit → present → fence を待つ）。待つ間に CPU は次の frame を作れないが、GPU の仕事は TFU（Y・UV 約 1.4 ms の見込み）と 1 枚の quad の描画（窓の大きさの fragment、texture 2 枚）だけ。
- 重ねる形（実装するなら）: frame の slot を 2 つ（command buffer・fence・acquire の semaphore）、picture の quad の頂点を slot ごと、前の picture は「最後に描いた frame の通し番号」を持たせて、その frame の fence が済んでから解放（videoplayer に解放待ちの列）。overlay の image は書く前に飛んでいる全 frame を待つ（GPU の picture では overlay の書き換えは bar の表示中の 250 ms ごとだけなので、待ちは稀）。見積もり 1〜2 LW。
- 効く条件: `DRAW rest_us`（present と fence の待ち）が 1 frame の時間（30 fps で 33 ms、60 fps で 16.7 ms）に近い時。V3D の executor は compositor と videoplayer の仕事を順に流すので、compositor の 1 frame（WS141 p013 では 33 ms 級だった）が長いと fence の待ちも長くなる。その時は重ねても compositor の合成の速さが上限（表示は compositor が出す）で、60 fps には compositor 側の短縮が要る。
- 判断: T1-679 の `DRAW picture_us`・`rest_us` と kernel の perf を見てから決める。`rest_us` が 1 frame の半分を超え、かつ compositor が 60 fps で回っている時に実装する。それ以外（compositor が律速）なら、HVS の overlay に NV12 を直に載せる案 2（WS214、compositor の合成を通らない）の方が効く。

### T2 の結果（T1-683、2026-10-16、Pi 4 実機、main a7f6474bf）と律速の切り分け

- 1080p30 high: shown 270・late 30（10 s）、`DRAW` は 1 秒に 25〜29 frame、`acquire_us` 25〜32 ms・`submit_us` 1.6〜3.2 ms・`wait_us` 0.2〜0.9 ms・`picture_us` 2 ms→0。baseline: shown 250・late 50。1080p60（15 s）: shown 279・late 321、`DRAW` 1 秒に 12〜21 frame、`acquire_us` 25〜59 ms、`Vulkan Video:` 2 秒に 74〜83 picture（約 41 fps）・submit 4.4 ms・wait 14.5 ms・copy 4.5 ms、flights 3（17 slots、held 7）。落ちない、`execution failed` 無し、PNG の色は正しい。
- `PRESENTED picture=0 gpu=0` は最初の frame（picture がまだ無い）の行だけで、続く `DRAW` は gpu=frames（18 に 17 などは最初の 1 枚）。1080p60 も GPU の picture の道に入っている（Q1 の (2) は問題なし）。
- **表示の律速（窓）**: frame の時間の大部分が `vkAcquireNextImageKHR`（swapchain の image が compositor から戻るまで）。libvulkan の Wayland の FIFO は前の commit の frame callback を待って次を commit し、image は compositor の release まで使えない。compositor が 1 frame を出すまでの GPU の仕事が律速。
- kernel の `v3d: perf`（1080p30、2 秒）: subs 284、passes 112（videoplayer と compositor が 1 frame に 1 つずつ）、image passes 112、TFU 168（1 frame に 3: Y・UV と compositor が窓の image を写す 1）、busy 72%（1080p60 は 86〜90%）。`v3d: gpu tiles`: draw 17604 tile × 22650 cycle（500 MHz で 45 us/tile）、copy 17780 tile × 2972 cycle。1 frame あたり GPU 約 8M cycle＝**約 16 ms**、TFU 約 3 ms、executor の CPU の準備・解放を足すと executor の時間は 1 frame に約 25 ms。窓の再生では 1 frame に videoplayer の pass と compositor の合成の pass が両方要り、60 fps（16.7 ms）には GPU の仕事を半分以下にしないと届かない。
- **decode の律速（1080p60）**: kernel の `v3d: video` で depth 2.1（flight 3 のうち平均 2 つしか VPU に無い）、frame 49 ms。libmedia は 1 picture に submit 4.4 ms と copy（fence の reset・command buffer の reset 2 回・query の読み）4.5 ms を順にかけ、その間 flight が 1 つ空く。executor が 86% 埋まっているので、1 回 1 回の kernel の要求が V3D の pass の後ろで待つ（2 秒に要求 1740、1 picture に約 20）。

### 直し 5（2026-10-16、P3）

- videoplayer（`main.c`）: `--fullscreen`（起動から全画面）。content type は VIDEO なので、compositor の game mode（ws122-p005b、`KWL SCANOUT direct=1`）に入れば compositor は合成せず、窓の image を HVS がそのまま出す（bcm2711 の `GPU_DISPLAY_PRESENT` の BLOB は allocation を直に出す）。1 frame の GPU の仕事は Y・UV の TFU と出力の大きさの 1 枚の quad だけになり、compositor の pass・窓の image の TFU・copy pass が消える。
- videoplayer（`videoplayer.h`）: 先読みの picture を 3 → 2（hold 7 → 6）。High の stream（参照 4・reorder 2）で libmedia の flight が 3 → 4 になる（17 slots の中）。
- libmedia（`vkvideo-runtime.c`）: GPU の picture の context は readback を記録しないので、picture ごとの transfer の command buffer の reset を省く（kernel の要求が 1 picture に 1 つ減る）。

### 確かめ 5（host と build）

| 確かめ | 結果 |
| --- | --- |
| rpi4: `make -j16 BUILD=build/p3/rpi4 ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/p3/rpi4/dynamic/libmedia.so build/p3/rpi4/bin/videoplayer` | rc 0、warning 0 |
| amd64: `make -j16 BUILD=build/p3/amd64 ZEDBSD_CONFIG=config/ci/config-amd64.mk build/p3/amd64/bin/videoplayer build/p3/amd64/dynamic/libmedia.so` | rc 0、warning 0 |
| `sh plan/ws202/tests/run-host-native-playback.sh`・`run-host-app-fallback.sh` | PASS（plain、ASan/UBSan） |
| style-check（変えた 3 file） | 指摘 0 |

### T2 への依頼 5（Pi 4 実機、約 30 分）

1. build（この commit か merge 後の main）: `build/<T>/rpi4/dynamic/libmedia.so`・`bin/videoplayer`（kernel・libvulkan は T1-683 のまま）。compositor は T1-683 の capture 入りの物（`KWL SCANOUT` の行は compositor の標準出力に出る）。
2. Pi: libmedia・videoplayer を差し替え、boot の行に `v3d.perf=1`。
3. 再生（`--timeout-s=15`）: (i) `high-1080p60-10s.mp4 --fullscreen`、(ii) `high-1080p30-10s.mp4 --fullscreen`、(iii) `high-1080p60-10s.mp4`（窓、比較）。pointer は動かさない。各 1 回、5 秒目ころに keiland-shot（game mode の間は撮ると合成に戻るので、(i) は撮らない回と撮る回の 2 回、撮る回は 8 秒目）。
4. 報告: videoplayer の `Vulkan Video:`（flights・held）・`FULLSCREEN`・`FRAMES`・`DRAW`（acquire_us・submit_us・wait_us）、compositor の `KWL SCANOUT`（direct=1 か、direct=0 の reason）、dmesg の `v3d: perf`・`v3d: gpu tiles`・`v3d: video:`（depth・frame）・`v3d: requests`・`execution failed`、PNG。
5. 合否: (a) (i) で `KWL SCANOUT direct=1`、(b) (i) の shown（film は 10 s・600 frame、目標 580 以上・late 20 以下、記録）、(c) (ii) が shown 290 以上・late 10 以下、(d) `Vulkan Video:` の flights 4・depth と 2 秒の picture 数（目標 120 以上）、(e) 落ちない・`execution failed` 無し、PNG の色。終わったら `v3d.perf=1` を外す。

### ユーザーの判断が要る点（記録、P3 は先へ進む）

- **窓の再生の 60 fps**: 窓では compositor の合成の pass（GPU 約 8 ms/frame の見込み）と窓の image の TFU が毎 frame 要り、videoplayer 側だけでは届かない。案は (A) compositor の合成の短縮（窓の下の層を描かない遮蔽の省略など、WS141 p013・WS211 の範囲）、(B) WS214（窓を HVS の plane に載せる）の p005 で再生中の窓を plane に載せる、(C) 60 fps は全画面（game mode）だけを目標にする。P3 は (C) の道（`--fullscreen`）を先に確かめる。
- **先読みの picture 3 → 2**: 1080p60 の decode の throughput（flight 4）のため。30p の再生で揺れ（late）が増えるなら戻すか、libmedia の slot の上限（`H264_DPB_SLOTS` 17、kernel の `VIDEO_MAX_DPB_SLOTS` 17）を広げる（kernel は slot の数を使っていないので広げられるが、libmedia の DPB の表の大きさが変わる）。

## 残り・危険

- 段 a の YUV の候補は当たらなかった（B-2 に決めた）。
- frame の pitch・chroma の offset が image の配置と違えば direct にならず、bounce からの CPU の写し（補正なし）が残る（log で分かる）。
- 段 c の shader の compiler（kernel）は picture.frag（2 枚の sample と push の行列）を host で compile できた。実機の値（色）は依頼 3 で見る。
- 1 frame ずつ fence を待つ presenter（Image Viewer と同じ）。1080p30 の再生には十分の見込みだが、60 fps の表示で足りなければ 2 frame を重ねる（次の段）。
- slot の数が 17 を超える stream（参照・reorder が多い）は readback の道に戻る（log の `pictures are read back`）。
- i915（5330）の videoplayer は変えていない（arm64 だけ）。i915 で同じ道を使うには i915 の kernel に NV12 の plane の view と export が要る（範囲外）。

- 2026-10-16 ユーザー（T2 の T1-681 の再生の最中、OLED を目視）:「Piは少なくとも30fps、60fpsに近いフレームレートで動画が再生されました」（b4452d35d の kernel、GPU の picture の道の見込み。数は T1-681 の結果で確かめる）。
