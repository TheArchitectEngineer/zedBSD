<!-- awesome-plan project=zedbsd record=ws291-p002 -->

# ws291-p002 3D の scene を決め、Linux と zedBSD の差を測る

- 親: [WS291](../ws.md)。
- 状態: in-progress（2026-10-26 23:47 P2、q1596-i01）。Linux の基準は測った。zedBSD 側は [p002a](../phase002a/phase.md)（amdrdna の executor に depth・index draw・mip を足す）の後。
- 依存: p001（cleared、main bad29b52f）、p002a（zedBSD の計測の前提、2026-10-26 ユーザー「機能を足してから比較（推し）」）。

## 範囲

1. mview の拡張: `--content=game`（Keiland の game mode の direct scanout）、`--present=fifo|mailbox|immediate`、`--copies=N`（model を N 個、格子に並べる）。
2. Linux（Debian 13、RADV）で fullscreen の direct scanout の上で約 50 fps になる scene を決める。
3. zedBSD で同じ scene を測る（`KWL SCANOUT direct=1` を確かめる）。

## 受け入れ

- scene の定義（model・数・解像度・shading・present mode）、両 OS の fps、direct scanout の証拠、mview の改良の範囲を記録。

## mview の改良の範囲（userland/tests/mview、commit 0b0bfaf70）

- `--content=none|video|game`: wp_content_type_v1 の manager を bind し、最初の commit の前に surface の content type を言う（compositor に無ければ言わない）。
- `--present=fifo|mailbox|immediate`: surface が持つ時だけその mode、無ければ FIFO。`MVIEW PRESENT` の行に asked・mode・copies・content・content_manager。
- `--copies=N`（1〜4096）: 1 frame に model を N 個描く。列は √N の切り上げ、行はその残り、copy ごとに view 平面の pan を model の直径ずつずらし、視点を列の数の倍だけ下げる（全体が見える）。copy ごとに material の group の数だけ draw（qs40 は 16 group）。
- Linux の build: `plan/ws291/tests/linux/build-mview.sh OUT`（system の libwayland-client・Vulkan loader・Mesa の WSI、wayland-scanner の xdg-shell と content-type の code、`linux/content-type-shim.c`）。Keiland の Linux build（keiland-linux.mk）は Keiland の libwayland と WSI を挟むので基準に使わない。

## 結果

### Linux の基準（実機 10.0.10.3、2026-10-26 23:45）

- 環境: Debian 13、kernel 6.12.111+deb13-amd64、mesa-vulkan-drivers 25.0.7-2+deb13u1（RADV NAVI10）、firmware-amd-graphics 20250410-2（この Phase で入れた。それまで amdgpu は firmware が無く init に失敗していた）、weston 14.0.2 の kiosk shell・DRM backend（seatd）、DP-1 の DELL SE2426H 1920x1080@60。
- direct scanout の証拠: weston の `--logger-scopes=log,drm-backend` の log の repaint ごとの判定。1500 copies の 10 秒で `Using plane-only state composition` 450・`Using mixed state composition` 61（mixed は client の FB が取れない frame、`couldn't get FB for view`）。大半の frame で mview の window が primary plane 70 に直に載る。
- scene: qs40（25861 vertices・37000 triangles・16 materials・13 textures）、fullscreen 1920x1080、vertex shading（既定）、MAILBOX、`--spin=10`。

| copies | fps | frame ms avg | record ms | fence ms（GPU） |
| --- | --- | --- | --- | --- |
| 1 | 9255 | 0.11 | 0.01 | 0.06 |
| 64 | 733 | 1.36 | 0.60 | 0.71 |
| 256 | 219 | 4.55 | 2.05 | 2.43 |
| 1024 | 68.6 | 14.5 | 6.83 | 7.64 |
| 1400 | 53.7〜53.8 | 18.6 | 8.7〜8.8 | 9.7 |
| **1500** | **50.9〜51.0**（4 回） | 19.6 | 9.3 | 10.2 |

- 決めた scene: **`mview --spin=10 --content=game --present=mailbox --copies=1500`**（1920x1080、qs40、vertex shading）。Linux 50.9 fps。IMMEDIATE も MAILBOX と同じ（1400 で 53.6）。FIFO の 1 copy は 59.9（60 Hz の上限）。
- mview は 1 frame ずつ CPU の記録と GPU の完了を待つ（command buffer 1 本と fence 1 個）ので、frame の時間は record（1500×16 draw の記録、CPU）と fence（GPU）の和。zedBSD でも同じ program なので比べられるが、driver の記録の道（zedBSD は vkCmd* を executor へ送る）の速さも含む。
- 計測: `plan/ws291/tests/measure.sh linux-mview OUT --token=... --spin=10 --content=game --present=mailbox --copies=1500`。

### zedBSD

- 2026-10-26 23:20 実機: mview が `MVIEW FAILED api=depth format result=-11`。amdrdna の executor（src/drivers/gpu/amdrdna/vk）は depth の format を持たない（vk-device.c）、render pass は colour 1 枚だけ（vk-target.c）、image は 1 level・pitch linear（vk-resource.c）、BindIndexBuffer・DrawIndexed を記録しない（vk-record.c）。→ p002a。

## 記録

- 2026-10-26 23:47 P2: 新設（Q1 の指示で phase.md を作る）。mview の拡張、Linux の基準の計測。
