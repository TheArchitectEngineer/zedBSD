<!-- awesome-plan project=zedbsd record=ws134-p004 -->
# ws134-p004: システムモニターの操作（M3a）

Status: cleared（q660、2026-10-04。Q1 判定）
Disposition: normal
Parent: [WS134](../ws.md)
設計: [design.md](../design.md) §3.9・§3.10・§5
依存: p003（同じ source に重ねる。p003 の uncleared の直しとは独立に進められる）

## 範囲

tap / click で plate をカードとして前へ（外の tap で戻す）、長押し・右 click で固定、1 本指の swipe・Shift+wheel・←→ で時間の範囲、
pinch（開く: 指の下の plate の detail、閉じる: overview）、2 本指の tap（detail と overview の交互）、状態コアの drag で少し回す（離すと戻る）、
Tab・Shift+Tab・Enter・Space・Esc・P の key の操作。pointer の位置で視差（p003 から）。`--calm`（p002 で入った）。

## 今の状態（2026-10-03、commit 1227ff647）

- `userland/desktop/monitor/app.h`: `struct sm_focus`（カードの plate・出る進み・pin・key の plate）、`struct sm_touch`（libkeiland の
  `keiland_gesture`、指 5 本の id と位置、2 本指の tap の記録、pinch の済み、drag と core の drag、pointer の押下、`core_turn`）、
  `sm_app` の `focus`・`touch`、`interact.c` の prototype（`sm_interact_open/close/event/tick`、`sm_plate_name`、`sm_set_range`）。
- `userland/desktop/monitor/interact.c`（新）: 上の範囲の全ての入力の解釈と log（`ZMON CARD open|close|pin`、`ZMON VIEW detail|overview`、
  `ZMON FOCUS`、`ZMON CORE turn=`、range は `sm_set_range` の `ZMON RANGE`）。gesture の時刻は `kui_clock_us()`（kui の `arrival_us` と同じ時計）。
  2 本指の tap は libkeiland に無いので app が判定（2 本目から 300 ms 以内に全部離す、8 px 以上動かない、pinch が働いていない）。
- 確かめ: `make ZEDBSD_CONFIG=plan/ws134/tests/config-amd64-monitor.mk BUILD=build/p2-p002-img build/p2-p002-img/bin/monitor` が
  `-Werror` で通る（`interact.c` は Makefile に未登録なので link されていない）。`interact.c` は zedBSD の clang で
  `-Wall -Wextra -Werror -fsyntax-only` が通る。style-check・Linux/FreeBSD の build・host 試験・QEMU は未実施。

## 再開の条件（残り）

1. `main.c`: `main_set_range` を `sm_set_range`（非 static）に改名して prototype を消す。`main_input` は RESIZE・CLOSE・Ctrl+Q だけを
   自分で扱い、他の event（←→ の key を含む）を `sm_interact_event(app, &event, main_now(app))` へ渡す（←→ の重複を消す）。
   `main_open` で `sm_interact_open`、`main_close` で `sm_interact_close`。loop で `sm_interact_tick` が 1 を返す間、または指・pointer が
   押されている間は `app->dirty = 1`（固定の時計でも long press が時刻で出るように）。
2. `scene.c`: カードの描画。overlay の暗さ 0.4×e、box は plate の箱から中央の 72%×64% へ補間（e = 1-(1-p)^3）、title・値・「Pinned」の chip・
   その plate の系列の chart（下から上がる）。CORES は core ごとの棒、EVENTS は一覧、STATE は rule ごとの level。key の plate には ice の縁。
3. `space.c`: 状態コアの方位に `app->touch.core_turn` を足す。
4. Makefile・Makefile.linux・Makefile.freebsd に `interact.c`。Linux と FreeBSD の `-fsyntax-only`、`style-check.py`。
5. 試験: `plan/ws134/tests/config-amd64-monitor.mk` に `CONFIG_INPUT_TEST_INJECT := y` と `touchinject`（試験用の image だけ）。
   `plan/ws134/tests/monitor-p004.sh`: touchinject の script（`size 1280 800` を出力の pixel に合わせ、触る前に `wait 2600`）で tap・長押し・
   swipe・pinch・2 本指の tap、guest.sh で key、`ZMON CARD/RANGE/VIEW/FOCUS/CORE` の log と PNG（card.png・pinned.png・overview.png）で判定。
6. build（warning 0）→ commit → Q1 に merge 依頼 → T1 に `monitor-p004.sh` を依頼。

## stub の項目

p002・p003 と同じ（design.md §1.5 の表のまま）。画面の値は全て sim か replay で、本物は hostname・CPU の数・uptime だけ。
この Phase は入力だけで、stub の項目は増減しない。

## 再開（q660、P2 generation8、2026-10-04）

再開の条件 1〜6 を実装した（commit a37e407、base 97e1ebe）。

- `interact.c`: 前の世代の下書きを規約に合わせて書き直し、review で見つけた 4 点を直した。(a) swipe の判定が前の接触の指の
  位置（slot に残る）も見ていた → 接触の始めに記録を消し、1 本指の lift の dx・dy だけで判定。(b) Shift+Tab を何も無い所から押すと
  最後の 1 つ前（lanes）へ行った → 最後（events）へ。(c) pointer の長押しが `--clock=fixed` の時計では起きない（止まった時計で
  測っていた）→ 本当の時計（`kui_clock_us`）で測る。(d) card が出ている時に card の上の tap でも閉じた → card の外の tap だけ閉じる。
  2 本指の tap に「2 本目が 1 本目から 250 ms 以内」（design.md §3.10）を足した。swipe は plate の上で始まった 1 本指の drag だけ
  （状態コアの上は回す操作）。Esc は `ZMON VIEW overview`（固定も外す）。log: `ZMON CARD open|close|pin=|shown`、`ZMON VIEW detail|overview`、
  `ZMON FOCUS`、`ZMON CORE turn= via=touch|pointer`、`ZMON RANGE`。公開の `sm_card_box`（card の箱の補間、tap の当たりと描画で共有）。
- `main.c`: `main_set_range` → 公開の `sm_set_range`。`main_input` は RESIZE・CLOSE・Ctrl+Q だけを扱い、他（←→ を含む）は
  `sm_interact_event` へ。`sm_interact_open/close`。loop で `sm_interact_tick` の答え（指・ボタンが下りている、card・コアが動いている）
  を `input_active` にして `dirty` を立て、`main_timeout` は 16 ms 以下に（固定の時計でも長押しが時刻で出る）。試験が触る場所を知るため
  `ZMON PLATE name= x= y= width= height=` を READY の前と resize の後に出す。
- `scene.c`: `build_keyboard`（key の plate の周りに ice の 2 px の縁）、`build_card`（暗さ 0.4×e の overlay、plate の箱から中央の 72%×64% へ
  e = 1-(1-p)^3 で補間、pin の時は縁が ice と「Pinned」の chip、題・範囲の名前・値・注記、chart は p の後ろ 2/3 で下から伸びる。CPU・GPU・
  Memory は系列の graph、Network は RX・TX、Disk・Latency は Read・Write、CPU cores は core ごとの棒（85% 超はアンバー）、System state は
  rule ごとの level、Events は新しい順の一覧）。出きった時に 1 回 `ZMON CARD shown plate= pinned= value=`。
- `space.c`: 状態コアの方位に `app->touch.core_turn`。
- Makefile・Makefile.linux・Makefile.freebsd に `interact.c`。
- 試験: `tests/host/interact-test.c`（新、`run.sh` に追加。interact.c を libkeiland の gesture.c・motion.c と試験の時計で動かす: tap・card の上と
  外の tap・長押し・固定の card と外の tap・Esc・右 click・左右の swipe・コアの drag（範囲は変わらない、戻る）・pinch の開閉（範囲は変わらない）・
  2 本指の tap 2 回と 400 ms の押し続け・Tab/Shift+Tab/Enter/←→・pointer のコアの drag・click・pointer の長押し、42 の確かめ）。
  `tests/monitor-p004.sh`（新、guest の 8 段: tap → card.png、外の tap、長押し → 固定・外の tap で閉じない・pinned.png・Esc、swipe → `RANGE 15 min`
  で card なし、pinch の開閉 → `VIEW detail/overview` で RANGE 不変・overview.png、2 本指の tap 2 回、key（Tab Tab Enter P Esc ←）→ keys.png、
  コアの drag → `CORE turn= via=touch`）。`config-amd64-monitor.mk` に `CONFIG_INPUT_TEST_INJECT := y` と `touchinject`（試験の image だけ）。
  `tests/host/preview.c` に `PREVIEW_CARD`・`PREVIEW_KEYBOARD`。

### 確かめ（host、2026-10-04）

- build: `make ZEDBSD_CONFIG=plan/ws134/tests/config-amd64-monitor.mk BUILD=build/p2-q656 build/p2-q656/bin/monitor`（zedBSD の clang、`-Werror`）
  exit 0、warning 0。Linux: `keiland-linux.mk` の flag（gcc `-std=gnu17 -Wall -Wextra -Werror`）で monitor の 11 file の object を作れた。
  FreeBSD: `keiland-freebsd.mk` の flag で host の clang の `-fsyntax-only` 11 file（FreeBSD の sysroot ではない）。
- `plan/tools/style-check.py userland/desktop/monitor/*.c plan/ws134/tests/host/interact-test.c` 違反 0。
- `plan/ws134/tests/host/run.sh build/p2-q660-host` → `monitor-host: PASS`・`monitor-interact: PASS`（42 の確かめ全て ok）。
- host の preview（critical の replay、130 s、1280x800）: CPU の card（pin、key の縁が Memory）、CPU cores の card（8 本の棒）、System state の card
  （6 rule、CPU と Disk latency が Critical）、Network の card の途中（p=0.5）。`build/p2-q660-preview/`（worktree p2、git の外）。
- QEMU: 未実施（T1/T2 に `monitor-p004.sh` を依頼する）。実機: 未実施。


## 結果（Q1、2026-10-04、T1-063、QEMU Venus KVM、main 3333344）

cleared。`monitor-p004: PASS`: tap・外の tap・長押しの pin・Esc・左 swipe（RANGE 15 min）・pinch（detail↔overview）・2 本指の tap（graphics）・key（FOCUS gpu・Enter・P・Esc・Left で 5 min）・コアの drag、ERROR なし。card.png・pinned.png・overview.png。fps は判定していない（p003 の件、実機で）。証拠 worktrees/t1/build/t1-063/。
