<!-- awesome-plan project=zedbsd record=ws081-p004 -->

# ws081-p004: compositor での touch motion の使用と library の公開

<!-- awesome-plan-current:start -->
Status: cleared（2026-09-29、WS081 の作業用サブエージェント。library の公開、compositor の τ・追従、QEMU の guest 試験・回帰・boot test。滑らかさの数値は QEMU では測れず（下）、実機は未実施）
Disposition: normal
Parent: [WS081](../ws.md)
Queue: main の指示（2026-09-29「p002 の後、時間があれば p004 に進んでよい」、compositor と libkeiland の公開の部分の変更を許可）。Awesome Plan の Queue の item ではない
Resume point: なし（p005 以降は main の指示の後）
<!-- awesome-plan-current:end -->

## 範囲

[design.md](../design.md) §4・§6: library の公開（Makefile・exports.map・keiland.h、`KEILAND_VERSION` 9）、compositor が報告の時刻を Scan Time から作って
wl_touch の時刻にする（§3.7・§4.3）、全接触の報告を motion に入れて device が学ぶ、shell が持つ指（タイトルバーの drag、端のジェスチャー）を resampling した点で
追う（§4.4）。ブラウザ・libbrowser・他の app は触らない（p005 以降）。HAL・toolchain・kernel は変えない。

## 実装

| file | 内容 |
| --- | --- |
| `include/libc/keiland.h` | touch motion の節（p003 の `motion.h` の宣言と説明）を足し、`KEILAND_VERSION` を 9 に。冒頭の説明に「指に付いて動く全 program の共有」を足した。wl_touch の時刻の基準（CLOCK_MONOTONIC の ms の低い 32 bit、Scan Time のある panel は scan の時刻）を約束として書いた |
| `userland/desktop/libkeiland/motion.c`・`Makefile`・`exports.map` | `motion.c` を libkeiland の source に、`keiland_motion_*` を export に。`motion.c` は `<keiland.h>` を include（`motion.h` は削除）。引数の NULL の検査を 1 つずつの if に（`style-extra.py` の joined-check） |
| `userland/desktop/wayland/zwl.h`・`input.c` | `zwl_input_device.frame_time_us`: 報告の SYN_REPORT の evdev の時刻（µs） |
| `userland/desktop/wayland/touch.c` | screen ごとに `keiland_motion_device`、指（slot）ごとに `keiland_motion`（最初の指で作り、以後の指に使い回す）。報告の `MSC_TIMESTAMP` を読み、`keiland_motion_device_time` で τ を作って `report.time`（wl_touch の時刻と、タイトルバーの判定の時刻）に。全指の報告を motion に入れ、離すと `keiland_motion_end` で device が学ぶ。shell が持つ指（`ROUTE_SHELL`、タイトルバーの drag の昇格を含む）は、報告ごとと event loop の各 pass（`zwl_touch_tick`、`zwl_glass_tick` から）に `keiland_motion_point(now, 12 ms)` の点へ pointer を動かし（別の pixel のときだけ）、shell がそれに従う。離すときは最後の報告の点へ動かしてから button を離す。motion が作れなければ今までどおり報告の点。`--log-frames` のときだけ `ZWL TOUCH report …`（点・τ・host の時刻・Scan Time の有無）と `ZWL TOUCH follow …`（追う点と最後の報告の点）を印字 |
| `userland/base/tests/touchinject/main.c`（main の許可の範囲: Scan Time・µs の台本） | 台本の時刻を宣言からの絶対の予定にした（相対の sleep の超過が積もって Scan Time が実時間より約 3% 遅れ、Scan Time の写しが始め直して跳んでいた）。jitter のある `swipe` は、指が一定の速さで動き panel が揺れた時刻に読む形に（前は位置が等分で、指の速さが報告ごとに ±25% 変わっていた）。jitter なしの位置は今までと同じ整数の式（WS079 の期待の file は不変） |
| `plan/ws081/tests/host-motion.c`・`run-motion.sh` | `motion.h` の代わりに `<keiland.h>`。host の試験では `include/libc` の keiland.h だけを置いた dir を include path に（zedBSD の libc の header が host のものを覆わないように） |
| `plan/ws081/tests/p004-guest.sh`（新） | guest 試験 |

## sysroot の写し

`include/libc/keiland.h` を変えたので、p002 と同じ手順（main の許可、[phase002](../phase002/phase.md) の「sysroot の写しの扱い」）で
`build/amd64/sysroot/usr/include/keiland.h` に写して `.zedbsd-sysroot-complete` を touch した。main が merge の後に sysroot を作り直すときは、
`keiland.h`（と p002 の `uapi/input.h`・`uapi/input-inject.h`）が入ることを確かめる。展開・patch は 0 件（build の log）。

## 確認（実行したもの）

| 確認 | 結果 |
| --- | --- |
| `plan/ws081/tests/run-motion.sh`（公開の後の library） | `host-motion: ok (426009 checks)` |
| `make -j16 … build/amd64/bin/wayland build/amd64/dynamic/libkeiland.so`（`config-amd64-pen.mk`、`-Werror`） | rc=0、warning 0、`nm -D libkeiland.so` に `keiland_motion_*` 12 個 |
| `plan/ws079/tests/build-pen-image.sh build/amd64`（新しい compositor・libkeiland・touchinject の image） | rc=0、変えた file の warning 0、toolchain の展開・patch 0 件 |
| `plan/tools/style-check.py`・`plan/ws079/tests/style-extra.py`（touch.c・compositor の input.c・motion.c・keiland.h・touchinject） | 指摘 0 |
| [p004-guest.sh](../tests/p004-guest.sh)（`VENUS_RENDERER=…/build/ws035-sq-venus/install` の guest） | **PASS**: 窓（tablet-probe、320,203）のタイトルバー (470,173) を一本指で 150 ms 待ってから、30 Hz・jitter ±8 ms・Scan Time ありで +300,+150 に drag。`TOUCH title drag`、報告 30 がすべて Scan Time つき、τ ≤ host、τ の歩みが 26〜39 ms（台本の 33.3 ± 8 ms）、追従の点 21 個が一度も戻らない（1 px 以内）、窓が (620,353) に着く。compositor の ERROR/FAILED 0。参考の judder（x、直線からの rms）: 追従 1.85 px、報告を着いた時に描く場合 2.13 px（12 の時点） |
| WS079 の [p012-guest.sh](../../ws079/tests/p012-guest.sh)（同じ guest、新しい compositor） | **PASS**（touchinject -c、二本指の 48 event の完全一致、pen、compositor の seat に `kind=touch`） |
| [p002-guest.sh](../tests/p002-guest.sh)（同じ guest、新しい touchinject） | **PASS**（-c 14/14、-s 15/15、jitter のある 90 Hz の MSC_TIMESTAMP の歩み） |
| `plan/tools/boot-test.sh`（新しい image） | **PASS**、login prompt（`/home/awe/zedBSD-rpi4/build/ws081-shots/ws081-p004-20260929-boot-login.png`）。greeter は QEMU では終わり getty に（今までと同じ） |

guest の drag の log: `/home/awe/zedBSD-rpi4/build/ws081-shots/ws081-p004-20260929-drag-log.txt`（worktree の `build/ws081-p004-guest/` にも）。

## 経過と判断

- 1 回目は wltest の窓で PASS に近かったが、「追従が報告より多い」「追従の judder が小さい」が FAIL。調べると、QEMU の software の Vulkan では compositor の
  1 frame の描画に約 100〜150 ms かかり（`ZWL PERF compose … frame_ms=110〜157`）、その間 event loop が止まるので、pass が少なく不揃いで、報告の処理も
  遅れて束になる。この環境で滑らかさの数値は測れないので、guest 試験は機能の確認に絞り、judder は参考の印字にした。滑らかさの数値は同じ library を覆う
  p003 の host 試験と、実機（p007）で見る。
- 同じ調査で touchinject の 2 つの誤り（Scan Time が実時間から遅れる、jitter で指の速さが揺れる）を見つけて直した（上の表）。
- 2 回目以降、この guest では wltest の窓が開かなくなった（`wltest_window_open errno=6`＝ENOENT）。image の元の compositor でも同じで、今回の変更とは無関係
  （compositor は起動し、socket もある。tablet-probe は開ける）。窓は tablet-probe に替えた。一時的に client が繋がらない（EIO）ことも 1 度あり、compositor の
  起動を `ZWL MODE` の行まで待つようにした。原因は調べていない（Venus の guest の状態によるものと見られる）。
- 追従は event loop の pass ごと（≤10 ms）。表示の frame の時刻（vblank）は compositor に無いので「描き始め」を使う（design §4.4 のとおり）。

## 未実施・制限

- WS079 の `zdesktop-p013-touch.sh`（wltest の窓を使う、二本指の flick・drag の回帰）は、この guest で wltest が開かないため未実施。タイトルバーの一本指の drag は
  p004-guest.sh が覆う。二本指の flick の判定の時刻は τ に変わった（ms の基準は同じ）。
- 実機（touch LCD）での滑らかさ・Scan Time は未確認（p007）。
- Keiland の app（Files 等）と browser は wl_touch を使っておらず、この Phase では変えていない（p005 以降）。

## 残り

- p005（scroller・gesture の library）以降は main の指示の後。
