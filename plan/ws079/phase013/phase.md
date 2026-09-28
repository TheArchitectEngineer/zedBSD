<!-- awesome-plan project=zedbsd record=ws079-p013 -->

# ws079-p013: compositor の touch と「あっちにいけ」（窓を z-order の後ろへ）

<!-- awesome-plan-current:start -->
Status: cleared（2026-09-28、Kei desktop subagent。mouse の triple click と touch（wl_touch、端のジェスチャー、二本指の上への flick、一本指の drag と tap）を QEMU の Venus guest で確認。実機は未実施）
Disposition: normal
Parent: [WS079](../ws.md)
Queue: main の指示（Kei desktop subagent、2026-09-28）。Awesome Plan の Queue の item ではない
Resume point: なし（実機の確認は touch LCD の到着後。下の 2 つ目の区切りの「判断・制限」）
<!-- awesome-plan-current:end -->

## 範囲

- ユーザー（2026-09-28）:「ウィンドウのフローティングタイトルバーを二本指でタッチする（叩く）と、Zオーダーが後ろに回って奥に行き、次のウィンドウが表示されるようにしたいです。」
  →「2本指で軽く短く上方向こすって、「あっちにいけ」というジェスチャー」→「とりあえず3回クリックで実装しつつ、マルチタッチが実現したら実装しましょう。」
  →「では、マウスで3回クリックすると同じ動作にしましょう。」
- main の注意: タイトルバーの double click は今ドッキング（ws035-p062）なので、triple click を見分けるために double click の動作を
  click の間隔の上限（400 ms）まで待たせる。mouse の部分は touch を待たずに先に入れてよい。
- この phase の全体: client への `wl_touch`、touch の接触を端のジェスチャー（p010 の `zwl_corner_contact_*`、Home・Wiseview）へ、
  浮いたタイトルバーの上の二本指の短い上へのこすり（「あっちにいけ」）。依存は p012（kernel の multitouch）と p003。
- HAL の変更なし。

## 1 つ目の区切り: mouse の triple click（2026-09-28、Kei desktop subagent）

### 実装

| file | 変更 |
| --- | --- |
| `userland/desktop/wayland/shell.c` | 浮いたタイトルバーの押しを `title_clicks()` で数える（同じ窓の上で前の押しから 400 ms（`DOUBLE_CLICK_MS`）未満なら続き、そうでなければ 1 から）。2 回目は **すぐには dock しない**: `server->dock_waiting` に窓を置き、期限を 2 回目の押しの時刻 + 400 ms に（log `GLASS dock waiting`）。3 回目が期限の前に来たら待ちを取り消し、`window_lower()`（log `GLASS lower client=C surface=S via=triple-click next=C:S focus=C:S`）。`zwl_glass_tick` の `dock_when_due()` が期限を過ぎた待ちを dock する（log `GLASS double-click surface=S waited_ms=N` の後に従来の `GLASS dock … via=double-click`）。待ちの間に窓が消えた・dock された・最小化・別の desktop へ移ったなら dock しない（`GLASS dock dropped`）。`window_lower()`: 他の mapped な surface の最小の `map_order` の下へ置く（1 の下に空きが無ければ他を全部 1 つ上げる。相対の順は保つ）。`zwl_top_window` を `front_surface` にして `zwl_seat_focus`（keyboard の focus が次の窓へ）。system bar の題の double click（undock）は今どおりすぐ（`double_click()` は `click_count` を戻すだけ足した） |
| `userland/desktop/wayland/zwl.h` | server に `click_count`・`dock_waiting`・`dock_due_ms` |
| `userland/desktop/wayland/objects.c` | 破棄された窓を `dock_waiting` から外す |
| `plan/ws079/tests/zdesktop-p013.sh`（新） | guest の試験（下） |

1 回目の押しは今どおり move を始め（動かさずに離せば位置は変わらない）、2 回目・3 回目は move を始めない（2 回目は従来も同じ）。
dock は 2 回目の押しから 400 ms 後（測定 400〜428 ms）に始まるので、double click の dock はその分遅れる（main の注意のとおり）。

### 確認（QEMU の Venus guest だけ。実機は未実施）

build: 試験の image `plan/ws079/tests/config-amd64-pen.mk`（worktree の `build/amd64`、`plan/ws079/tests/build-pen-image.sh`）、
`make -j16 … build/amd64/bin/wayland` は rc=0・warning 0（`-Wall -Wextra -Werror`）。`plan/tools/style-check.py`（shell.c・objects.c・zwl.h）の指摘 0、`git diff --check` 問題なし。
clang-format は host に無く未実施。

[zdesktop-p013.sh](../tests/zdesktop-p013.sh)（`pen-guest.sh start build/amd64/hdd-image.img` の guest、1280x800、pointer は `qmp-pointer.py`、判定は compositor の log と VNC の画素）: **PASS**（最終の run）。
3 つの wltest の窓 a（f4f7fc）・b（c8d8ec）・c（e8c8b0）を、それぞれ一番上の間にタイトルバーの drag で階段に置いてから:

| # | 確かめたこと | 結果 |
| --- | --- | --- |
| 0 | 3 つの drag が pointer の動いた所に着く（`GLASS moved … x=100 y=150` 等）、drag の間に dock・lower が無い、画素で a < b < c | PASS |
| 1 | c のタイトルバーの triple click（60 ms 間隔）→ `lower client=3 surface=6 via=triple-click next=2:6 focus=2:6`、c は dock しない、b と c の重なりが b の色、c の見えている所は c の色 | PASS |
| 2 | b の triple click → `next=1:6 focus=1:6`（a が前に）、a と b の重なりが a、b と c の重なりが c | PASS |
| 3 | c のタイトルバーの single click → c が最前面（画素）、lower・dock waiting・dock は増えない | PASS |
| 4 | c の double click → `dock waiting` → `double-click … waited_ms=400`（run によって 423・428）→ `dock … via=double-click`、この順。lower は増えない。dock 後の画面（出力の下の角・bar の下）が c。bar の題の double click で `undock … x=560 y=350` | PASS |
| 5 | c のタイトルバーの drag（−60,+40）→ `moved … x=500 y=390`、lower は増えない | PASS |

回帰（同じ guest、新しい compositor）: `plan/ws035/tests/zdesktop-p062.sh`（ドッキングの double click・drag・pull・button）PASS、
`plan/ws035/tests/zdesktop-p076.sh`（popup・move・resize・dock・minimize）PASS。

画面（`build/ws035-shots/`、main の checkout の共有の dir。worktree の `build/ws079-p013-shots/` にも）:
`ws079-p013-20260928-stack.png`・`-c-lowered.png`・`-b-lowered.png`・`-c-raised.png`・`-c-docked.png`・`-c-moved.png`、log の抜粋 `-log.txt`。

試験の run の経過: run1 は試験の誤り（wltest の `--frames` の上限 3600 を超えて窓が出なかった）。run2 は PASS したが、surface の番号が client ごとの
番号で 3 つとも 6 だったため、lower の log を `client:surface` にして run3・run4 を PASS。

### 残り（1 つ目の区切りの時点。1 の touch は下の 2 つ目の区切りで済んだ）

1. touch（p012 の後。p012 は 2026-09-28 に kernel の protocol B と注入の `touchinject` まで入った: guest で `touchinject` の台本が
   touch screen の evdev（`ABS_MT_*`・`BTN_TOUCH`・`ABS_X/Y`）を出し、compositor は今それを絶対 pointer として開く）: `wl_touch`、touch の接触を `zwl_corner_contact_*` と Home・Wiseview の端へ、浮いたタイトルバーの上の二本指の短い上へのこすりを
   `window_lower()`（同じ関数、via を変える）へ。
2. 実機（mouse の triple click を含む）は未実施。
3. docked の窓の bar の題の triple click は範囲外（浮いたタイトルバーだけ）。

## 2 つ目の区切り: touch（2026-09-28、Kei desktop subagent）

### 実装

| file | 変更 |
| --- | --- |
| `userland/desktop/wayland/touch.c`・`touch.h`（新） | touch screen（multitouch protocol B）。report ごとに、離れた指 → 動いた指 → タイトルバーの指の判定 → 新しい指の順に適用し、何か聞いた client に `wl_touch.frame`。指の行き先は触れたときに決め、離れるまで変えない。浮いたタイトルバーの上の最初の指は 150 ms（`TITLE_PAIR_MS`）二本目を待つ（`title held`）。同じタイトルバーに二本目が来れば組（`title pair`）。組の最初の指が離れたとき、二本とも 24 px（`TITLE_FLICK_DISTANCE`）以上上へ、横より上が大きく、二本目が触れてから 250 ms（`TITLE_FLICK_MS`）以内なら `zwl_glass_lower(…, "two-finger-flick")`（triple click と同じ）。250 ms を過ぎる（`reason=slow`）・24 px 下へ（`down`）・24 px 以上で横が上より大きい（`sideways`）・足りない（`short`）は何もしない（離れるまで指は無視）。一本だけで 150 ms 以内に離れれば tap（押しと離しを shell へ。click と同じく窓が前に）、150 ms を過ぎれば drag（触れた点の押しを shell へ、その後の動きを追いつかせる）。それ以外の最初の指は pointer の左 button として shell（`zwl_seat_button_shell`・`zwl_seat_motion_shell`、`server->shell_source = ZWL_CONTACT_TOUCH`）へ。端のジェスチャー・system bar・タイトルバーの button・App Home・Wiseview は mouse と同じに取る。shell が取らなければ指の下の surface へ: `wl_touch` を持つ client には down/motion/up（指ごとの surface、surface-local の座標、id は screen×16+slot）、持たない client には pointer の左 button（motion → button、指が pointer を動かす。以前の絶対 pointer の扱いを保つ fallback）。指が pointer かタイトルバーを持つ間の他の指は `wl_touch` の client にだけ行く。compositor が指を取ったら（shell が取った・タイトルバーの drag・flick の成立）、`wl_touch` で聞いている client の指を全部 `wl_touch.cancel`（`TOUCH cancel reason=…`）、以後その指は無視。surface が消えたら指は無視、touch screen が消えたら shell の押しを離し client は cancel |
| `input.c` | `ABS_MT_SLOT`・`ABS_MT_TRACKING_ID`・`ABS_MT_POSITION_X/Y` を持つ node は touch screen（`kind=touch`、絶対 pointer にしない）。SYN_REPORT で `zwl_touch_frame`。seat の capability に touch（4）と fallback の pointer（1） |
| `seat.c`・`protocol.c`・`objects.c`・`zwl.h` | `wl_seat.get_touch`（`ZWL_TOUCH`）、`wl_touch.release`、SEAT の log に `touch=`、surface の破棄で `zwl_touch_object_gone`、`zwl_input_device.touch`、`server->shell_source`（`enum zwl_contact_source` を前へ移した）、冒頭の comment |
| `shell.c` | `zwl_glass_title_at()`（押しが浮いたタイトルバーに届く窓。greeter・lock・DnD・popup・Wiseview・Home・menu・system bar・左右端・下端は NULL）、`zwl_glass_lower()`（click の run と dock の待ちを消して `window_lower`）、`zwl_glass_tick` から `zwl_touch_tick`（待ちの期限） |
| `corner.c` | contact の source を `server->shell_source` から（`CORNER press source=touch`）、motion は同じ source のときだけ、release の来ない contact の片付けを pen 以外に |
| `userland/desktop/libwayland/touch-protocol.c`（新）・`Makefile`・`protocol.c`・`include/libc/wayland/wayland-client-protocol.h`・`API-PROVENANCE.md`・`README.md` | `wl_touch` v5 の client の表（release、down `uuoiff`・up `uui`・motion `uiff`・frame・cancel。shape/orientation（v6）は無し）、`wl_seat_get_touch`・`wl_touch_add_listener`・release・destroy・user data・version。event は generic dispatch |
| `userland/base/tests/tablet-probe/main.c` | `--touch`（wl_touch を bind して down/motion/up/frame/cancel を `TABLETPROBE touch …` で記録、緑の点）と `--color=RRGGBB` |
| `plan/ws079/tests/zdesktop-p013-touch.sh`（新）・`p012-guest.sh` | 試験（下）。p012 の段 4 は touch screen が `kind=touch` で seat に入ることを見るように直した |

### 確認（QEMU の Venus guest だけ。実機は未実施）

build（worktree の `build/amd64`、`ZEDBSD_CONFIG=plan/ws079/tests/config-amd64-pen.mk`、`-Wall -Wextra -Werror`）: `plan/ws079/tests/build-pen-image.sh build/amd64`（image、exit 0）と
`make -j16 … build/amd64/bin/wayland build/amd64/bin/tablet-probe build/amd64/dynamic/libwayland-client.so` は rc=0・warning 0。sysroot は共有の写しに変えた header（`wayland/wayland-client-protocol.h`）を手で写した。
`plan/tools/style-check.py`（touch.c・touch.h・touch-protocol.c・input.c・seat.c・shell.c・objects.c・protocol.c・tablet-probe）の指摘 0（corner.c の 3 件は HEAD の既存のもの）。whitespace の検査（diff --check）問題なし。clang-format は host に無く未実施。

[zdesktop-p013-touch.sh](../tests/zdesktop-p013-touch.sh)（`pen-guest.sh start build/amd64/hdd-image.img`、1280x800、touchinject の台本は画面の画素、判定は compositor・probe の log と VNC の画素）: **PASS**（run4、全段）。

| # | 確かめたこと | 結果 |
| --- | --- | --- |
| 1 | `tablet-probe --touch`: seat capabilities=7、二本の指の down（`id=0/1 surface=1`、局所座標 100,100 と 400,300）・motion・up・frame 9・cancel 0、compositor の `TOUCH down client=… contact=0/1`、点の画素 | PASS |
| 1b | wl_touch の無い client（`tablet-probe --pointer`）: `TOUCH pointer …`、BTN_LEFT 1 → motion → 0、wl_touch の down 無し、点の画素 | PASS |
| 2 | probe の上の指の後、下端からの二本目で `TOUCH shell contact=1`・`TOUCH cancel client=… reason=shell`・`WISEVIEW opening`、probe に cancel 1、cancel の後の motion・up は 0。tap で `WISEVIEW closed` | PASS |
| 3 | 左上の角 → `HOME open via=drag`、Home の下端 → `HOME bottom swipe`・`HOME close via=bottom`（Wiseview 0）、右上 → `CORNER press source=touch`・`commit via=distance`・`notes launch`（代役、全画面の画素）、デスクトップの下端 → Wiseview | PASS |
| 4a | c のタイトルバーで二本指の上への flick（40 px、4×25 ms）→ `title pair`・`flick window=3:6 dx=0,0 dy=-40,-40`・`GLASS lower … via=two-finger-flick next=2:6 focus=2:6`、dock・move 無し、画素で b が前 | PASS |
| 4b | 二本指の遅い drag（40 px を 600 ms）→ `flick refused reason=slow`、lower・move 無し | PASS |
| 4c | 下への flick → `reason=down`、横 → `reason=sideways`、lower・move 無し | PASS |
| 4d | 一本指の drag（−60,+40 を 240 ms）→ `title held`・`title drag`・`GLASS moved … x=240 y=290`（指の動いただけ） | PASS |
| 4e | 後ろの c のタイトルバーの一本指の tap → `title tap`、c が前（画素） | PASS |

回帰（同じ guest、新しい compositor）: `plan/ws079/tests/zdesktop-p013.sh`（mouse の triple click）PASS、`plan/ws079/tests/zdesktop-p010.sh`（右上のスワイプと端のジェスチャー）PASS、
`plan/ws079/tests/p012-guest.sh`（touchinject の拒否 14/14、二本指の evdev 48 event、pen、compositor に `kind=touch`）PASS。

試験の run の経過: run1 は画素の検査点の誤り 2 件（二本目の指の点に cursor の矢印が重なる、b の影）。run3 は wl_touch の無い client に押しの前の motion が無く、probe の点が古い位置に描かれた → 押しの前に `wl_pointer.motion` を送るように直した（実際の mouse と同じ順）。run4 で全段 PASS。

画面（`build/ws035-shots/`、main の checkout の共有の dir。worktree の `build/ws079-p013-touch/` にも）: `ws079-p013-20260928-touch-probe.png`・`-pointer.png`・`-wiseview.png`・`-home.png`・`-notes.png`・`-stack.png`・`-flick-lowered.png`・`-slow.png`・`-down.png`・`-drag.png`・`-tap.png`、log `-log.txt`・`-edges-log.txt`・`-probe-log.txt`・`-pointer-log.txt`・`-errors.txt`（空）。

### 判断・制限

- 閾値は依頼の例のまま（二本とも 24 px 以上上、250 ms 以内。二本目は一本目から 150 ms 以内に同じタイトルバーへ）。判定は最初の指が離れたとき（「短く」を接触の短さとした）。遅い drag は 250 ms を過ぎた時点で取り消す。
- 一本指のタイトルバーの drag と tap は 150 ms 遅れる（二本目を待つため）。drag は待ちの後に指の今の位置へ追いつく。tap の 2 回・3 回は click と同じく dock・lower になる。
- 指が pointer の左 button として shell を通るので、cursor は指の位置へ跳ぶ（隠さない）。
- client が touch の down の serial で `xdg_toplevel.move` を頼んでも、BTN_LEFT が押されていないので compositor は断る（未対応）。
- `wl_touch` の shape・orientation（v6）、touch での cursor の隠し、複数の出力への写しは範囲外。
- 実機（10 インチの touch LCD）は未着で未実施。main の sysroot には次の sysroot の build で header が入る。
