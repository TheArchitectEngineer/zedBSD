<!-- awesome-plan project=zedbsd record=ws035p057 -->

# ws035-p057: 効果: 背後の窓のぼかし

Phase ID: `ws035-p057`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-27、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行。元は sq001 の planned）

## 範囲

2026-09-25 承認の「すりガラス（背後のぼかし）と影」のうち、client の card のガラスと窓の alpha は [p083](../phase083/phase.md) に分けた。
残りの**背後の窓のぼかし**: title bar・窓の glass の panel・see-through の body のガラスが、ぼかした壁紙だけでなく、その窓の下の窓も
ぼかして見せる（[compositing-design.md](../compositing-design.md) D10）。damage で広げる所は p055。

## 設計（決定、2026-09-27）

- **下の scene を小さく描き直す**（出力の画像を copy しない）: 下に窓がある窓を描く前に、出力の render pass を終え、その窓の下の scene（壁紙と
  下の窓の body と浮いた title bar のガラス、下の窓自身のガラスはぼかした壁紙）を出力の 1/8 の大きさ（1280x800 なら 160x100）の画像へ描き、
  横・縦の Gaussian（9 tap を 5 回の読みで、1.5 texel 間隔）を 2 回ずつかけ、出力の pass を LOAD で再開する。その窓のガラス（`MODE_GLASS` で自分の
  画像を持たない shape）は、この画像を画面の座標で読む（`glass_shape_draw` の `backdrop_set`）。popup・system bar・menu はぼかした壁紙に戻す。
  - 理由: swapchain の画像を TRANSFER_SRC にせず（vkdemo の display と i915 の native の経路を変えない）、既存の panel の pipeline（小さい pass と
    互換: 同じ format・1 sample）と shader の座標（出力の pixel、viewport に依らない）をそのまま使える。下の窓は title・menu・button を描かない
    （ぼけて見えないうえ、menu と control の hit を 2 度記録してしまう）。
  - 費用: 下に窓がある窓 1 つにつき、小さい scene と 4 回の 160x100 の pass。窓が N 枚なら scene の描画は O(N²) の小さい draw。
- **遅延**: 画像・pass は、初めて要る frame で作る（起動の仕事を増やさない。Venus の READY の時刻を変えない）。作れない device は以後ずっと
  ぼかした壁紙（`ZWL BACKDROP failed`）。出力を閉じるとき解放。
- 新しい shader の mode `MODE_BLUR`（6、panel.frag）。text は `mode < 5.5` の枝に（i915 の native compiler の OpSwitch を避ける if の連鎖のまま）。

## 実装（2026-09-27）

- 新 `userland/base/zdesktop/backdrop.c`（`zwl_backdrop_begin`・`_end`・`_reset`・`_destroy`、小さい pass、LOAD の resume pass、2 枚の image）、
  `compose.h`（`struct zwl_backdrop`、`framebuffer_now`、`backdrop_set`）、`compose.c`（frame ごとに framebuffer と backdrop_set、出力を閉じるとき
  解放）、`glass.c`（`backdrop_set` を先に）、`glass.h`（`MODE_BLUR`）、`shaders/panel.frag`・`shaders.h`、`shell.c`（窓の loop で `draw_backdrop`、
  `window_shown`・`window_layer` に分けた、`draw_window_blurred`）、`Makefile`。
- 試験: 新 `plan/ws035/tests/zdesktop-p057.sh`。

## 検証（amd64 だけ、2026-09-27）

- build: lean image（`plan/tools/files/build-files-image.sh build/amd64`）、warning 0。style: `backdrop.c` 0、変えた file は悪化なし。
- guest（QEMU、Venus）: `zdesktop-p057.sh` PASS（`ZWL BACKDROP ready width=160 height=100`、extras-probe の赤い sub-surface の上の zdesktop-files の
  content の card の画素が (233,165,161)、probe を閉じた後は (211,227,223): 差 62、赤へ寄る）。画面 `build/ws035-p057/{over,gone}.png`。
  最初の版（1/4、1 回のぼかし）はぼけが弱く下の窓の縁がくっきり見えた → 1/8・2 回に。
- 回帰（同じ image）: menu-regress（p059 p062 p063 p064 p065 p068 p069 p070 p071 p072 p014 p076 p077 p078 p079 p080 p081）・menu-p002・
  menu-p003・titlebar-p008・p009・p010 PASS、files-regress（p002〜p008・p012〜p015）PASS、boot test PASS（`build/ws035-p057-boot/login.png`）。

画面（ユーザー向けの写し）: `/home/awe/zedBSD-rpi4/build/ws035-shots/p057-20260927-venus-{glass-over-window,glass-window-gone}.png`。
- 実機（i915）: 未実施（i915 の native の driver で offscreen の pass・LOAD の再開・小さい画像の標本が動くかは未確認。失敗なら壁紙に戻る作り）。
