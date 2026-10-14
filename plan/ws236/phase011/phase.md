<!-- awesome-plan project=zedbsd record=ws236-p011 -->

# ws236-p011: 案 1: decoder の出力を GPU の texture に、表示を GPU で

Parent: [WS236](../ws.md)
Status: in-progress（2026-10-16、P3。段 a は T2 の T1-674 で結果済み（B-2 の道）、段 b・c の kernel は実装・build・host 試験済み、段 d・e を実装中）
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
| b | VPU の bulk を NV12 の image の memory へ直に（place を無くす）。補正（2〜15 の +1）は buffer への readback の時だけ | `vulkan-video.c`、`rpi4-mmal.c`・`.h`、`vulkan-buffer-copy.c`、`vkvideo-probe`（`--sampled`） | 実装・build・host 試験済み、T2 の probe 待ち |
| c | V3D: R8・RG8 の sampled image、NV12 の image に SAMPLED と plane の view（PLANE_0 を R8、PLANE_1 を RG8）、snapshot（raster→UIF）を 1・2 byte の texel に（TFU と CPU の予備）。a の YUV が当たればその道（B-1）を選ぶ | `vulkan-query.c`・`vulkan-resource.c`・`vulkan-input.c`・`.h`・`native-texture.c`・`native-state.h`・`vulkan-native-draw.c`・`.h`・`native-storage.c`・`.h` | 実装・build・host 試験済み（実機は段 e と一緒） |
| d | libmedia: GPU の picture の API（VkImage と plane の view、表示が終わるまで DPB の slot を busy に）、VkDevice を app と共有 | `userland/desktop/libmedia/` | 未着手 |
| e | videoplayer: 描画を Vulkan に（YCbCr→RGB と縮小を shader で、自前の swapchain、KL_PRESENT_NONE）。UI は内容が変わった時だけ texture へ | `userland/desktop/videoplayer/` | 未着手 |

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

## 残り・危険

- 段 a の YUV の候補は当たらなかった（B-2 に決めた）。
- frame の pitch・chroma の offset が image の配置と違えば direct にならず、bounce からの CPU の写し（補正なし）が残る（log で分かる）。
- 段 c の shader の compiler（kernel）が 2 枚の sample と行列の演算を扱えるかは未確認。
