<!-- awesome-plan project=zedbsd record=ws291-p002 -->

# ws291-p002 3D の scene を決め、Linux と zedBSD の差を測る

- 親: [WS291](../ws.md)。
- 状態: in-progress（2026-10-26 23:47 P2、q1596-i01）。両 OS を同じ条件で測った（2026-10-27 00:40）: 主の scene（qs40 ×6500 instanced）は zedBSD 20.1 fps・Linux 50.1 fps。clearance は Q1 の判定。
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

## benchmark の作り直し（2026-10-26 ユーザー、Q1 経由）

- 原文「そのベンチマークですが、command組み立ては毎回やってたらVF2の方がCPUが遅いので確実に遅くなります。頂点データやインデックスはバッファに入れておいて、描画コマンドの組み立て時間が最小限になるように変更してください。でないと実際のユースケースと合わないです。ただ、別途、たくさんのコマンドをキューするようなベンチマークも、必要かもしれませんね。」「コマンド投入がzedBSDでは遅い可能性もあるので、コマンドが大きいか、コマンドがたくさんあるか、は、RDNAのLinuxとzedBSDで比較できるといいですね。」
- mview の変更: copy の位置を per-instance の vertex buffer（copy ごとに float 4 個: clip 空間の xy のずれ、view 空間の xy のずれ）に置き、shader（mview.vert・pixel.vert、location 3 の copy_offset）が足す。buffer は格子のずれ（1 列・1 行の clip のずれ）が変わった時だけ CPU が書き直す（spin では最初の 1 回だけ）。頂点・index・texture は最初に device の buffer へ置いたまま。
  - 既定（`--draws=instanced`）: material の group ごとに 1 回の instanced の DrawIndexed（instanceCount = copies）。1 frame の記録は 16 draw、copies に依らない。**主の benchmark**。
  - `--draws=each`: copy ごとに 16 draw（firstInstance = copy）。1 回の submit に 16×copies の draw（大きな command buffer）。
  - `--submits=N`: frame を N 個の command buffer に分け、N 回の vkQueueSubmit で出す（最初の pass が clear、途中は load・store、最後が present）。copy は N 個に等分。`--draws=each --copies=N --submits=N` で 1 submit あたり 1 copy（16 draw）の N 回の submit。
  - `MVIEW STAGES` の record は frame の全 command buffer の記録、submit は全 vkQueueSubmit、fence は GPU の完了の待ち。
- 計測の組（`plan/ws291/tests/bench.list`、`measure.sh bench zedbsd|linux OUT` で 1 回に流し `mview-table.py` で表に）: scene-1・scene-1500・scene-6500（instanced）、big-1・10・100・1000（1 submit に 16〜16000 draw）、subs-10・100・1000（N submit × 16 draw）。各 8 秒、MAILBOX、content game。

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

### Linux（作り直した benchmark、2026-10-27 00:00、build/ws291/measure/bench-linux-2/table.txt、時間は frame あたり ms）

| run | fps | frame | record | submit | present | fence（GPU） |
| --- | --- | --- | --- | --- | --- | --- |
| scene-1 | 8833 | 0.11 | 0.01 | 0.02 | 0.01 | 0.06 |
| scene-1500 | 201 | 4.97 | 0.11 | 0.10 | 0.07 | 4.63 |
| scene-3000 | 105 | 9.49 | 0.12 | 0.10 | 0.07 | 9.13 |
| big-1 | 9391 | 0.11 | 0.01 | 0.02 | 0.01 | 0.06 |
| big-10 | 2810 | 0.36 | 0.10 | 0.02 | 0.02 | 0.20 |
| big-100 | 528 | 1.89 | 0.77 | 0.04 | 0.03 | 1.03 |
| big-1000 | 84.2 | 11.86 | 5.62 | 0.02 | 0.02 | 6.18 |
| subs-10 | 3806 | 0.26 | 0.06 | 0.10 | 0.01 | 0.09 |
| subs-100 | 519 | 1.93 | 0.59 | 0.90 | 0.01 | 0.41 |
| subs-1000 | 57.7 | 17.29 | 6.82 | 10.17 | 0.03 | 0.26 |

- 主の scene を決め直した: **`mview --spin=8 --content=game --present=mailbox --copies=6500`**（instanced、qs40 × 6500 の 81×81 の格子、1920x1080、vertex shading）→ Linux **50.0 fps**（frame 19.9 ms、record 0.15・submit 0.10・fence 19.6）。6000 copies は 54.1。weston の log で repaint の大半が plane-only（6500 で 258、mixed 64）。
- 見た目の基準（weston-screenshooter、`--debug` の weston）: build/ws291/measure/ref/linux-shot-1.png（1 体）・linux-shot-6500.png（細かい格子、1 体が十数 pixel）。6500 copies の scene は三角形が pixel より小さい geometry の重い scene で、pixel の負荷は軽い（制限として記録。pixel の重い scene が要れば別に足す）。
- 以前の per-copy draw の 1500 copies（50.9 fps）は「毎 frame 24000 draw を記録」の scene で、今は big-N 系がその役。

### zedBSD と Linux（同じ binary の source・同じ条件、2026-10-27 00:40 頃、frame あたり ms）

zedBSD: c8ba529 ＋ p002a（b20e5545e）・scanout の import の修正（160ad707f）・mview（commands の 1 個ずつの確保）、build/ws291/measure/bench-zedbsd-2/table.txt。
Linux: 上の環境、build/ws291/measure/bench-linux-3/table.txt。両方とも format 44（B8G8R8A8_UNORM）、MAILBOX、content game、8 秒の spin。

| run | zedBSD fps | Linux fps | zedBSD record / submit / GPU 待ち | Linux record / submit / GPU 待ち |
| --- | --- | --- | --- | --- |
| scene-1 | 58.4 | 9604 | 0.26 / 2.75 / 0.04（acquire 12.1） | 0.01 / 0.02 / 0.06 |
| scene-1500 | 57.5 | 201 | 0.63 / 12.8 / 0.03 | 0.11 / 0.10 / 4.62 |
| **scene-6500** | **20.1** | **50.1** | 1.90 / 44.7 / 0.03 | 0.14 / 0.10 / 19.5 |
| big-1（16 draw） | 58.1 | 9345 | 0.35 / 2.48 / 0.07 | 0.01 / 0.02 / 0.06 |
| big-10（160） | 57.6 | 2660 | 0.69 / 8.26 / 0.03 | 0.11 / 0.03 / 0.21 |
| big-100（1600） | 6.06 | 533 | 89.6 / 69.4 / 0.02 | 0.76 / 0.04 / 1.02 |
| big-1000（16000） | 0.53 | 86.9 | 918 / 666 / 0.04 | 5.42 / 0.02 / 6.01 |
| subs-10 | 41.7 | 3797 | 2.24 / 19.2 / 0.03 | 0.06 / 0.10 / 0.09 |
| subs-100 | 4.35 | 521 | 21.0 / 193 / 0.02 | 0.59 / 0.91 / 0.39 |
| subs-1000 | 0.37 | 58.2 | 210 / 1806 / 0.01 | 6.63 / 10.1 / 0.36 |

- 読み方: zedBSD の executor は vkQueueSubmit の中で GPU の完了まで待つ（indirect buffer ごとに待つ、vk-execute.c）ので、zedBSD の submit は GPU の時間を含み、fence は 0 に近い。Linux は submit が返ってから fence で待つ。
- direct scanout: zedBSD は `KWL SCANOUT direct=1`（scene-6500 で switch_ms=84）。Linux は weston の plane-only 264・mixed 61。
- zedBSD の direct scanout は Keiland の game mode が flip ごとに vblank を待つ（GPU_DISPLAY_PRESENT の FIFO、amdrdna の present が flip を待つ）ので、MAILBOX でも 60 fps が上限（scene-1・big-1 は 58 で acquire が 12 ms）。zedBSD の WSI は IMMEDIATE を持たない（FIFO に落ちる）。60 fps 未満の scene-6500・big-100 以上・subs はこの上限に掛からない。
- 差の大きい所（改善の案は Q1 へ送った、p003 以降）:
  1. scene-6500（GPU の重さ）: 44.7 ms 対 19.5 ms（2.3 倍）。RADV は Navi10 で NGG の primitive の culling（小さい三角形を shader で捨てる）を使う。amdrdna は legacy の vertex program で culling が無い。他に HiZ（HTILE）無し、texture が linear。
  2. 記録（big-100: 1600 draw で 90 ms、Linux 0.76 ms）: kernel の executor の record の decode（object の lookup・record ごとの kern_malloc・append）が 1 command 約 14 µs。
  3. submit（subs-100: 1 submit 約 1.9 ms、Linux 0.009 ms）: submit ごとに indirect buffer を作り、state を全部書き、GPU の完了を待つ。draw ごとに全 state と 2 つの resource table を書く。
  4. 60 fps の上限（game mode の flip の待ち）: mailbox 相当（新しい image が来たら待っている方を返す）と待たない flip。
- 試験の補助で直した所（p002 の範囲で必要だった物）: mview は B8G8R8A8_UNORM を先に選ぶ（amdrdna の scanout が BGRA だけを受ける。Linux も同じ format で測った）、command buffer を 1 個ずつ確保（executor は 1 回 64 個まで）。Keiland の zedBSD の scanout backend が import に image の記述を渡していて kernel（gpu_import_ioctl「Never trust receiver-supplied descriptions」）が EINVAL で断っていた → 渡さないよう直した（160ad707f、全 GPU の game mode に効く）。refused の段を `KL SCANOUT refused step=...`、amdrdna の present の拒否を kernel の log に出すようにした。

### zedBSD（以前の記録）

- 2026-10-26 23:20 実機: mview が `MVIEW FAILED api=depth format result=-11`。amdrdna の executor（src/drivers/gpu/amdrdna/vk）は depth の format を持たない（vk-device.c）、render pass は colour 1 枚だけ（vk-target.c）、image は 1 level・pitch linear（vk-resource.c）、BindIndexBuffer・DrawIndexed を記録しない（vk-record.c）。→ p002a。

## 記録

- 2026-10-26 23:47 P2: 新設（Q1 の指示で phase.md を作る）。mview の拡張、Linux の基準の計測。
- 2026-10-27 00:05 P2: ユーザーの指示で benchmark を作り直し（instanced の主の scene、big-N・subs-N の command の重さの組）、Linux を再計測。scene は 6500 copies で 50.0 fps。
- 2026-10-27 00:42 P2: p002a の後、zedBSD と Linux を同じ benchmark（bench.list）で測った。Keiland の scanout の import を直して direct scanout が動いた。改善の案を Q1 へ。
