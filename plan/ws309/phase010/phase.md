<!-- awesome-plan project=zedbsd record=ws309-p010 -->

# ws309-p010 blur の thread（T9）とその都度の decode（A1）: 壁紙・出力の大きさ・回転

- 親: [WS309](../ws.md)、設計 [design.md](../design.md) §2（T9・A1）・§3.2（lane）・§3.5（その都度）・§4.7。Queue: q1662-i07（P1）。
- 状態: in-progress（2026-10-29 14:0x P1。T9・A1 と会話の中の壁紙・landscape・出力の大きさの変化まで実装、QEMU `grave-guest.sh` PASS。残りは下の「残り」）。
- 依存: p009（cleared）。

## 今（基点 166958a7a、branch `agent/p1-ws309g`）

- 起動: `kwl_glass_prefetch`（main.c、READY の前）が thread で file を読み decode、`wallpaper_create` → `wallpaper_fill` が prefetch を取り main で描く（起動は原則 1 の例外）。
- 会話の中の壁紙（`kwl_glass_wallpaper_begin`）: glass.c の `glass_loader`（その都度の thread、`loader_run`）が read・decode・出力の大きさの rows と blur を作る。
  `kwl_glass_wallpaper_poll`（settings.c の `kwl_settings_tick` から毎 pass、settings が無い greeter では呼ばれない）が join して copy（p009 の `wallpaper_target`）。
  結果の大きさが今の出力と違えば main で `wallpaper_draw`（描き直しと blur が main）。
- landscape: loader が空いていれば loader に、走っていれば main で `wallpaper_draw`。
- 出力の大きさの変化（`kwl_glass_resize`、output-switch.c）: main で image を作り直し `wallpaper_fill`（file の read・decode・描画・blur が全て main）。
- 回転（`kwl_glass_size_prepare`、park の中）: main で read・decode・描画（p012 の T3 の手順の中）。

## 実装の形（調べの時の案。済みは下）

1. T9 = glass.c の lane `glass_blur`（`kwl_lane_start(&glass_blur, "blur", 1, server->wake[1])`、glass の open の後・glyph worker の後、close で `kwl_lane_stop`）。
   job `struct glass_blur_job`（lane の job、decode 済みの picture（data NULL は landscape、job が持つ）、幅・高さ、rows・blurred、error、settings に答えるか、resize の印、開始の ms、path）。
   run（T9）: rows・blurred を確保して `wallpaper_render`（picture は消さない: 大きさが違えば描き直すため）。
   done（main）: glass が無ければ捨てる（答える物は ENODEV）。job の大きさ ≠ 今の出力なら同じ picture で submit し直す。そうでなければ image を用意して（大きさが同じで
   `kwl_compose_host_writable` なら同じ image、そうでなければ `glass_image_renew` で新しい大きさの image、古い物は墓）rows を写し、`KWL GLASS wallpaper path=... ms=... images=...`
   （resize は `KWL GLASS resize ...`）。答える物は結果を `glass_answer` に置く。
2. A1 = 今の `glass_loader`（その都度の thread）を decode だけにする（`loader_run` から rows と blur を外す）。join と T9 への submit は毎 pass の新しい `kwl_glass_blur_take`
   （main.c の `kwl_glass_glyphs_poll` の後に呼ぶ: greeter でも回る、§3.3 の順「font → 壁紙」）。`kwl_glass_wallpaper_poll` は `glass_answer` を返すだけにする。
   答えを待つ job がある間は `kwl_glass_wallpaper_begin` は EBUSY（settings の待ちは 1 つ）。
3. landscape: loader が走っていても main で描かず、picture 無しの job を T9 に積む（答えは `glass_answer`）。
4. `kwl_glass_resize`: 古い image のまま描き続け（大きさが違う間は引き伸ばし、design §4.7）、loader が空いていれば今の壁紙の path（無ければ landscape）で A1 → T9、
   走っていればその結果が大きさの違いで描き直される。T9 が無い時（thread の失敗）は今の main の経路を残す（log）。
5. 回転の `kwl_glass_size_prepare` は park の中（T3 の手順）なので p012 で A1・T9 に載せる。起動の prefetch は READY の前のまま（今も thread）。
6. 試験: `plan/ws309/tests/grave-guest.sh` の壁紙の変更（images=same|new、ms は decode と blur が thread なので main の時間ではなくなる: `KWL PERF pass` の pass_max と比べる）、
   出力の大きさの変化の試験（QEMU で output の size を変える経路を探す。無ければ未実施と書く）。

## 未決・注意

- 大きさが違う間の古い壁紙の描き方（引き伸ばし）: glass-map の map は blurred の generation・version で作り直すので、大きさの違う blurred でも描ける見込み（未確かめ）。

## 済み（q1662-i07、branch `agent/p1-ws309g`、60a4ee15b・b6b5b788c）

- T9（glass.c）: lane `glass_blur`（名前 blur、`kwl_glass_open` の glyph worker の後に起動、`kwl_glass_close` で stop、`KWL THREADS` に blur が出る）。job `struct glass_blur_job`。
  run で rows・blurred を確保して `wallpaper_render`。done（main）: 出力の大きさが変わっていれば同じ picture で積み直す。大きさが今の image と同じで staging を書けない時は
  `glass_blur_held` に置き、毎 pass の take で書けるようになった時（最長 `GLASS_BLUR_HOLD_MS` 200 ms、過ぎたら新しい image）に `blur_apply`（`wallpaper_target` ＋ `blur_copy`）。
- A1: `glass_loader`（その都度の thread）は read と decode だけ。join と T9 への submit は新しい `kwl_glass_blur_take`（main.c の `kwl_glass_glyphs_poll` の後、毎 pass、greeter でも回る）。
  `kwl_glass_wallpaper_poll` は settings の request の結果（`glass_answer`）を返すだけ。答えを待つ間は begin が EBUSY。
- landscape: T9 があれば picture 無しの job を直に積む（loader を通さない）。T9 が無い時だけ今の経路（loader か main）。
- 出力の大きさの変化（`kwl_glass_resize`）: T9 があれば古い image のまま描き（引き伸ばし）、loader が空いていれば今の壁紙の path で A1 → T9（`KWL GLASS resize width= height= ms= images=`）、
  走っていればその結果が大きさの違いで描き直される。T9 が無い時は今の main の経路。
- 変えた log: `KWL GLASS wallpaper path=... ms=N images=same|new` の ms は「依頼から表示まで」（thread の時間を含む。main の時間ではない）。wallpaper-time.sh・settings-p007.sh の正規表現は変えずに合う。
- 確かめ: amd64 bin/wayland・Linux（keiland-linux.mk、-Werror）exit 0・warning 0。arm64・riscv64 は下の確かめに書く。QEMU Venus `grave-guest.sh`:
  `build/ws309/p010a`（hold の前）PASS（graves=4、壁紙 682 ms images=new、landscape 2302 ms images=new）、`p010b` は guest が SSH に答えず（wayland を入れる前、compositor と無関係と見て同じ条件で 1 回やり直し）、
  `p010c`（hold の後）: 壁紙 318 ms・landscape 1867 ms、どちらも images=same、他の判定は全て通り、graves=0 で当時の判定の「墓の破棄が 1 つ以上」だけ落ちた。
  墓の数は image を手放した時に frame が飛んでいたかで決まるので判定から外し、表示だけにした（b6b5b788c。墓の経路は p009c・p009d・p010a で graves 2〜4 を見た）。
  pass_max_us は 371〜391 ms（p009d 366〜399 ms と同じ程度）。
- 未実施: 出力の大きさの変化の経路の QEMU の試験（output を変える試験の経路を探していない）、実機。

## 残り（次の起動はここから）

1. 回転（`kwl_glass_size_prepare`、park の中で main が read・decode・描画）を A1・T9 へ: 回転の手順（T3、p012）が結果を待つ形になるので p012 で一緒に行う案（Q1 に確かめる）。
2. 出力の大きさの変化の試験: QEMU で `kwl_glass_resize` を通す経路（output-switch.c の出力の切り替え、VENUS_OUTPUTS=2 など）を探し、古い image の引き伸ばしと結果の表示を確かめる。
3. 起動の prefetch（READY の前）は今のまま（原則 1 の例外）。
