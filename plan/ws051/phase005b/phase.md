<!-- awesome-plan project=zedbsd record=ws051-p005b -->
# ws051-p005b: IRQ_HPD の retrain と scanout 中の抜けの停止（M4 の後半）

Parent: [WS051](../ws.md)
Status: in-progress / 実機待ち（2026-10-13 P3 q956: 範囲 2（scanout 中の抜け、案 (b)）を実装・host・build（warning 0）。範囲 1・2 とも 5330 の実機の確かめ待ち、QEMU には TC の port が無いので T1 には頼まない。S0ix はベータ3）（2026-10-08 q902 P1 の照合: 範囲 1 の retrain は実装・host 済み、5330 の IRQ_HPD は未。範囲 2 は Q1 が案 (b) に決定、5330 で抜き差しを試せる時に実装（未着手）。S0ix はベータ3）（旧: in-progress（2026-10-08 P1 q877: retrain を実装・host 試験・build warning 0。scanout 中の抜けの停止は案だけ、S0ix の口はベータ3））
Disposition: normal
Queue: q877（P1、2026-10-08 Q1 の承認の 2 番、「host で進められる分。S0ix の口（WS052）はベータ3 なので除く」）
依存: p004b（外部 DP の resident の出力）、p005a（長い・短い pulse、2 秒の猶予の log）。設計: [design.md](../design.md) §6 の hotplug・§12 の M4・M10

## 範囲

1. IRQ_HPD（短い pulse）と長い pulse の後の link の確かめと retrain（Linux の `intel_dp_needs_link_retrain`・`intel_dp_retrain_link`）。今は `hotplug.c` の `i915_hpd_intel_dp_retrain_link` が「unported」の log だけ。
2. scanout 中の抜け（2 秒の猶予の後もまだ DP-alt の partner が居ない）で pipe を止め PHY を手放す。
3. S0ix の口（M10）: **範囲の外**（WS052、ベータ3）。

## 実装（2026-10-08 P1、範囲 1）

- `dp-ext.c`: `drv_i915_dp_ext_link_needs_retrain(env, lane_count)`。sink の link status（DPCD 0x202 から 6 byte）を読み、inter-lane alignment（0x204 bit 0）が無いか、link の lane のどれかの clock recovery・equalisation・symbol lock（lane ごとの 4 bit の下 3 bit）が欠けたら 1。読めない status と 1〜4 の外の lane 数は 0（Linux も読めた status でだけ retrain する）。
- `dp-ext-kern.c`:
  - `drv_i915_dp_ext_link_check(display, port)`: その port の外部 DP が resident の出力で点いている時（`output.kind == DP_EXT`・port が同じ・`window.display_up`）だけ、port の lock の下で link を 1 つ持って status を読む。要れば log `DP-ext TCn: the lit link (xN) lost its alignment or a lane's lock: it is trained again`。
  - `drv_i915_dp_ext_pulse`: 短い pulse を扱えた後に link を確かめ、retrain が要れば 0 を返す。detection（`i915_ddi_hotplug`）に回し、そこで retrain する（Linux の `intel_dp_short_pulse` が `intel_dp_needs_link_retrain` で false を返す形）。
- `hotplug.c` の `i915_hpd_intel_dp_retrain_link`: 外部 DP の port なら `drv_i915_dp_ext_link_check`。要れば `drv_i915_present_retrain_request`。それ以外の DP は今までどおり retrain しない（log）。
- `present.c`・`present.h`・`internal.h`: `window.retrain`。hotplug の側が device の IRQ lock の下で立て、worker が次の frame の `drv_i915_present_relight_begin` で取る。window を出て resident の出力を点け直す（`relight`、buffer と絵は保つ）。点け直しは link training を初めからやり直す（training の fallback を含む、p004b）。Linux の retrain は pipe を止めずに link だけを train し直すが、ここでは resident の run の点け直しで代える（違いは一瞬の黒）。

## 判断の点（範囲 2、案を記録、Q1 へ）

- 2 秒後もまだ partner が居ない時（`i915_hpd_tc_link_reset_work`、今は log だけ）、その port の外部 DP が resident の出力として点いていれば、window を出て出力を止める。案: `window.retrain` と同じ形の `window.unplugged` を立て、worker が次の frame で relight なしに window を出る（hold の終わりと同じ停止の道。PHY は出力の disable で link が 0 になり返る）。ただし、その後の present は firmware の出力（その TC）を点け直そうとして失敗し、「presentation fails from here on」になる（present.c の XXX）。Keiland が別の display を claim するまで画面が無い。
  - 選択肢: (a) 上の形で止めるだけ（driver は出力先を変えない、ユーザーの決定 2・3 のとおり）。(b) 止めた後に gop_output を「無し」にし、Keiland の claim を待つ（present は ENXIO）。
  - 推奨は (b)。決定 3（driver は切り替えず、事象を Keiland へ）に沿い、失敗の連鎖を避けられる。実装は present の失敗の道に触れるので、5330 で抜き差しを試せる時に。今回は実装しない。
  - **決定（2026-10-08 Q1）**: 案 (b) を採用。5330 で抜き差しを試せる時に実装する。

## 確認

- host: `sh plan/ws051/tests/host-dpext.sh` → 96 checks、0 failures（retrain の 13 件を追加: 4 lane で全て済み、lane 3 の symbol lock の欠け（4 lane は要る・2 lane は要らない）、lane 1 の clock recovery の欠け（2 lane は要る・1 lane は要らない）、alignment の欠け、読めない status、lane 数 0・5 は読みもしない）。`sh plan/ws051/tests/host-tc.sh` → host-tc 90/0、host-tc-tables 34/0。
- build（warning 0）: `make -j16 BUILD=build/ws051-k ZEDBSD_CONFIG=config/ci/config-amd64.mk build/ws051-k/vmunix`、`I915_TESTS=y I915_TEST_SET=display`・`execution` の vmunix（rc 0）。
- QEMU: 意味が無い（TC の port が無い）。T1 には頼まない。
- 実機（未実施、BUG-256 の後）: 5330 の USB-C の DP の monitor を resident の出力にし、monitor の入力の切り替えや cable の揺すりで IRQ_HPD を起こす。`lost its alignment or a lane's lock` → `the window is left to train the link again` → 画面が戻ることを見る。

## 積み残し

- 範囲 2（上の案、ユーザーか Q1 の決定の後）。S0ix の口（WS052、ベータ3）。
- 2 つ目の出力（ws113-p011 の head）が外部 DP の時の retrain（今は resident の出力だけ）。

## Q1 の判断（2026-10-08）

範囲 2（scanout 中の抜け）: 案 (b)（止めた後に firmware の出力を「無し」にし、Keiland の claim を待つ。present は ENXIO）を採る。ユーザーの決定 3（driver は切り替えず事象を Keiland へ）に沿う技術の判断。present の失敗の道に触れるので、5330 で抜き差しを試せる時に実装する。

## 実装（2026-10-13 P3 q956、範囲 2: scanout 中の抜け、Q1 の案 (b)）

Queue: q956（2026-10-13 Q1。ユーザー「これ以上ベータ2に機能追加しないつもりなので、実装を終わらせましょう」）。

2026-10-08 以後に BUG-268（P2）が「移した出力（Keiland の claim で動かした外部の表示）が抜けたら直ちに窓を出て firmware の出力に戻す」を入れた（`drv_i915_present_unplugged`、`window.unplugged`、worker の loop の頭の確かめ、hold の即時の終わり）。残っていたのは **firmware 自身の外部の出力（GOP が点けた Type-C の DP、p004c、または HDMI）** が点いている間に抜けた場合で、今までは何もせず、窓を出た後の点け直しが失敗して `display_failed`（以後ずっと presentation が失敗）になった。

| file | 変更 |
| --- | --- |
| `display/present.c` | `drv_i915_present_unplugged`: 移した出力に加え、firmware 自身の外部の出力（panel でなく none でない）が点いていてその connector が抜けた時も `window.unplugged` を立て worker を起こす（log を分けた）。新しい `drv_i915_present_tc_partner_gone(display, tc_port)`: resident の出力がその TC の外部 DP なら、その connector の抜けとして扱う（connector の切断が見えなかった時の backstop）。窓の終わり（`drv_i915_present_window`）: 抜けた出力が移した物なら今までどおり fail back、firmware 自身の物なら `drv_i915_display_output_lost`、その後 `drv_i915_hpd_topology_touch`。抜けで失敗した run は `display_failed` にしない |
| `display/display.c`・`display.h` | 新しい `drv_i915_display_output_lost`: 案 (b)。driver は別の出力を自分では点けない（ユーザーの決定 3）。その lease の presentation は失敗（`lease_failed`、ENXIO）。**built-in panel の connector があれば、panel を resident の出力（と firmware の出力）にし、点けずに置く**（表示の一覧に panel が出て、Keiland の claim と最初の frame で点く。移した出力の fail back の後と同じ）。panel の connector が無い machine では none（起動時に firmware の出力を駆動できない時と同じ） |
| `display/hotplug.c` | `i915_hpd_tc_link_reset_work`: 2 秒後も partner が居なければ `drv_i915_present_tc_partner_gone`（log の「the output is not stopped here」を外した） |
| `plan/ws051/tests/host-unplug-gop.c`・`host-unplug-gop-stubs.c`・`host-unplug-gop.sh`（新） | host 試験 |

案 (b) からの技術の補い（Q1 に報告）: Q1 の文は「gop_output を『無し』にし Keiland の claim を待つ」。none にすると表示の一覧（`drv_i915_display_info`）が resident の出力の mode を要るので 0 個になり、Keiland は panel も claim できない。そこで panel の connector がある時は「点けない panel」を resident・firmware の出力に置き、none は panel の無い machine だけにした。driver が自分で点けない点、lease の presentation が ENXIO になる点、Keiland の claim を待つ点は (b) のまま。

### 確認（2026-10-13）

- host: `sh plan/ws051/tests/host-unplug-gop.sh build/p3-ws051/unplug-gop` → 11 checks、0 failures（ASan・UBSan）: 別の connector・別の port は印を付けない、firmware の外部の表示の抜けで印と 1 回の wake、抜けの後の partner gone は再び起こさない、partner gone だけでも印、点いていない出力は印を付けない、panel は抜けとしない、panel の connector の無い machine で手放すと none と `lease_failed`、移した出力とは見なさない、none の後は何も印を付けない。
- 回帰: `sh plan/bugs/BUG-268/tests/run-host-unplug.sh` 10/0、`sh plan/ws083/tests/run-host-vcs-worker.sh` PASS、`sh plan/ws051/tests/host-dpext.sh` 96/0。
- build: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p3-ws051-amd64 build/p3-ws051-amd64/vmunix` → warning 0、`amd64 vmunix check: PASS`。
- style-check: present.c・display.c・hotplug.c は変更の前と同じ数（新しい指摘 0）、新しい試験は 0。
- 未実施: 窓の終わりの道（`drv_i915_present_window` の lcd の resident run と組み合わせた分）は host で走らせられず、読みで確かめた。panel を置く道（hotplug の path がある時）も host では none の道だけを試した。実機。

### 実機の確かめ（Q1・ユーザー、5330）

1. USB-C の DP の monitor だけをつないで起動（GOP が TC に出す、p004c）。Keiland の desktop が出たら monitor を抜く。期待: kernel の log に `the DP display (the firmware's output) was unplugged; the window is left and nothing is lit until a display is claimed` → `the firmware's output was unplugged; the built-in panel is the output, not lit until a display is claimed` → `lease … claimed`、eDP に desktop が出る（人が触らずに数秒以内）。`presentation fails from now on` が出ない。
2. 同じ状態から抜いて挿し直す: Keiland が TC を見つけ、claim で TC へ移せる（拡張・mirror の操作）。
3. 範囲 1（IRQ_HPD の retrain）は上の §確認の実機の手順のまま。

### 残り

- 点いていない firmware の出力（Keiland の最初の frame の前、GOP の絵のまま）が抜けた時: 今は何もせず、最初の frame の run が失敗して `display_failed` になりうる（hotplug の道から worker と競わずに手放す仕組みが要る）。ベータ2 の範囲の外として記録。
- panel を置いた時の panel の mode は起動時に読んだ eDP の情報に依る（GOP が TC だけに出し eDP が消えていた起動で読めるかは実機で見る）。
- `plan/bugs/BUG-268/tests/host-unplug.c` の冒頭の注記「the firmware's own output … is not marked」は、firmware の外部の出力が印を付けるようになったので不正確（その check 自体は panel で正しい）。bug の試験は P3 の範囲の外なので Q1 に報告。
