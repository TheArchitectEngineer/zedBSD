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
