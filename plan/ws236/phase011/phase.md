<!-- awesome-plan project=zedbsd record=ws236-p011 -->

# ws236-p011: 案 1: decoder の出力を GPU の texture に、表示を GPU で

Parent: [WS236](../ws.md)
Status: in-progress（2026-10-16、P3。段 a の probe は実装・build 済み、T2 の Pi 実機待ち）
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
| a | TFU の probe（起動の診断、boot の `v3d.tfu_probe=1` の時だけ）: (1) 1・2 byte の texel の raster→UIF が CPU の配置と合うか、512x480 R8・256x240 RG8 の時間（1080p の見積もり）、(2) YUV 入力の候補の設定（入力の format 0〜10、NV12 と I420 の分け方）で RGBA8 を書くか、BT.601・709・full のどれと合うか | `v3d-tfu-probe.c`（新規）、`v3d-diagnostic.c`、`v3d-job.c`・`.h`（TFU の job に chroma・U plane の address）、`native-texture.c`（1・2・4 byte の UIF の大きさと texel の offset）、`stage.c` | 実装・build 済み、T2 待ち |
| b | VPU の bulk を NV12 の image の memory へ直に（place を無くす）。補正（2〜15 の +1）は buffer への readback の時だけ | `vulkan-video.c`、`rpi4-mmal.c` | 未着手 |
| c | V3D: R8・RG8 の sampled image、NV12 の image に SAMPLED と plane の view（PLANE_0 を R8、PLANE_1 を RG8）、snapshot（raster→UIF）を 1・2 byte の texel に（TFU と CPU の予備）。a の YUV が当たればその道（B-1）を選ぶ | `vulkan-query.c`・`vulkan-resource.c`・`vulkan-native-image.c`・`native-texture.c`・`vulkan-native-draw.c`・`native-storage.c` | 未着手 |
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

## 残り・危険

- 段 a の YUV の候補は公開の資料が無いので当たらない見込みもある（その時は B-2: R8・RG8 の 2 枚を shader で変換）。
- 段 c の shader の compiler（kernel）が 2 枚の sample と行列の演算を扱えるかは未確認。
