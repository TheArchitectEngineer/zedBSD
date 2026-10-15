<!-- awesome-plan project=zedbsd record=ws236-p011 -->

# ws236-p011: 案 1: decoder の出力を GPU の texture に、表示を GPU で

Parent: [WS236](../ws.md)
Status: in-progress（2026-10-16、P3 新世代。直し 7 は T1-687 PASS（全画面で VPU が止まらない、原因は出力の閉じ開き）。60 fps: A1（全画面の窓の copy）を merge 済み、T2 の T1-688（依頼 8）待ち。A2 は調べて、T1-688 の数の後に決める（v3d perf に worker の実の占有を足した）。V1 は不採用）
Phase disposition: normal
実行者: P3（worktree `/home/awe/zedBSD-worktrees/p3`、branch `agent/p3-ws236e` → 2026-10-16 新世代 `agent/p3-ws236f`）
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

### T2 の結果（T1-685、2026-10-16、Pi 4 実機、main f0576b990）: FAIL

- `--fullscreen`（1080p60・1080p30）: `PLAYBACK failed error=5`（shown 9〜14）。dmesg: 入力 4 つが flags `0x406`（FRAME_START・FRAME_END・**TRANSMISSION_FAILED**）で返り、`v3d: video: picture 26/27 skipped: no frame from the decoder (42)`、続いて `PORT_ACTION not answered`・`decoder 0 destroyed broken`。reboot の後の 1 回目（r-a）も同じ止まり方（入力 4 つが返らず `PORT_ACTION not answered`）。以後 reboot まで `ril.video_decode not created (42)`・`no firmware decoder for a 1920x1088 session`（libmedia は CPU の decode に戻り再生は続く）。
- compositor: `KWL SCANOUT direct=0 reason=size` → `KWL OUTPUT closed` → `reason=backend errno=21` → `KWL OUTPUT open`（`KWL MODE window switch_ms=59〜64`）→ `reason=refused`。
- 窓の 1080p60（reboot 後）は落ちない: shown 304・late 296、decode 2 秒に 89〜95、`v3d: perf` busy 86〜91%。結果 `plan/agents/T2/results-20261016-t1-685.md`。

## 直し 6（2026-10-16、P3 新世代）: 全画面の切り替えで VPU が止まる、game mode、decode の要求

### 調べ

1. **game mode が入らない理由**: bcm2711 の display の QUERY（`display-device.c` `output_query`）が `GPU_DISPLAY_BLOB` を立てないので、backend の `scanout_display()`（`scanout-zedbsd.c`）が `ENOTSUP`（= `EOPNOTSUPP` 21）を返す。ws223-p001 §0 の 4 と同じ。**2026-10-13 のユーザーの決定（WS223 原文「Video Playerは、Game Modeをやめます。…全画面ではコンポジターを利用しましょう。」）と、直し 5 の「Video Player を game mode に入れる」は衝突する**。Q1 の指示（2026-10-16）で game mode の道は実装しない。60 fps の道（A 合成の短縮／B HVS の plane／WS223 の見直し）はユーザーの判断として Q1 が master の未決に記録した。
2. **VPU が止まる理由（見込み）**: 全画面への切り替えで compositor は game mode を試すために swapchain を閉じる（`scanout_enter` → `kwl_compose_output_close`）。display の lease の release は console を戻し（`bcm2711_display_flip_restore`）、**core clock を console の値へ下げる**（`lower_clock`、720p で 67 MHz 級の要求）。backend が断ると compositor は直ちに swapchain を開き直し、次の flip が **core clock を 500 MHz へ上げ直す**（`raise_clock`）。core clock は VPU（firmware の MMAL）の clock で、その 2 回の変更が decode の最中に起きた直後に入力の bulk が TRANSMISSION_FAILED で返り、firmware の decoder が答えなくなった（T1-685 の 3 回とも全画面の切り替えの時、窓の再生では起きない）。出力の閉じ開きで他に VPU に触る物（HVS の list、console の再描画、V3D の allocation）は無い。
3. **reboot なしの回復**: firmware の MMAL の worker が答えなくなると、VCHIQ の service を閉じて開き直しても component の作成が答えない（a2・b・c）。firmware を作り直す口は無く、reboot なしの回復はできない。今の道（session の作成が失敗 → libmedia が CPU の decode に戻る）で再生は続く（T1-685 の b は shown 151）。よって**止まらないようにする**のが直し。
4. **decode の要求**: GPU の picture の道で libmedia が 1 picture にかける kernel の要求は Begin・End（記録）・QueueSubmit・WaitForFences・ResetFences・ResetCommandBuffer・GetQueryPoolResults の 7。`vkBeginCommandBuffer` は pool の `RESET_COMMAND_BUFFER_BIT` で暗に reset する（kernel の `begin_buffer` が `bcm2711_vulkan_command_clear` する）ので、明示の reset は要らない。`v3d: decode requests` は全 client（compositor・videoplayer・libmedia）の wire の要求の数で、1 picture あたり 20 ではない（前の見立ての訂正）。

### 直し

- `src/drivers/platform/rpi4/rpi4-firmware.c`・`.h`: **core clock の floor の集約** `drv_rpi4_firmware_core_clock(owner, hz)`（owner は DISPLAY・VIDEO）。firmware に頼む rate は owner の floor の最大（firmware の最高の rate まで）、誰も頼まない時は最初の floor の前に firmware が回していた rate（`GET_CLOCK_RATE`・`GET_MAX_CLOCK_RATE` で 1 度だけ読む）。rate が変わる時だけ `SET_CLOCK_RATE` を送り、`rpi4-firmware: core clock N Hz (display D, video V)` を log する（変化は稀）。失敗した時は owner の floor を元に戻す。直列化は既存の request buffer と同じ atomic の turn（mutex を使わないので ws048・ws206 の host 試験はそのまま通る）。
- `src/drivers/gpu/bcm2711/display-flip.c`: `request_clock` は display の floor として集約に頼む（display の上げ下げの規則は変えない）。
- `src/drivers/platform/rpi4/rpi4-mmal.c`: decoder の entry が 1 つでも使われている間は VIDEO の floor を firmware の最高の rate（Pi 4 は 500 MHz）に保つ（`core_floor()`、最初の decoder の component を作る前に取り、最後の decoder が消えたら返す）。これで decode の最中は display の lease の release・claim で core clock が変わらない。入力の buffer が TRANSMISSION_FAILED で返った時に log（`rpi4-mmal: input N came back unread`）。`rpi4-mmal-wire.h`: `DRV_RPI4_MMAL_FLAG_TRANSMISSION_FAILED`。
- `userland/desktop/libmedia/vkvideo-runtime.c`: GPU の picture の道で decode の command buffer の明示の reset を省く（次の Begin が暗に reset）。1 picture の要求 7 → 6。readback の道（amd64 の i915 を含む）は前のまま。
- 試験: `plan/ws236/tests/core-clock-host-test.c`・`run-core-clock-test.sh`（新規、rpi4-firmware.c を mailbox と clock の tag の模型で: 最大の floor、decode 中の display の上げ下げで変化なし、decoder の後に display に従う、誰も頼まない時に元の rate、拒まれた変化で floor が残る、最高の rate で頭打ち、範囲外の owner）。`plan/ws141/tests/display-{flip,device,execute}-host-test.c` に `drv_rpi4_firmware_core_clock` の stub（同じ wire の要求を既存の property の stub へ）。

### 確かめ 6（host と build）

| 確かめ | 結果 |
| --- | --- |
| `make -j16 BUILD=build/p3/rpi4 ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/p3/rpi4/vmunix build/p3/rpi4/dynamic/libmedia.so` | rc 0、warning 0、`arm64 vmunix check: PASS` |
| amd64 `build/p3/amd64/dynamic/libmedia.so` | rc 0、warning 0 |
| `sh plan/ws236/tests/run-core-clock-test.sh` | PASS（ASan/UBSan） |
| `sh plan/ws141/tests/display-host-test.sh`・`sh plan/ws214/tests/display-p003a-host.sh` | PASS |
| `make -f plan/ws048/tests/host-test.mk run DTB=…`・`plan/ws206/tests/firmware-smp-host-test.sh`・`gpio-host-test.sh` | PASS（rpi4-firmware.c の回帰） |
| `sh plan/ws236/tests/run-mmal-wire-test.sh` | all PASS |
| `sh plan/ws202/tests/run-host-native-playback.sh`・`run-host-app-fallback.sh`・`run-host-nv12-readback.sh` | PASS |
| style-check（変えた C の file、HEAD との差） | 新しい指摘 0 |

- 未実施: Pi 実機（下の依頼 6）。見込みが外れていれば（core clock の変化の log が無いのに止まる）、次の見込みは出力の閉じ開きの別の要素（console の復帰・swapchain の作り直しの間の executor の止まり）で、依頼 6 の log で切り分ける。

### T2 への依頼 6（Pi 4 実機、QEMU 不要、約 30 分）

1. build（agent/p3-ws236f の commit か merge 後の main）: `make -j16 BUILD=build/<T>/rpi4 ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/<T>/rpi4/vmunix build/<T>/rpi4/dynamic/libmedia.so`（warning 0）。videoplayer・libvulkan・compositor は T1-685 のまま（compositor は CI の物でよい）。
2. Pi: kernel と `/lib/libmedia.so` を差し替え、boot の行に `v3d.perf=1`、reboot。
3. **reboot を挟まずに**順に（各 `--timeout-s=15`、pointer は動かさない）: (i) `high-1080p60-10s.mp4 --fullscreen`、(ii) `high-1080p30-10s.mp4 --fullscreen`、(iii) `high-1080p60-10s.mp4`（窓）、(iv) (i) をもう 1 回、(v) `baseline-1080p30-10s.mp4 --fullscreen`。(ii) の 5 秒目に keiland-shot を 1 枚（capture 入りの compositor の時だけ）。
4. 報告: 各 run の videoplayer の `Vulkan Video:`・`FULLSCREEN`・`FRAMES`・`DRAW`（acquire_us・submit_us・wait_us）・`PLAYBACK`・`DONE`、compositor の session.log の `KWL SCANOUT`・`KWL MODE`・`KWL PERF`（compose の frame_ms）・`KWL SLOW_PASS`、dmesg の `rpi4-firmware: core clock`（全行）・`rpi4-mmal: (request .* not answered|stall|came back unread|core clock floor|not created)`・`v3d: video:`・`v3d: perf`・`bcm2711: (cadence|flip perf)`・`execution failed`。
5. 合否: (a) 5 本とも `PLAYBACK failed` が無く、dmesg に `not answered`・`came back unread`・`no firmware decoder` が無い（reboot なしで続けて再生できる）、(b) `rpi4-firmware: core clock` の行が video の再生中（全画面の切り替えの時を含む）に出ない（出るのは compositor の起動と、再生の始まり・終わりの境だけ）、(c) 全画面の数の記録（合否ではない）: (ii)・(v) の shown・late（目標 290 以上・10 以下）、(i)・(iv) の shown、`KWL PERF compose frame_ms`、`DRAW acquire_us`。game mode は WS223 のユーザーの決定で入らない（`reason=backend errno=21` は期待どおり）。(d) `execution failed` 無し、PNG の色。終わったら `v3d.perf=1` を外す。
6. 止め所: Pi が応答しなくなったら止めて報告（Q1 が SwitchBot で入れ直す）。

### T2 の結果（T1-686、2026-10-16、Pi 4 実機、main 514a5775c）: FAIL

- 全画面の 1080p60・30 で `PLAYBACK failed error=5`（shown 13〜14 か 0）。reboot の直後の 1 本目（a・r-a）は**同じ pts 26〜29 の入力 4 つ**が TRANSMISSION_FAILED（flags 0x406）で返り、transmit queued 30 completed 26、以後 `not answered (42)`・`ril.video_decode not created`。2 回とも同じ所で壊れる（決定的）。結果 `plan/agents/T2/results-20261016-t1-686.md`。
- **r-a の decode の間に `rpi4-firmware: core clock` の行は 1 つも無い**（VIDEO の floor が 500 MHz を保ち、display の上げ下げは集約の中で吸収された）。それでも壊れたので、直し 6 の見立て（core clock の変化）は外れ。再生中の 67→500 MHz の対（r-d・r-e）は decoder が壊れて消えた後（floor が無い）で、結果であって原因ではない。
- 壊れる時に起きていること: compositor の `KWL CURSOR_PLANE closed`・`KWL OUTPUT closed`（game mode を試すために swapchain を閉じる → display の lease の release、console の list の復帰と text の再描画）→ backend が ENOTSUP → 出力を開き直す（lease の claim、swapchain 3 枚・layer・cursor の image の作り直し）。同じ時に videoplayer は 960x600 → 1280x720 に swapchain を作り直す。窓の再生（どちらも無い）では壊れない。

## 直し 7（2026-10-16、P3 新世代）: 出力を閉じない、lease の log

### 調べ（見込み）

- 原因の候補は (1) display の lease の release・claim（console の list の復帰、text の再描画、出力の image の解放と確保）と (2) videoplayer の swapchain の作り直し。kernel の log に lease の行が無いので、今の log では順序が分からない。firmware の MMAL の入力の bulk（ARM → VPU）が VPU の側で失敗する仕組みは公開の資料からは決められなかった。
- (1) の引き金は compositor が**出来ない game mode を試すために出力を閉じること**。Pi 4（bcm2711 は `GPU_DISPLAY_BLOB` を立てない）・Linux・FreeBSD（backend が ENOTSUP）では、全画面の動画の窓ごとに出力を閉じて開き直していた（WS223 review の M2 と同じ）。WS223 のユーザーの決定（Video Player は game mode を使わない）にも合う。

### 直し

- `libkeiland-backend/keiland-backend-display.h`: `kl_backend_scanout_possible(backend, width, height)`（claim をせずに scanout が開けるかを答える。0・ENOTSUP・照会の失敗）。zedBSD（`scanout-zedbsd.c`）は自分の GPU の fd で `scanout_display`（QUERY の BLOB と MODE の VALIDATE）だけを行って fd を閉じる。drm（Linux・FreeBSD、`drm/display-drm.c`）は ENOTSUP。
- compositor（`wayland/scanout.c` の `scanout_enter`）: 出力を閉じる前に聞き、出来なければ窓を refused にして出力を閉じずに合成を続ける（log `KWL SCANOUT direct=0 reason=backend errno=N … output=kept`）。i915（BLOB あり）は前と同じ道（聞いて出来るので閉じて claim）。
- kernel（`bcm2711/display-device.c`）: `bcm2711: display lease N claimed` と `… released restore_ms=R text_ms=T` を 1 行ずつ（lease の変化は稀）。もし (2) が原因なら、依頼 7 で lease の行なしに入力が失敗し、その時は videoplayer の作り直しの何が VPU を止めるかを次に追う。

### 確かめ 7（host と build）

| 確かめ | 結果 |
| --- | --- |
| `make -j16 BUILD=build/p3/rpi4 ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/p3/rpi4/vmunix build/p3/rpi4/bin/wayland` | rc 0、warning 0、`arm64 vmunix check: PASS` |
| amd64 `build/p3/amd64/bin/wayland` | rc 0、warning 0 |
| host の cc で `drm/display-drm.c` の `-fsyntax-only -Wall -Wextra` | 指摘 0 |
| `plan/tools/gpu-boundary/v1-check.sh` | PASS |
| `plan/tools/keiland-os-boundary/check.sh` | FAIL は libbrowser の `/dev/urandom` の A1 の 2 行だけ（変更の前と同じ、この直しと無関係） |
| `sh plan/ws141/tests/display-host-test.sh`・`sh plan/ws214/tests/display-p003a-host.sh` | PASS |
| style-check（変えた 4 つの C、HEAD との差） | 新しい指摘 0 |

### T2 への依頼 7（Pi 4 実機、QEMU 不要、約 30 分）

1. build（この commit か merge 後の main）: `make -j16 BUILD=build/<T>/rpi4 ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/<T>/rpi4/vmunix build/<T>/rpi4/bin/wayland`（warning 0）。libmedia・videoplayer・libvulkan は T1-686 のまま。
2. Pi: `/boot/boot0/vmunix` と compositor（`bin/wayland`、今の置き場所）を差し替え（前の物は退避）、boot の行に `v3d.perf=1`、reboot。
3. **reboot を挟まずに**順に（各 `--timeout-s=15`、pointer は動かさない）: (i) `high-1080p60-10s.mp4 --fullscreen`、(ii) `high-1080p30-10s.mp4 --fullscreen`、(iii) `high-1080p60-10s.mp4`（窓）、(iv) (i) をもう 1 回、(v) `baseline-1080p30-10s.mp4 --fullscreen`。
4. 報告: 各 run の videoplayer の `Vulkan Video:`・`FRAMES`・`DRAW`・`PLAYBACK`・`DONE`、compositor の `KWL SCANOUT`・`KWL OUTPUT`・`KWL MODE`・`KWL PERF`、dmesg の `bcm2711: display lease`（全行）・`rpi4-firmware: core clock`・`rpi4-mmal: (request .* not answered|stall|came back unread|not created)`・`v3d: video:`・`v3d: perf`・`execution failed`。
5. 合否: (a) 全画面の切り替えで `KWL SCANOUT direct=0 reason=backend errno=21 … output=kept` が出て、`KWL OUTPUT closed` が出ない、(b) 5 本とも `PLAYBACK failed` が無く、dmesg に `came back unread`・`not answered`・`no firmware decoder` が無い、(c) 記録: (ii)・(v) の shown・late、`bcm2711: display lease` の行が再生の間に出ないこと。終わったら `v3d.perf=1` を外し、compositor を CI の物に戻す（kernel は残してよい）。
6. FAIL の時の材料: (b) が FAIL で lease の行が無ければ原因は videoplayer の作り直し側（P3 が次に追う）。lease の行があれば、その行と `came back unread` の順を報告。止め所: Pi が応答しなくなったら止めて報告（Q1 が SwitchBot で入れ直す）。

### T2 の結果（T1-687、2026-10-16、Pi 4 実機、main 37baf57ce）: 直し 7 PASS

- 全画面 4 本で `KWL SCANOUT direct=0 reason=backend errno=21 … output=kept`、`KWL OUTPUT closed` 0。5 本とも `PLAYBACK failed`・`came back unread`・`not answered`・`no firmware decoder`・`execution failed` 無し、`bcm2711: display lease` の行 0、再生中の core clock の行 0。**原因は出力の閉じ開き（display の lease の release・claim）で、直し 7 で VPU は止まらなくなった**（lease の release・claim のどの要素が firmware の decoder を止めるかは未解明、Bug にするかは Q1 の判断）。結果 `plan/agents/T2/results-20261016-t1-687.md`。
- 速さ（A1 の前）: 60p 全画面 shown 306・late 294（約 20 fps）、compose frame_ms 23〜24、`DRAW acquire_us` 19〜21 ms。30p 全画面 263/37、frame_ms 18.8。60p 窓 304/296、frame_ms 28。`v3d: perf` busy 91〜93%。

### 60 fps の続き（V1・A1、2026-10-16 P3 新世代、T1-686 で中断）

- **V1（TMU が raster を読む）は不採用**: V3D 4.2 の TMU は 2D の raster の texture を sample できない（Mesa v3dv `v3dv_formats.c`「Raster format is only supported for 1D textures」、gallium v3d `v3dx_state.c`「V3D still doesn't support sampling from raster textures」、MIT）。1D・texel buffer として読み、shader で 2D の address と bilinear を組むと 1 画素に 8 回の fetch で TFU（Y・UV 約 1.4 ms）より遥かに遅い。
- **A1（設計、下の「A1 の実装」で実装した）**: kernel は whole-image の `vkCmdCopyImage`（同じ format・大きさの raster 同士）を 0 draw の TLB の copy の pass で行う（`vulkan-native-image.c` `copy_whole`、T1-686 の perf で copy 2700 cycle/tile ≒ 240 tile で約 1.3 ms、draw は 7800〜8900 cycle/tile ≒ 約 4 ms＋窓の TFU 1.6 ms）。compositor の `compose_record` で、出力と同じ大きさ・同じ format の不透明な全画面の窓だけが映る frame（`scanout.c` の overlay の判定、glass の `fullscreen_whole`、cursor が frame に描かれない、turn・head なし）を、pass の代わりに UNDEFINED→TRANSFER_DST の barrier・copy・PRESENT_SRC の barrier で出す。i915・Venus を変えないため Broadcom（vendor 0x14e4）だけ。copy の結果は plain の不透明の quad と byte 単位で同じ、glass の whole の描き方とは alpha だけが違う（HVS の primary は固定の alpha、keiland-shot は RGB なので見えない）。videoplayer の swapchain は RGBA（wayland の WSI の最初の format）、compositor の出力は format=37（R8G8B8A8）で一致の見込み。

### A1 の実装（2026-10-16、P3 新世代）: 全画面の窓だけが映る frame は copy で出す

- 振る舞い: Pi 4（Broadcom の GPU）で、出力と同じ大きさ・format の不透明な全画面の窓だけが映る frame は、合成の pass（窓の image の TFU と出力の大きさの quad）をやめ、窓の image を swapchain の image へ `vkCmdCopyImage` で写す（kernel は 0 draw の TLB の copy の pass、`copy_whole`）。合成で出すことは変わらない（game mode・直の scanout ではない、WS223 の決定の中）。何かが重なる（bar・menu・popup・sub-surface・IME の候補・edge の gesture・animation・lock・cursor を frame に描く・drag）、大きさ・format が違う、透明、turn・head がある時は前と同じに描く。i915・Venus・Linux・FreeBSD は変わらない（policy が off）。
- `compose.c`: `compose_copy_source`（判定と理由）、`compose_copy_record`（UNDEFINED→TRANSFER_DST の barrier、copy、PRESENT_SRC への barrier）、`compose_copy_log`（`KWL COPY window=0|1 reason=… surface=N frame=F` を変化の時だけ）、`compose_record` の分岐（scene は無効に、hover・shot・host image は前と同じ）、`compose_limits` の policy（vendor 0x14e4、backend の import が copy の元になれる、RGBA・BGRA の linear が TRANSFER_SRC）。`compose.h`: `KWL_COPY_VENDOR`、`kwl_import.format`、`copy_policy`・`copy_target`（swapchain が TRANSFER_DST を持つ）・`copy_shown`・`copy_reason`。`import.c`: import の format を持つ。
- `shell.c`: `kwl_glass_whole_window`（glass の `fullscreen_whole` の窓で body が出力そのもの）。`scanout.c`: `kwl_scanout_covered`（game mode の overlay の判定を公開）。`kwl.h`: 2 つの宣言。
- backend: `keiland-backend-gpu.h` の `kl_backend_gpu_device.copy_source`（import を TRANSFER_SRC でも作る要求）と `kl_backend_gpu_copy_source()`（その要求に従う backend か: zedBSD 1、dma-buf 0）。`gpu-buffer-zedbsd.c` は要求があれば usage に TRANSFER_SRC を足す。
- 見込み: 1 frame の compositor の GPU は TFU 1 job（約 1.6 ms）と 240 tile の draw（約 4 ms）→ copy の 240 tile（約 1.3 ms）。videoplayer の pass（Y・UV の TFU と出力の大きさの quad）は残る。

### 確かめ A1（host と build）

| 確かめ | 結果 |
| --- | --- |
| rpi4・amd64 の `bin/wayland` | rc 0、warning 0 |
| host の cc で `dmabuf/gpu-dmabuf.c`・`drm/display-drm.c` の syntax check | 指摘 0 |
| `sh plan/ws213/tests/list-replay-host-test.sh`（draw list の replay、copy_image を含む） | PASS |
| style-check（変えた file、HEAD との差） | 新しい指摘 0 |

- 未実施: Pi 実機（依頼 8）。5330・Venus は policy が off なので build だけ（`KWL COPY` の行が出ないこと）。

### T2 への依頼 8（Pi 4 実機、QEMU 不要、約 30 分、依頼 7 の後）

1. build（A1 の commit か merge 後の main）: `make -j16 BUILD=build/<T>/rpi4 ZEDBSD_CONFIG=plan/ws236/tests/config-rpi4-shot.mk build/<T>/rpi4/bin/wayland build/<T>/rpi4/bin/keiland-shot`（capture 入りの compositor、warning 0）。kernel・libmedia・videoplayer・libvulkan は依頼 7 のまま。
2. Pi: compositor を差し替え（前の物は退避）、boot の行に `v3d.perf=1`、reboot。
3. 再生（各 `--timeout-s=15`、pointer は動かさない）: (i) `high-1080p60-10s.mp4 --fullscreen`、(ii) `high-1080p30-10s.mp4 --fullscreen`、(iii) `high-1080p60-10s.mp4`（窓、対照）。(ii) の 5 秒目に keiland-shot を 1 枚（撮影の frame は copy でも合成の image）。
4. 報告: compositor の `KWL COPY`（全行）・`KWL PERF`（compose の frames・frame_ms）・`KWL SCANOUT`、videoplayer の `FRAMES`・`DRAW`（acquire_us）・`PLAYBACK`、dmesg の `v3d: perf`（subs・passes・tfu）・`v3d: gpu tiles`・`v3d: tfu`・`execution failed`、PNG。
5. 合否: (a) (i)(ii) で `KWL COPY window=1` が出る（bar が消えた後。出なければ reason を報告）、(b) 落ちない・`execution failed` 無し・`PLAYBACK failed` 無し、(c) PNG の色と画（前の PNG と同じ見え方、ずれ・色の入れ替わりが無い）、(d) 記録: (i) の shown・late と `KWL PERF compose frame_ms`、`DRAW acquire_us`、`v3d: tfu jobs` の 2 秒の数（前の T1-685 の窓 1080p60 は compose 23.6 fps・frame_ms 25・TFU 1 frame に 3）。(iii) は `KWL COPY window=0 reason=not-fullscreen`。終わったら `v3d.perf=1` を外し、compositor を CI の物に戻す。

### T2 の結果（T1-689、2026-10-16、診断）の読み

- 全画面 60p: shown 303・late 297、`worker=` 61〜64%（executor が実際に持たれた割合、busy 84% より小さい）、compose frame_ms 25.3〜25.6、rec の begin 1.4〜1.9・end 1.4・submit 2.2・present 3.8〜4.2 ms、kernel の要求の wait 84〜86 us・run 154〜200 us。kernel の処理は 1 要求 0.25 ms 程度で、1 往復 1.4〜4 ms の残りは kernel の外。
- `kern.perf=1` の報告は出ていた（行は `sched:` ではなく `kern: perf` で始まる。不具合ではない）。desktop の静止時は 2 秒に switches 約 9 千・woken 約 5 千、**再生中は switches 18〜20 万・woken 12〜13 万・preempted 約 4 万**（20 倍）、preempted の最長の待ちは 4〜11 ms（videoplayer の thread が 476 個の switch の後ろで 11 ms 待った例）。何かが毎秒数万回起きて寝る（短い timeout の poll・spin の yield など）か、CPU の取り合いで往復が遅れている見込み。video の step は 2 秒に約 440 で、decode の poll の空回りではない。誰かは依頼 10 の `ps` の CPU 時間で見る。

### A2 の調べ（2026-10-16、P3 新世代、T1-687 の log から）

- **`v3d: perf` の busy は executor の占有を多く見せる**: `total` は job の最初の step から最後の step までで、step に分かれる job（decode を含む libmedia の command buffer、`EINPROGRESS` で番を譲る）は間に他の session の番を含む。T1-687 の 60p 全画面の 2 秒: subs 283・total 平均 6.5 ms（= busy 92%）に対し、exec（GPU の実行と待ち）3.1 ms・prep 0.67・xfer（video の event の CPU）0.83・rel 0.17・clean 0.19 で、和は約 5.0 ms、step に分かれた job の待ちが約 1.2 ms/sub。GPU の実行は 2 秒の約 44%、CPU を含めた実の占有は約 67% の見込み。**直し**: kernel の perf に job の step の自分の時間の和を足し、行の末尾に `worker=N%`（executor が実際に持たれていた割合）を出す（`vulkan-queue.c` が step を測り `bcm2711_vulkan_native_job_count_step`、`v3d-hardware.h` の `step_ticks`）。
- 1 表示 frame（2 秒に 47）あたりの executor の仕事（A1 の前）: videoplayer の TFU 2 job（Y・UV、平均 1.7〜1.8 ms/job、VPU と HVS と帯域を分ける）と 240 tile の draw（約 8000 cycle/tile ≒ 3.9 ms）、compositor の窓の TFU（約 1.7 ms）と 240 tile の draw（約 3.9 ms）と **scene の capture の copy（image → buffer、240 tile 約 1.9 ms、cursor plane が cursor を出していない時に毎 frame）**、decode の event（1 表示 frame に約 2 decode、約 7 step × 0.65 ms）。A1 は compositor の TFU・draw・capture を copy 1 回（約 1.3〜1.9 ms）に置き換えるので、1 表示 frame で約 5〜6 ms 減る見込み。
- **compositor の frame（frame_ms 24 ms）は CPU 側の Vulkan の呼びの待ちが半分以上**: `KWL PERF compose … acquire 4.3〜5.2 ms`、`rec begin 1.35・end 1.2〜1.4・submit 1.9〜2.2・present 3.5〜3.6 ms`（呼び 1 つが kernel の要求、合計約 12.7 ms）、残りが GPU の完了待ち。videoplayer の `acquire_us` 19〜21 ms は compositor が前の image を返すまでの待ち（Wayland の FIFO）で、compositor の frame の長さがそのまま出る。
- A2（compositor の frame を 2 つ重ねる、`in_flight` 2）の範囲: compose の held の buffer・frame callback・scene の valid・layer の generation・shot が 1 frame を前提にしている（`compose.c` の `kwl_compose_complete`・`compose_completed`、`objects.c`・`shm.c`・`layer-cache.c`・`scanout.c` が `in_flight` を 0/1 で見る）。重ねると CPU 側の呼びの待ち（約 12.7 ms）の間に前の frame の GPU が進むので frame の間隔は縮むが、変更は compositor の frame の持ち主の全体に及ぶ（見積もり 4〜8 LW、i915・Venus にも効くので回帰の範囲が広い）。
- `frame_wait_ms`（frame の後に窓の commit を最大 frame の半分待つ）は、frame callback を受けた窓がまだ commit していない（`awaiting`）時だけ効き、他に変化が無ければ frame は描かれないので、動画の全画面の律速ではない（変えない）。
- 決め: A2 は T1-688（A1）の `KWL PERF compose`・`worker=`・shown の数を見てから。A1 の後も compositor の frame の大部分が CPU 側の呼びの待ちなら、(a) A2 の前に呼びの待ちの中身（begin・end・present が kernel で何を待つか）を調べる方が小さく効く見込み、(b) executor が実際に埋まっている（`worker` が 85% 以上）なら重ねても伸びず、videoplayer の pass（3.9 ms）か TFU（Y・UV 約 3.5 ms）を削る道（または WS214 の HVS の plane、ユーザーの判断）になる。

### 確かめ（perf の worker）

| 確かめ | 結果 |
| --- | --- |
| `make -j16 BUILD=build/p3/rpi4 ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/p3/rpi4/vmunix` | rc 0、warning 0、`arm64 vmunix check: PASS` |
| `sh plan/ws141/tests/vulkan-device-host-test.sh` | 32 行 PASS（前と同じ） |
| style-check（`vulkan-native-job.c`・`vulkan-queue.c`、HEAD との差） | 新しい指摘 0 |

- T2 への依頼: 単独では出さない。次に Pi の kernel を差し替える依頼（A2 か A1 の続き）に含める（`v3d: perf` の行の末尾に `worker=N%` が出ること）。

### T2 の結果（T1-688、2026-10-16、Pi 4 実機、A1 7000f9bfd）: PASS、速さは変わらず

- 全画面で `KWL COPY window=1 reason=none`、窓は `window=0 reason=not-fullscreen`。落ちない、PNG の色・画は正しい（`build/review/t1-688-pi.png`、撮影の frame は合成）。60p 全画面 305/295・compose frame_ms 23.5〜24.6・videoplayer の acquire 約 20 ms、30p 全画面 243/57、60p 窓 305/295・frame_ms 27.6〜30。draw の tile は 1 表示 frame に 240（videoplayer だけ）に減ったが、frame の速さは同じ。
- 読み: `passes=47/0/140`（2 秒）＝ 1 表示 frame に image の pass が 3 つ: A1 の copy と、**libvulkan の present の copy が 2 つ**（videoplayer の Wayland の swapchain、compositor の display の swapchain。下の A3）。compositor の frame 24 ms のうち CPU 側の呼びが約 12 ms（acquire 4・begin 1.3・end 1.1・submit 1.9・present 3.1〜3.8、present は copy の command の submit）、残りは GPU の完了待ち。律速は GPU の量ではなく、present ごとの copy の submit と executor の順番待ちを含む往復の連鎖。

## A3 の設計（2026-10-16、P3 新世代、Q1 の委任「製品の挙動を変えない描画の最適化」）: Pi 4 では swapchain の image を共有の image そのものにして present の copy を無くす

### 今の形と問題

- libvulkan の WSI（`userland/desktop/libvulkan/wsi-swapchain.c`）は、app が描く swapchain の image（`group->images[i]`、OPTIMAL）と、compositor・display に渡す共有の image（`presentation[i].shared_image`、LINEAR、export した allocation を `platform->import_image` で native に登録）を分ける。`vkQueuePresentKHR` ごとに `present_record` が copy の command（`present_copy_shared`: barrier、`vkCmdCopyImage`、外の queue family への release の barrier）を記録し（同じ image の 2 回目からは記録を使い回す）、`present_submit` が app の待ちの semaphore とともに submit し、present の worker が fence の後に native の present（Wayland の commit、display の `GPU_DISPLAY_PRESENT_SYNC`）をする。
- bcm2711 の kernel では OPTIMAL と LINEAR の image は同じ raster の backing（`vulkan-resource.c`「Optimal images use the same native raster backing」）で、format の linear と optimal の feature も同じ（`vulkan-query.c`）。copy は同じ形の byte を写すだけの上乗せで、1 present に 240 tile の copy の pass（約 1.3〜2 ms）と、その pass が executor で他の session の後ろに並ぶ待ちが足される。i915 は OPTIMAL が tiled なので copy が要る。

### 約束（振る舞い）

- **direct の chain**: 次を全て満たす swapchain は、app の image を共有の image そのものにする（`vkGetSwapchainImagesKHR` が返すのは共有の image、LINEAR）。present は copy を記録・submit せず、app の待ちの semaphore だけの command buffer 無しの submit（fence を得るため）をして、前と同じ native の present に進む。
  1. GPU が direct を選ぶ device: `VkPhysicalDeviceProperties.vendorID` が Broadcom（0x14e4、Pi 4 の V3D。i915 は Intel、Venus は host の GPU の vendor なので変わらない）。正しさは下の 3 で保ち、vendor は速さの方針（他の GPU では LINEAR に描くと遅い）。
  2. `vkCreateSwapchainKHR` で 1 つだけ作る chain（`vkCreateSharedSwapchainsKHR` の group を共有する chain は今のまま）、cursor・overlay の plane の surface でない、`platform->import_image` がある（今の GPU の共有の道）、paired の Windows host の文脈（`copy_display`）でない。
  3. app の usage（`imageUsage`）の全てを format の `linearTilingFeatures` が支える（COLOR_ATTACHMENT・SAMPLED・TRANSFER_SRC/DST・STORAGE を feature で確かめる。支えない usage があれば今の copy の道）。
  - 上のどれかを満たさない chain、direct の作成が失敗した chain（native の image の作成・import の失敗）は今の道（copy）。
- **acquire**: 今と同じ。image i は app の状態が AVAILABLE で、かつ `platform->image_available(presentation[i].native_image)`（compositor・display がその共有の image を手放した）時だけ返る。今も copy の写し先 i が空いていないと acquire しないので、app が描く先が共有の image になっても、compositor が読む間に app が書くことは無い（同じ条件）。
- **layout**: app の image は app の barrier（PRESENT_SRC）のまま present される。direct では WSI は layout の barrier も外の queue family の release もしない（kernel は image の resource ごとに layout を持ち、compositor・display は同じ allocation を自分の image として import して自分の layout で扱う。bcm2711 は queue family の ownership を使わない）。再 acquire の後の app の barrier（oldLayout UNDEFINED か PRESENT_SRC）は今と同じに通る（今も copy の後に app の image は PRESENT_SRC に戻される）。
- **書いたことの印**: app の render pass・copy の出力は kernel の buffer の generation を進める（今の共有の image への copy と同じ）ので、compositor の TFU の snapshot の作り直し（`bcm2711_v3d_memory_written`）と A1 の copy の範囲の絞り（generation）は今と同じに働く。
- **display の矩形**（`VkDisplayPresentInfoKHR`、今の利用は compositor の cursor plane だけで、cursor は direct にしない）: direct の chain への present で src と dst が image の全体と同じなら無視、それ以外は `VK_ERROR_INITIALIZATION_FAILED`（写し先が無いので合成できない。他の不正な矩形と同じ返し方）。
- **破棄**: 共有の image は chain の `presentation` が持つ（今と同じく native の alias → image → memory の順に破棄）。group は images の handle を借りるだけ（`borrowed`）で、group の解放はそれを破棄しない。swapchain の image を app が `vkDestroyImage` できない印（`swapchain_owned`）は native の image を破棄する前に外す。

### 変える物

- `wsi-image.c` `vulkan_wsi_shared_image_create`: 引数に追加の usage（direct の時の app の usage、今の道は 0）。
- `wsi-swapchain.c`: `swapchain_create` に「単独の作成か」、`struct vulkan_swapchain` に `direct`、`struct wsi_image_group` に `borrowed`。`swapchain_direct_eligible`（上の 1〜3）、direct の時の作成（group は空の images の表、native の image を app の usage 付きで作り、group の images に入れる。失敗したら native を捨て、普通の group の image を作って今の道へ）、`swapchain_group_free`（borrowed は破棄しない）、`swapchain_native_images_free`（direct は `swapchain_owned` を外してから）。present: `present_prepare` の矩形の判定（direct）、`present_record`（direct の target は copy を記録しない。job の target が全て direct なら command buffer を作らず記録もしない、fence の slot だけ用意）、`present_submit`（その時は command buffer 0 個で submit）。
- i915・Venus・Linux・FreeBSD の compositor・app、cursor・overlay の plane、`vkCreateSharedSwapchainsKHR`、copy の道（readback）は変えない。

### 回帰の範囲（Pi の全ての Vulkan の app）

- compositor の display の swapchain（direct になる。A1 の copy・合成の pass・scene の capture と restore（TRANSFER_SRC/DST）・shot の copy が共有の image に直に効く。display の present は `GPU_DISPLAY_PRESENT_SYNC` の BLOB のまま）。cursor plane（direct にしない、今のまま）。HVS の overlay の plane（WS214、overlay の surface は direct にしない）。
- Wayland の app: videoplayer（自前の presenter）、keiland の toolkit の app（Settings・Files・Terminal など、`KL_PRESENT_VULKAN`）、browser（libbrowser の Vulkan の present）、Image Viewer、vkdemo・wltest。全て direct になる。窓の resize（swapchain の作り直し、oldSwapchain）、全画面の切り替え、最小化・復帰、画面の回転なし。
- 確かめ（T2、Pi 実機）: (a) 起動して desktop が出る（compositor の display）、(b) videoplayer の 60p 全画面と窓（`KWL PERF compose frame_ms`、shown・late、`v3d: perf` の image の pass が 1 表示 frame に 1 つ＝A1 の copy だけになる、`worker=`）、(c) Settings・Terminal・browser を開いて閉じる、窓の resize、(d) keiland-shot の PNG の色・画、(e) `execution failed` 無し。5330（i915）・Venus は direct にならないので build だけ（`vendorID` が違う）。
- host 試験: libvulkan の WSI の host 試験（`plan/ws083`・`plan/ws141` などの既存の host の swapchain の試験があれば流す。direct の判定の表は新しい小さな host 試験で）。

### 危険

- LINEAR の image に描くことは bcm2711 では OPTIMAL と同じ（同じ raster）だが、kernel の image の作成の検査（export、placement の付いた allocation、usage の組）で拒まれたら、今の道へ戻る（作成時に 1 回だけ、log 1 行）。
- display の placement（HVS が読める、1 GiB の下の連続）の付いた allocation に compositor が直に描く。今も copy の写し先として GPU が書いているので、GPU からの書きは今と同じ。

### A3 の実装（2026-10-16、P3 新世代、design-reviewer の review 待ち）

- `wsi-image.c`・`wsi-internal.h`: `vulkan_wsi_shared_image_create` に追加の usage。
- `wsi-swapchain.c`: `WSI_DIRECT_VENDOR`（0x14e4）、group の `borrowed`、chain の `direct`、job の `bare`。`swapchain_create(…, alone, …)`（`vkCreateSwapchainKHR` は VK_TRUE、shared は VK_FALSE）。`swapchain_direct_eligible`（単独・cursor/overlay でない・import_image・GPU_CAP_SHARE・paired の Windows host でない・vendor・EXCLUSIVE・usage が TRANSFER/SAMPLED/STORAGE/COLOR_ATTACHMENT だけで linear の feature が全て支える）、`swapchain_direct_create`（借りる group、app の usage 付きの共有の image、失敗は何も残さず今の道へ）。`swapchain_group_free`（borrowed は破棄しない）、`swapchain_native_images_free`（direct は `swapchain_owned` を外して破棄）。`present_prepare`（direct への矩形は全体だけ）、`present_bare`・`present_record`（bare は slot の fence だけ用意し、記録しない）・`present_recording_key`（bare は key なし）・`present_submit`（bare は command buffer 0）。
- 試験: `plan/ws214/tests/vulkan-overlay-host.c` に (x)（vendor を 0x14e4 にして単独の primary の chain が direct、`vkGetSwapchainImagesKHR` が共有の image、present が submit 1 回・copy 0・native の present 1 回、全体でない矩形は `VK_ERROR_INITIALIZATION_FAILED`、破棄で lease の release）。stub の引数（`vulkan-cursor-host.c` も）と `userland/tests/gpu-share/main.c` の呼びを新しい引数に。

### 確かめ A3（host と build）

| 確かめ | 結果 |
| --- | --- |
| rpi4・amd64 の `dynamic/libvulkan.so`、amd64 `bin/gpu-share-test` | rc 0、warning 0 |
| `sh plan/ws214/tests/vulkan-overlay-host.sh`（ordinary と ASan/UBSan、(x) を含む） | PASS |
| `sh plan/ws141/tests/vulkan-cursor-host.sh` | PASS |
| style-check（変えた file、HEAD との差） | 新しい指摘 0 |

### T2 への依頼 10（Pi 4 実機、QEMU 不要、約 40 分、A3 の review の後）

1. build（A3 の commit か merge 後の main）: `make -j16 BUILD=build/<T>/rpi4 ZEDBSD_CONFIG=plan/ws236/tests/config-rpi4-shot.mk build/<T>/rpi4/dynamic/libvulkan.so build/<T>/rpi4/bin/wayland build/<T>/rpi4/bin/keiland-shot`（warning 0）。kernel は main（`worker=` 入り）、libmedia・videoplayer は今のまま。
2. Pi: `/lib/libvulkan.so`（全 app が使う）・compositor・kernel を差し替え（前の物は退避）、boot の行に `v3d.perf=1`、reboot。
3. (a) desktop が出る（compositor の display の swapchain が direct）、(b) `high-1080p60-10s.mp4 --fullscreen --timeout-s=15` と窓、`high-1080p30-10s.mp4 --fullscreen`、(c) Settings・Terminal・browser（あれば）を開いて閉じる、窓の大きさを変える、(d) (b) の 30p 全画面の 5 秒目に keiland-shot。
4. 報告: videoplayer の `FRAMES`・`DRAW`（acquire_us）・`PLAYBACK`、compositor の `KWL PERF compose`・`rec`・`KWL COPY`、dmesg の `v3d: perf`（`passes=`・`worker=`）・`v3d: gpu tiles`・`execution failed`、PNG、(c) の各 app の見え方（画が出る、崩れない）。
   - CPU の内訳（T1-689 で再生中に context switch が 2 秒に約 19 万・起床 12 万と、desktop の静止時（約 9 千）の 20 倍だった。誰が回しているかを見る）: 60p 全画面の再生の直前と直後に `ps -e -o pid,time,comm` と `sysctl hw.cputimes` を保存。boot の行に `kern.perf=1` も足す（再生中の `kern: perf` の行、`sched:` ではなく `kern: perf` で始まる）。
5. 合否: (a)〜(c) で画が出て落ちない、`execution failed` 無し、PNG の色・画が正しい、`v3d: perf` の image の pass が 1 表示 frame に 1 つ（A1 の copy）に減る（2 秒の `passes=R/0/I` の I が R と同じ程度）。記録: 60p 全画面の shown・late、compose frame_ms、acquire_us。
6. 戻し: 失敗した時は `/lib/libvulkan.so` を退避の物に戻して reboot（全 app が影響を受けるので最初に戻す）。

### 合成の全画面・窓の速さの見積もり（2026-10-16、P3 新世代、T1-683・T1-685 の log から。依頼 6 の数で確かめる）

- **表示の速さは compositor の frame の速さで決まっている**: T1-685 の窓の 1080p60（r-c）で `KWL PERF compose frames=118`／5 s（23.6 fps）、`frame_ms=25`（frame の始まりから完了まで）、videoplayer の `DRAW` も 1 秒 24 frame で `acquire_us` 26 ms（compositor が前の image を返すのを待つ）。compositor は frame を 1 つずつ（`in_flight`）出し、完了の後に窓の commit を最大 frame の半分待つ（`frame_wait_ms`、display.c）ので、frame_ms 25 ms なら 1 frame 約 40 ms。
- **frame_ms 25 ms の中身は V3D の executor の順番待ち**: compositor 自身の GPU の仕事は窓の image の TFU（1280x720 RGBA、TFU 1 job 平均 1.7 ms）と 240 tile の 1 pass（約 2.5〜3 ms）だけ。executor（1 本）は compositor・videoplayer（Y・UV の TFU 2 job と 1 pass）・decode の step を交互に流し、`v3d: perf` busy 86〜91%・subs 280/2 s・1 sub 平均 6.2 ms。表示 1 frame（24 fps）あたり executor 約 37 ms（decode 2 picture 分を含む）。
- **60 fps に要る量**: 表示 1 frame あたりの executor の仕事を約 37 ms から 16 ms 未満へ（半分以下）。videoplayer 側（decode の要求の削り、presenter の重ね）は合わせて数 ms で、届かない。大きい所は 1 frame に 3 回の TFU（約 5 ms）と 2 回の 240 tile の pass（約 5〜6 ms）と decode の step。
- **削り所の候補（ユーザーの判断、Q1 が master の未決に記録済みの A・B に対応）**:
  - A1（compositor）: 出力と同じ大きさの不透明な全画面の窓は、TFU の snapshot と quad の描画をやめ、client の image を swapchain の image へ 1 回の写し（raster→raster）で出す。compositor の 1 frame の GPU を約 4.5 ms → 2 ms 級。
  - A2（compositor）: frame の重ね（`in_flight` を 2 に）と `frame_wait_ms` の見直しで、executor の順番待ちを frame の間隔に足さない。
  - B（WS214 p005）: 再生中の窓・全画面を HVS の plane に載せ、compositor の合成と窓の TFU を無くす（compositor が持ち主のまま）。
  - V1（videoplayer・kernel、要調べ）: V3D の TMU が raster の texture を読めれば、Y・UV の TFU 2 job（約 3 ms）を無くせる（読みは遅くなるので pass の時間との差し引き）。

### ユーザーの判断が要る点（記録、P3 は先へ進む）

- **窓の再生の 60 fps**: 窓では compositor の合成の pass（GPU 約 8 ms/frame の見込み）と窓の image の TFU が毎 frame 要り、videoplayer 側だけでは届かない。案は (A) compositor の合成の短縮（窓の下の層を描かない遮蔽の省略など、WS141 p013・WS211 の範囲）、(B) WS214（窓を HVS の plane に載せる）の p005 で再生中の窓を plane に載せる、(C) 60 fps は全画面（game mode）だけを目標にする。~~P3 は (C) の道（`--fullscreen`）を先に確かめる。~~ 2026-10-16 訂正（P3 新世代、Q1）: (C) の game mode は WS223 のユーザーの決定（2026-10-13「Video Playerは、Game Modeをやめます」）に反するので取らない。全画面も合成で、60 fps の道は (A)・(B)・WS223 の見直しのどれかをユーザーが選ぶ（Q1 が master の未決に記録）。
- **全画面の切り替えの出力の閉じ開き**: WS223 の p002（`scanout-rules.c` の content の条件を `game` だけに）が入るまで、Video Player の全画面は game mode を試して swapchain を 1 回閉じて開き直す（約 60 ms の止まり、`KWL MODE window switch_ms=59〜64`）。直し 6 で VPU は止まらなくなる見込みだが、無駄な閉じ開きは WS223 p002 で消える（Q1 の割り当て）。
- **先読みの picture 3 → 2**: 1080p60 の decode の throughput（flight 4）のため。30p の再生で揺れ（late）が増えるなら戻すか、libmedia の slot の上限（`H264_DPB_SLOTS` 17、kernel の `VIDEO_MAX_DPB_SLOTS` 17）を広げる（kernel は slot の数を使っていないので広げられるが、libmedia の DPB の表の大きさが変わる）。

## 残り・危険

- 段 a の YUV の候補は当たらなかった（B-2 に決めた）。
- frame の pitch・chroma の offset が image の配置と違えば direct にならず、bounce からの CPU の写し（補正なし）が残る（log で分かる）。
- 段 c の shader の compiler（kernel）は picture.frag（2 枚の sample と push の行列）を host で compile できた。実機の値（色）は依頼 3 で見る。
- 1 frame ずつ fence を待つ presenter（Image Viewer と同じ）。1080p30 の再生には十分の見込みだが、60 fps の表示で足りなければ 2 frame を重ねる（次の段）。
- slot の数が 17 を超える stream（参照・reorder が多い）は readback の道に戻る（log の `pictures are read back`）。
- i915（5330）の videoplayer は変えていない（arm64 だけ）。i915 で同じ道を使うには i915 の kernel に NV12 の plane の view と export が要る（範囲外）。

- 2026-10-16 ユーザー（T2 の T1-681 の再生の最中、OLED を目視）:「Piは少なくとも30fps、60fpsに近いフレームレートで動画が再生されました」（b4452d35d の kernel、GPU の picture の道の見込み。数は T1-681 の結果で確かめる）。
