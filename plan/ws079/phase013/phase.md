<!-- awesome-plan project=zedbsd record=ws079-p013 -->

# ws079-p013: compositor の touch と「あっちにいけ」（窓を z-order の後ろへ）

<!-- awesome-plan-current:start -->
Status: in-progress（1 つ目の区切り: mouse の triple click は QEMU の Venus guest で確認済み。touch の部分は p012 の後）
Disposition: normal
Parent: [WS079](../ws.md)
Queue: main の指示（Kei desktop subagent、2026-09-28）。Awesome Plan の Queue の item ではない
Resume point: 下の「残り」
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

### 残り（resume の条件）

1. touch（p012 の後）: `wl_touch`、touch の接触を `zwl_corner_contact_*` と Home・Wiseview の端へ、浮いたタイトルバーの上の二本指の短い上へのこすりを
   `window_lower()`（同じ関数、via を変える）へ。
2. 実機（mouse の triple click を含む）は未実施。
3. docked の窓の bar の題の triple click は範囲外（浮いたタイトルバーだけ）。
