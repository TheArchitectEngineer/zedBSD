# T1-697 WS241 p001: RTX の描画の速さ（X1＋計測 M1〜M3、診断）

RTX 機 10.0.30.5（実機）。tree 27b361f75（main）。2026-10-16。QEMU は使っていない。
build: `make -j16 BUILD=build/t1/nvrtx-perf2 ZEDBSD_CONFIG=plan/ws037/tests/config-amd64-nvrtx.mk build/t1/nvrtx-perf2/vmunix`（warning 0、34 s、vmunix check PASS、cksum 3247642944 3607816）。/boot/boot0/vmunix に差し替え（元 /boot/boot0/vmunix.pre-t1-697）。cfg は (a) `nvrtx.vk.perf=1`、(b) 加えて `nvrtx.boost=1`、各 1 回の起動で 3 場面。終了後 cfg は元に戻した（/boot/boot0/zedbsd.cfg.pre-t1-697 から複写、再起動はしていない＝今動いている起動は (b) の設定のまま）。kernel は新しいまま。
場面: scenes.sh（静止 20 s／`timeout 20 files`／terminal の `sh /tmp/spam.sh`（無限 echo）26 s ＋ 3 s 後に files 20 s）。
成果物（絶対 path）: /home/awe/zedBSD-worktrees/t1/plan/agents/T1/t1-697/ の scenes.sh・extract.py・marks-{a,b}.txt・kernel-{a,b}.log・session-{a,b}.log（KWL PERF 全文）・dmesg-{a,b}.txt・ext-{a,b}.txt・misc-{a,b}.txt。
実機の証拠のみ。

## 判定（診断）

- 失敗の行（`D3 fail`・`a command buffer did not run`・`rm: control`・`P8c fail`・`D2: the renderer faulted`）: (a)(b) とも 0 件。既知の P4m bar1 の fault 3 行のみ（P4m ok が続く）。
- perf の行: 5 行（runs/cpu copies、frames/flips、gpu、draws/quads/push、streams）は毎 window 出る。ただし `streams` は全 window で `0 0KiB 0us`。
- `P9p`: (a) `P9p: performance state P0`。(b) `P9p: performance state P0` と `P9p: boosted to performance state P0`。
- 崩れ: keiland-shot が image に無く PNG は**未実施**。app の log（ZFILES READY 1120x720、ZTERM START、VK SWAPCHAIN direct=0 reason=renderer）に error 無し。目視は無いので「崩れ無し」とは言えない。

## KWL PERF（compose、5 s の window ごと。時間は ms）

| 場面 | 起動 | window | compose frames | frame_ms | acquire | submit+present | rec submit（平均／最大） | rec present |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 静止 20 s | a・b | — | 0（frame 無し） | — | — | — | — | — |
| Files 20 s | a | 1 | 10 | 15.57 | 2.47 | 13.92 | 10.89／13.43 | 2.37 |
| Files 20 s | a | 2 | 1 | 11.79 | 0.05 | 11.41 | 8.83／8.83 | 2.25 |
| Files 20 s | b | 1 | 1 | 6.87 | 0.06 | 6.47 | 5.42／5.42 | 0.70 |
| Files 20 s | b | 2 | 10 | 7.57 | 1.96 | 7.05 | 5.77／7.61 | 0.71 |
| Terminal+Files | a | 1 | 111 | 13.44 | 6.60 | 12.05 | 8.28／22.91 | 2.94 |
| Terminal+Files | a | 2 | 125 | 24.00 | 8.56 | 22.25 | 18.41／35.09 | 2.61 |
| Terminal+Files | a | 3 | 125 | 23.79 | 9.23 | 22.36 | 18.49／35.60 | 2.62 |
| Terminal+Files | a | 4 | 122 | 24.68 | 9.98 | 23.21 | 19.37／99.50 | 2.61 |
| Terminal+Files | b | 1 | 77 | 4.02 | 5.74 | 3.17 | 1.97／10.53 | 0.71 |
| Terminal+Files | b | 2 | 212 | 9.90 | 7.20 | 8.77 | 7.21／31.18 | 0.66 |
| Terminal+Files | b | 3 | 143 | 20.96 | 7.20 | 14.55 | 12.75／34.19 | 0.59 |
| Terminal+Files | b | 4 | 176 | 15.44 | 6.59 | 13.87 | 12.15／29.25 | 0.58 |
| Terminal+Files | b | 5 | 174 | 15.60 | 6.56 | 13.98 | 12.17／31.24 | 0.56 |

draw_ms は 0.06〜0.21。T1-696 の同場面は Files 16.98、Terminal+Files 約 27.8。

## kernel.log の perf（0 だけの window は省く。run は nvrtx の GPU run）

| 場面 | 起動 | runs・avg us | cpu copies | frames・wait us・held us | flips・avg us | gpu N・avg us・max us・run max us | draws・quads・push KiB | streams |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Files | a | 28・2968 | 0 | 302・2・561 | 10・9532 | 28・2765・5615・6001 | 1068・18・813 | 0 |
| Files | b | 2・179 | 0 | 8・0・711 | 1・15354 | 2・170・272・281 | 99・1・73 | 0 |
| Files | b | 28・167 | 0 | 302・0・278 | 10・8068 | 28・159・403・411 | 1068・18・813 | 0 |
| T+F | a | 248・2143 | 0 | 1294・1・608 | 62・9379 | 248・1953・4811・5720 | 2629・124・2457 | 0 |
| T+F | a | 552・2643 | 0 | 2412・0・1225 | 136・9673 | 552・2461・6003・6925 | 22854・280・17093 | 0 |
| T+F | a | 508・2828 | 0 | 2159・0・1451 | 127・9797 | 508・2644・6000・6968 | 25579・254・19141 | 0 |
| T+F | a | 496・2795 | 0 | 2108・0・1442 | 124・10623 | 496・2627・6044・7044 | 25172・248・18689 | 0 |
| T+F | a | 572・2704 | 0 | 2440・0・1208 | 144・10365 | 572・2379・6047・87308 | 20050・286・15342 | 0 |
| T+F | b | 4・198 | 0 | 73・0・160 | 2・11337 | 4・189・293・302 | 198・2・147 | 0 |
| T+F | b | 844・118 | 0 | 3835・0・262 | 207・11129 | 844・97・444・1254 | 10099・426・9162 | 0 |
| T+F | b | 596・169 | 0 | 2535・0・788 | 150・7863 | 596・156・444・477 | 30247・298・22457 | 0 |
| T+F | b | 692・169 | 0 | 2941・0・810 | 173・7862 | 692・156・457・478 | 35119・346・26075 | 0 |
| T+F | b | 700・169 | 0 | 2975・0・794 | 175・7455 | 700・156・447・474 | 35525・350・26376 | 0 |

(a の T+F の最後の行は場面の終わりの mark の少し外。静止の window は全て 0。)

## 事実だけ

- (b) では GPU の run の平均が約 2.5 ms から約 0.16 ms に下がり、Files の frame_ms は約 15.6→7.6、Terminal+Files は後半の window で約 24→15.5。(a) でも P9p は P0 と出ているのに gpu 平均は 2〜3 ms。原因の解析はしていない。判定は Q1。
- 1 回ずつの実行（retry 無し）。場面の窓の行番号は marks-{a,b}.txt。
