# ws270-p007 P1 の記録（q1538 / q1538-i01、Keiland 側）

Phase の本体は [phase.md](phase.md)（Q1 が書く）。ここは P1 の作業の記録。

## 読んで分かった事（2026-10-21、code の読み、板は未実施）

- Keiland はすでに damage を持つ（ws035-p055・ws211）: buffer age の history（`compose_region_since`）、damage の矩形ごとの scissor と draw
  （`compose-bind.c`）、矩形の外の quad の省略（`kwl_compose_clip_visible`）、窓の移動・cursor・client の commit の測った damage
  （`damage.c`・`shell.c` の `drag_damage`）。全画面になるのは `server->dirty`（測れない変化）と、damage が画面の 7 割を超える時。
- driver の費用の形（`src/drivers/gpu/powervr`、読むだけ）:
  - 1 つの render job は draw を 64 まで持つ（`POWERVR_RENDER_DRAWS`）。pass の draw が 64 を超えるごとに次の job が増え、その job は
    render area 全体を LOAD する（`powervr_render_job_continue`）。render-pass.h の注記: 全画面の compositor の job 1 つ約 37 ms。
  - LOAD は全画面の draw で行う（`load_draw_build`、scissor は render area）。板 d42: 全画面の LOAD 23.35 ms。
  - transfer（copy・blit、host image の copy）は前の render の完了を待つ（`order_after`）。
- Keiland の側で pass が切れる所: backdrop（`backdrop.c`、blur を頼む窓・docked の中央の窓の下の scene を小さく描いて blur し、
  出力の pass を LOAD で再開）、scene の capture（cursor が plane に無い時だけ）、layer の picture（App Home など）。

## 計測の追加（dee85df07）

- `KWL PASSES`（`--log-frames` の時だけ、frame の完了の時に KWL DRAWS の次に出る）: frame の list の pass の数、job の数（64 draw ごとに
  1 job と数えた推定）、draw の数、LOAD する pass の数、pass の面積の和（area_px）、LOAD する画素（load_px: LOAD の pass の面積と、
  続きの job ごとの面積）、copy の数と画素、最初の 8 つの pass の `x,y,WxH:draws[l]`。全画面 1 枚 = 2073600 px（1920x1080）。
- 確認: `make -j16 ZEDBSD_CONFIG=plan/ws270/tests/config-visionfive2-desktop-gpu.mk BUILD=build/q1538 build/q1538/bin/wayland` rc 0、
  warning 0。板は未実施（Q1 に依頼済み、scenario A〜E）。

## 板の数と変更（2026-10-21）

| 板 | wayland | 3 窓の全画面の frame | terminal の部分 frame |
| --- | --- | --- | --- |
| d48 | dee85df07（計測） | draws 263・jobs 5（driver の 64 draw ごと）・blended 7.2 M px、rec submit 392 ms | （terminal の command が失敗、未取得） |
| d50 | ef1a02591（glyph の batch） | draws 263→132、MODES image 6.98 M px・glass 2.10 M・shadow 1.32 M・text 153 glyph、rec submit 300〜320 ms（P2 の 512 draw の kernel） | 同上 |
| d56 | 98c7a4ccf（壁紙・desktop の穴）、P2 の vf2c kernel | draws 165、image 4.93 M px、rec submit 約 160 ms | 916x632 の部分、draws 26、opaque 1.01 M・blended 2.51 M px（面積の 6 倍）、rec submit 約 66 ms |

変更（commit、全部 VF2 の build で rc 0・warning 0）:

- dee85df07・720d55dbb: `KWL PASSES`（pass・job・LOAD の画素・copy）と `KWL MODES`（mode ごとの draw/px）の行（`--log-frames`）。
- ef1a02591: 連続する glyph を 1 draw に（`shaders/panel-text.*`、batch.c の text の道、最大 85 glyph）。PowerVR の compiler の host 試験（p010-compiler の複写に 2 shader を足す）425 passed。
- 98c7a4ccf: 壁紙と desktop の surface を窓の不透明な内側の下で描かない（`kwl_damage_rects_subtract`、帯に分けて最大 64 片）。subtract の host 試験（全 pixel の総当たり、固定 9 例・乱数 200 例）bad 0。
- ec2694de6・c3dde7fc7: 一般化（下の窓の shape も上の窓の穴の下で描かない、`glass_shape_cover`、`server->cover_*`）、opaque region（compositor の wl_region 保持・`set_opaque_region`、libkeiland の `kl_window_set_opaque`、KL_VERSION 95、terminal が宣言）、desktop の描いた範囲（`kl_desktop_surface_v1` version 2 の `set_drawn`、libkeiland が present の時に 32x32 の tile で見て送る）。desktop の tile→矩形の host 試験 bad 0。

板で分かった事:

- damage は効いている。全画面になるのは dirty=1（map・configure）と、その後 swapchain 3 枚の buffer age の追いつきの 2 frame。
- d50 の D'（terminal の出力）は command が失敗していた（画面に「test: -lt: unexpected operator」）。
