<!-- awesome-plan project=zedbsd record=ws217-p003 -->

# ws217-p003: 入力の座標を回転に合わせる（touch panel・pen・絶対の pointer、anchor）

Parent: [WS217](../ws.md)
Status: in-progress（2026-10-14 P1: T1-592 の P3-3 の FAIL を解析した（§16.6）。code の誤りは見つからず、原因は試験の手順（pen の宣言の直後の接近が compositor の node の open より前に出て失われる）。§12 の 3・5 の script に宣言の後の `wait 3000` を足し、T1 への再試験（P3-3・P3-5）を §16.6 に書いた。T1 の結果を Q1 が判定するまで test-wait）
Disposition: normal
Primary Milestone: MG006（WS から継承）
Queue / attempts: q959（2026-10-13 Q1 の依頼、P1）。承認: ユーザーの選択（2026-10-13 AskUserQuestion「WS217 p003（入力の座標）を続ける」、WS217 はベータ2）と WS217 の D1〜D6・D11・D12（ws.md）
Purpose / goal: p001 第 4 版 §6.1・§6.6・§4.6 の A の 5・8 と §13 の p003 の行。回転した anchor の上で、touch panel・pen（画面の上の pen）・絶対の pointer（QEMU の tablet、AAT の絶対の pointer）が指した物理の点の論理の座標に届き、机の上の pen tablet は回さずに論理の画面全体へ写る。相対の device（mouse・touchpad）は今のまま（論理の上を動く）。
Prerequisites: p002b の出力（main 済み `2d804c237`: `kwl_output_apply_rotation`・`kwl_output_rotated`・`kwl_output_pointer_turn`・`compose->rotation`）。p002b は T1-583 待ちだが、p003 は p002b の code の上に入力を足すだけで、T1 の束 A（p002b+p003）は p001 §13 のとおり一緒に流せる。

## 1. 範囲

入れる物（§13 の p003 の行）:

1. `struct kwl_cursor_geometry`（cursor-box.h）と受け箱の書き方（§2）。
2. 絶対の device の写像（§3）: 純粋な `kwl_cursor_geometry_place()`（cursor-box.c）と main thread の `kwl_output_absolute()`（heads.c）。I thread の `kwl_cursor_box_place()`、touch.c の `read_report()`、tablet.c の `place_pointer()` をこれに置き換える。
3. direct の分類（§4）: `kwl_input_device.direct`。touch screen と絶対の pointer は常に 1、pen は `INPUT_PROP_DIRECT` のときだけ 1。
4. pen の傾き（§5）: direct の pen は `kwl_rotate_vector` で論理の向きに。
5. ROTATED の入力の手順（§6）: 指の contact の cancel、pen の up と proximity_out、受け箱の geometry の書き（pointer の「同じ物理の点」は p002b で済み、同じ区間で geometry も書く）。
6. kernel（§7）: `hid-input.c` が pen の device に `INPUT_PROP_DIRECT`（Pen の collection）か `INPUT_PROP_POINTER`（Digitizer だけ）を付ける。
7. 注入の pen（§8）: `INPUT_INJECT_KIND_PEN` に DIRECT、新しい `INPUT_INJECT_KIND_PEN_POINTER`（UAPI の追加、D12）は POINTER。peninject に `--pointer`。
8. Linux・FreeBSD の backend（§9）: `input-evdev.c` が `EVIOCGPROP` を読む（D11）。
9. 試験の口（§10）: 試験の image だけの shot.c に `POINTER` の request、tablet.c に pen の down の log 1 行。

入れない物（後の段）: head・mirror の上の写像と slot（p005・p006。この段の slot は anchor だけで、head は回らないので物理＝論理）、pen・touch と display の結び付け（`identity`・`touch=`・`absolute_slot` の選び方、p006）、contact の矩形と anchor だけの gesture（p006、S7）、Settings（p004）、game の session（p012）。

## 2. geometry と受け箱（p001 §6.6）

```c
struct kwl_cursor_physical {
	uint32_t width;
	uint32_t height;
	unsigned rotation;
};

struct kwl_cursor_geometry {
	struct kwl_plane_rect outputs[KWL_PLANE_SLOTS];      /* 論理（今の kwl_outputs の値） */
	unsigned output_count;
	struct kwl_cursor_physical physical[KWL_PLANE_SLOTS]; /* 物理の大きさと実効の回転 */
	unsigned absolute_slot;                               /* 絶対の pointer の出力（p006 まで anchor） */
};
```

- `mirror_fit[]` は p006（mirror の head に結んだ device）で足す。この段で足しても書き手も読み手も無い。
- 受け箱は今の `outputs[]`・`output_count` に `physical[]`・`absolute_slot` を足す（field の名前を変えると WS213 の試験と cursor-thread の差分が増えるので、geometry は「受け箱の 4 つの field を丸ごと写す」型として使う）。
- `kwl_cursor_box_warp(box, geometry, move, context, motion)`・`kwl_cursor_box_outputs(box, geometry)`: どちらも 4 つの field を 1 区間で書く（geometry NULL の warp は今どおり場所だけ）。
- M は `kwl_output_geometry(server, &geometry)`（heads.c、`kwl_outputs` の隣）で作る: anchor は `server->width/height` と `compose->rotation`（compose が無い時は 0）、物理は `kwl_rotate_size(rotation, 論理)`（= `compose->mode_*`）。head は p005 まで回転 0・物理＝論理。
- `kwl_pointer_warp(server, outputs, move, context)` の形は変えない（呼び手 6 つ）。outputs がある時は中で `kwl_output_geometry` を作り、矩形だけ渡された outputs に替えて warp に渡す。呼び手は全部、状態（大きさと回転）を変えた後に呼ぶので、回転は新しい値になる。
- `kwl_input_geometry(server)`（input.c、新）: geometry を作って `kwl_cursor_box_outputs` で書く。`kwl_input_sync` の毎 pass の書きもこれを呼ぶ。
- `kwl_cursor_box_init` は `physical[ANCHOR]` を（幅, 高さ, 0）、`absolute_slot` を anchor にする（L3: geometry の最初の書きの前の絶対の report も今と同じに写る）。

## 3. 写像（p001 §6.1）

- 純粋な `kwl_cursor_geometry_place(geometry, slot, direct, unit, across, down, &x, &y)`（cursor-box.c）: `across`・`down` は `struct kwl_cursor_axis {value, minimum, maximum}`、`unit` は 1（画素、I thread）か 256（24.8、touch・pen）。結果は plane の座標（slot の矩形の左上を足す）。
  - direct: 値 → slot の物理（`physical[slot]` の幅・高さ）の上の点（`unit` 単位、最大の端は `(size−1)·unit`）→ `kwl_rotate_pixel_back(rotation, width·unit, height·unit)` で論理 → 矩形の左上を足す。
  - 非 direct（机の上の板）: 値 → slot の論理の矩形（`outputs[slot]` の幅・高さ）の上の点（回さない、縦横比は崩れる、p001 §14）。
  - slot の矩形が大きさ 0（閉じた head）なら anchor。
- `kwl_rotate_pixel_back`（rotate.h の inline、新）: `kwl_rotate_point_back` と同じ式を inline に。cursor-box.c は rotate.c 無しで link される（WS213 の `input-thread-host-test.sh`・`cursor-due-host-test.sh`）ので inline にする。大きさを 256 倍で渡せば `kwl_rotate_fixed_back` と同じ（host 試験で総当たり）。
- 置き換え:
  - I の `kwl_cursor_box_place()`（QEMU の tablet・AAT の絶対の pointer）: `absolute_slot` の geometry で direct、`unit` 1。相対の動きを足した後の clamp は slot の論理の矩形の中。
  - touch.c の `read_report()` の最後の写像: `kwl_output_absolute(server, KWL_PLANE_ANCHOR, 1, …)`（touch screen は常に direct）。
  - tablet.c の `place_pointer()`: `kwl_output_absolute(server, KWL_PLANE_ANCHOR, input->direct, …)`。
  - touch.c と tablet.c の `scale_fixed()` は使う所が無くなるので消す（同じ式は `kwl_cursor_geometry_place` に 1 つ）。
- 0° で今と同じ: direct の 0° は物理＝論理で、式は今の `scale_fixed`・`kwl_cursor_scale_absolute` と同じ（host 試験で値の一致）。非 direct の 0° も同じ。

## 4. direct の分類（p001 §6.1、M6・D11）

- `struct kwl_input_device` に `unsigned direct`。
- `kwl_input_probe`: pen は `bit_is_set(capabilities->properties, INPUT_PROP_DIRECT)`、touch screen は 1、絶対の pointer は `INPUT_PROP_POINTER` が無ければ 1（review-1 M5: pressure の無い Digitizer の pen は絶対の pointer に落ち、kernel が POINTER を付けるので回らない。QEMU の tablet・AAT・virtio は property が無いので direct のまま）、他は 0。`attach_tablet` と `kwl_input_attach` が `direct` を受ける。
- log: `KWL TABLET added … tilt=N direct=D`（行の末尾に足す。既存の試験は行の頭で照合している）。

## 5. 傾き（p001 §6.1）

- tablet.c の `send_axes()`: direct の pen は `tilt_fixed` の 2 つの値（度の wl_fixed）を `kwl_rotate_vector(kwl_output_rotation(server, KWL_PLANE_ANCHOR), …)` で論理の向きに回してから比べて送る。非 direct は回さない。
- `kwl_output_rotation(server, slot)`（heads.c、新）: anchor は `compose->rotation`、他は 0（p005 まで）。

## 6. ROTATED の入力の手順（p001 §4.6 の A の 5・8、C5。review-1 の S1・M1・M3・M7・L1 で直した）

- `kwl_input_rotated(server)`（input.c、新）: 順に
  1. `kwl_data_drag_cancel(server)`（M1: drag and drop の最中なら取り消す。下の button の release で古い場所に drop しない）、
  2. `kwl_seat_grabs_end(server)`（seat.c、新、M3: p001 のとおり grab を終える。client の move/resize の button の宛先を忘れ、resize は最後の大きさで終え（`kwl_toplevel_button(0)`）、窓の move は今の場所で終え（`kwl_glass_toplevel_move_end`）、arrange の入れ替えも今の場所で終える。押したままの button は後で client に普通の release として届く。log `KWL SEAT grabs end grabs=N`）、
  3. `kwl_touch_cancel(server, "rotated")`（全部の touch screen の指: **shell の press は pointer を動かさずに今の場所で離す**（S1、`shell_release_here`）、pointer の左の button を離す、指の drag の取り消し、OSK の touch の取り消し、title・edge の group を捨てる、client の指は wl_touch.cancel、以後その指は上がるまで `ROUTE_IGNORED`。log `KWL TOUCH cancel all reason=rotated fingers=N` と、wl_touch の client の上の指があれば既存の `KWL TOUCH cancel client=… reason=rotated`）、
  4. `kwl_tablet_cancel(server)`（下りている pen は up、全部の pen は proximity_out。tool は近いままなので、次の report で新しい写像の点から proximity_in。log `KWL TABLET cancel pens=N`）。
- 呼ぶ所（どちらも pointer の「同じ物理の点」の warp の**前**、S1）:
  - 実行中の回転（output-rotate.c）: ROTATED の時、新しい turn と壁紙を作れた後・park と大きさの書き換えの前（`output_rotate_turn` の中、資源の失敗では何も変えない）。今は ROTATED は TURN の時だけ起こる。**p010 の REOPEN の経路でも同じく ROTATED の bit で呼ぶこと**（L1）。p001 §4.6 A の 5 は「park の中」だが、park の前の方が良い（入力は main thread の上で、park は R と cursor の thread だけを止める。L10）。
  - open の経路（output-switch.c の `kwl_output_rotated`）: ROTATED の時、`kwl_output_pointer_turn` の前。
- geometry の書き: ROTATED の時の `kwl_output_pointer_turn` の warp が新しい geometry を pointer と同じ区間で書く（§2）。両方の経路の最後に `kwl_input_geometry(server)` も呼ぶ。`kwl_cursor_box_outputs` は panel・角度・`absolute_slot` が変わった時に warp の番号を進め、main thread はその番号を受け取る（M7: 別の panel への switch で warp が無い時も、古い geometry で計算した place を捨てる）。
- `kwl_pointer_warp` の不変条件（L2）: 物理と角度は呼ばれた時の server の状態から、矩形は呼び手の物。呼び手は全部、状態を変えた後に直前の `kwl_outputs` で矩形を作って呼ぶ。

## 7. kernel の pen の property（p001 §6.1、BUG-267 の fixture）

- `hid-input.c` の `drv_hid_input_publish`: main の device の `info.properties` を `hid_input_pen_properties(input)`（static、新）で: `input->pen == HID_REPORT_PEN_DISPLAY` → `1 << INPUT_PROP_DIRECT`、`_TABLET` → 同じ `hid_input` の touch の device が DIRECT なら DIRECT（補い）、他は `1 << INPUT_PROP_POINTER`、`_NONE` → 0（今のまま）。
- UAPI は不変（`INPUT_PROP_*` は既にある）。i2c-hid も `drv_hid_input_publish` を通る。

## 8. 注入の pen（D12）

- `include/uapi/input-inject.h`: `INPUT_INJECT_KIND_PEN_POINTER 6U`（追加だけ、Q1 の判断で可、ws.md D12）。
- `input-inject.c`: setup の kind が PEN か PEN_POINTER なら pen を宣言。PEN は `1 << INPUT_PROP_DIRECT`、PEN_POINTER は `1 << INPUT_PROP_POINTER`。名前は `Test pen (input-inject)` と `Test pen tablet (input-inject)`（L19、p006 の identity で別の device）。`state->kind` は書きの経路のため PEN のまま。
- peninject: `peninject --pointer [SCRIPT]` で PEN_POINTER を宣言。usage と冒頭の説明に 1 行。

## 9. Linux・FreeBSD の backend（D11）

- `input-evdev.c` の `read_capabilities`: `EVIOCGPROP` を読む（失敗は空のまま、zedBSD の backend と同じ形）。header の説明（「Linux and FreeBSD for now」）を直す。
- 影響: Linux・FreeBSD で touch pad（`INPUT_PROP_POINTER`）が touch screen でなく touch pad として扱われ（ws159 の zedBSD と同じ）、画面の上の pen だけが direct になる。Linux・FreeBSD は回転を出さない（hw180 が無い、D14）ので、座標の写像は変わらない。

## 10. 試験の口

- shot.c（試験の image だけ）: request `POINTER` → `OK x=X y=Y output=O`（main thread の pointer の場所、plane の論理の座標）。AAT の絶対の pointer（`aat-input move-to`）の写像を T1 が確かめる。製品の `shot-none.c` は不変。
- tablet.c: pen が触れた時に `KWL TABLET down device=… x=… y=… direct=… tilt=…,…`（論理、傾きは度の整数）。押す度に 1 行。

## 11. 試験（host、`plan/ws217/tests/`）

- `host-rotate.c` に: `kwl_rotate_pixel_back` を 4 つの panel × 4 つの角度の全画素で `kwl_rotate_point_back` と、256 倍の大きさで `kwl_rotate_fixed_back` と一致。
- `host-absolute.{c,sh}`（新）: cursor-box.c・plane.c を link。`kwl_cursor_geometry_place` を 4 つの panel × 4 つの角度 × direct/非 direct × unit 1/256 で、軸の範囲の全部の値（小さい範囲）と端（最小・最大・外）を、「物理の画素を `kwl_cursor_scale_absolute` で出して `kwl_rotate_point_back`」の式と比べる。0° は今の式（`kwl_cursor_scale_absolute`、touch の `scale_fixed` の写し）と一致。slot の矩形の左上の足し。`kwl_cursor_box_place` の 4 つの角度（相対の足しと clamp）、`kwl_cursor_box_warp` と `kwl_cursor_box_outputs` が geometry の 4 つを丸ごと書くこと。
- `host-tablet-direct.{c,sh}`（T1-592 の解析で足した）: tablet.c を seat の fake と link し、peninject の hover・down・up の report を direct の pen と pen tablet に 0°・90° で入れ、surface が proximity_in・down・up・proximity_out を聞き、場所が §12 の 3 の期待値であることを確かめる。node を接近の後に open した場合（LATE-OPEN）の再現も印字する（失敗には数えない）。
- `host-hid-pen-props.{c,sh}`（新）: kernel の hid-report・hid-digitizer・hid-touch・hid-input を freestanding で build し、記録する input layer の fake で `drv_hid_input_prepare` → `drv_hid_input_publish`。BUG-267 の if1（Pen）→ DIRECT、if0（Touch Screen の node）→ DIRECT（既存）、合成の Digitizer だけの pen → POINTER、合成の Digitizer の pen と touch screen の 1 つの descriptor → DIRECT（補い）、mouse → 0。
- build: amd64（`bin/wayland`・`vmunix`・`bin/peninject`、ci と試験の config）、rpi4（`bin/wayland`・`vmunix`）、`keiland-linux`、warning 0。`plan/tools/keiland-os-boundary/check.sh`。
- 回帰: `host-rotate.sh`・`host-scanout-rules.sh`、WS213 の `cursor-due-host-test.sh`・`input-thread-host-test.sh`（後者は warp の引数が変わるので直しが要る、§13）、WS143 の `hid-input-host-test.sh`（pen の property が増えるので比べ方の直しが要る、§13）、WS079 の `run-hid-touch.sh`。

## 12. T1 の試験（束 A の p003 の分、QEMU、policy ANY。review-1 M2・L4 で足した）

p001 §12.2 の 3 と 7 の C5 の行。image は p002b と同じ（`AAT_CONFIG=plan/ws217/tests/config-amd64.mk plan/tools/aat/build-image.sh`、wltest と tablet-probe を足した）。`aat-input start --width 1280 --height 800`（物理の大きさ）、`keiland-shot --request "POLICY any"` の後。tap の点は edge の帯・keyboard の zone・title bar を避けた (1000, 400) と (600, 300)（L6、期待の行は shell の `KWL TOUCH shell contact=… x=… y=…` か client の `KWL TOUCH down …`）。`POINTER` の答えは I thread の event の後に変わるので、期待値に一致するまで 1 秒まで繰り返し読む（L4）。

1. touch: 各角度で `aat-input tap 1000 400`（物理）→ 論理 90 (400, 279)、270 (399, 1000)、180 (279, 399)、0 (1000, 400)。
2. 絶対の pointer: `aat-input move-to 1000 400` → `keiland-shot --request POINTER` が同じ論理の点。
3. pen（direct）: peninject の `size 1279 799`・`wait 3000`・`down 1000 400 2000 20 0`・`wait 300`・`up` → `KWL TABLET added … direct=1`、90° で `KWL TABLET down … x=400 y=279 direct=1 tilt=0,-a`（0° の `tilt=a,0` の a）。`peninject --pointer` は `name="Test pen tablet (input-inject)" … direct=0`、90° で `x=624 y=640 direct=0 tilt=a,0`。
4. C5 の指（実行中の回転）: 90 で tablet-probe `--touch` の窓の上に `touch-down 0 X Y` → `ROTATE 270` → `KWL TOUCH cancel all reason=rotated fingers=1` と `KWL TOUCH cancel client=… reason=rotated`、tablet-probe に `TABLETPROBE touch cancel`。続く `touch-move 0 …` で tablet-probe に motion が来ない、`touch-up 0` の後の tap は新しい写像。90↔270 と 0↔180 でも cancel。
5. pen の cancel（M2 (1)）: tablet-probe（tablet の protocol）の窓の上で peninject の `down` を保ったまま（script は `size 1279 799`・`wait 3000`・`down …`・`wait 5000`・`move …`・`up`）`ROTATE` → `KWL TABLET cancel pens=1`、tablet-probe に up と proximity_out、次の `move` で proximity_in（down は来ない、§15）。
6. open の経路（M2 (2)、S1）: 90 で shell の上に指を置いたまま（`touch-down 0 1000 400`）`keiland-shot --request "MODE mirror"` か output switch の無い 1 画面では `--rotate` を変えた compositor の起こし直しが無いので、**2 画面の image が要る**（WS113 の `config-amd64-p004b.mk` の形、p005 の束 C で流す。今回は未実施として記録してよい）。
7. grab（M3）: 窓の title を `aat-input down left` で掴んで少し動かしたまま `ROTATE 90` → `KWL SEAT grabs end grabs=1`、`KWL GLASS moved …`、その後の `up left` で窓が飛ばない。
8. mirror の拒み（M6）: 2 画面の image が要るので p005 の束で（`KWL OUTPUT rotate refused … reason=mirror`）。
9. 後始末（M2 (4)）: 最後に `ROTATE 0`・`POLICY default`、`$HOME/.config/keiland/displays.conf` に `rotate=` の行が無い。
10. 0° の回帰: AAT の smoke（p002b の D と同じ回でよい）。
11. Linux（M4、Q1 がユーザーに報告）: WS131 の Debian の QEMU+KVM の guest で keiland-linux を build し、uinput（`plan/ws213/tests/uinput-drive.c` の形）で MT＋`INPUT_PROP_POINTER` の device → `KWL TOUCHPAD added`（または `KWL INPUT … kind=touchpad`）、MT だけの device → `KWL TOUCH added`。FreeBSD は guest の native の build（`plan/tools/keiland-freebsd/backend-test.sh`）が warning 0。

## 13. 他の WS の試験（Q1 に許可を頼む）

1. `plan/ws213/tests/input-thread-host-test.c`（WS213 incomplete、`kwl_cursor_box_warp` の 3 か所）: outputs の配列を `struct kwl_cursor_geometry` に入れて渡す形に。
2. `plan/ws143/tests/hid-input-host-test.c`（WS143 incomplete、`compare_devices`）: 古い usb-hid の写しは pen に property を付けないので、新しい側の pen の device の property（DIRECT か POINTER）を除いて比べる。

## 14. 設計との違い（第 4 版との）

1. （review-1 M3 で取り消し: p001 のとおり grab を終える、§6）
2. `mirror_fit[]` は p006 で足す（§2）。
3. head の物理・回転は p005 まで論理・0（§2）。

## 15. 制限

- p006 まで、全部の絶対の device は anchor に写る（今と同じ）。
- 机の上の板（非 direct）は論理の画面全体に写り、縦の画面では縦横比が崩れる（p001 §14）。
- 回転の最中に押していた pen は、上げて置き直すまで hover として届く（down は送り直さない）。
- mirror の head が開いている間は回転を拒む（review-1 M6、Q1 の決め。`KWL OUTPUT rotate refused … reason=mirror`、extended の `reason=heads` と同じ ENOTSUP）。p006 で mirror の head の device を fit の逆写像で写すまで。
- 回転の cancel の shell の release は、click や gesture を完了させ得る（`kwl_touch_remove` と同じ形。取り消しの口は足さない、L9）。
- 板の pen tablet で Pen（0x02）の collection を申告する物は回り、Digitizer（0x01）を申告する pen の液晶は回らない（手で選ぶ口は Future Work）。touch の軸が panel の走査の向きと揃わない panel（calibration）は扱わない（L8）。
- p006 の identity の例（p001 §6.4 の `inject-pen`・`inject-pen-pointer`）は、この段の kernel の名前 `Test pen (input-inject)`・`Test pen tablet (input-inject)` から `virtual:0000:0000:Test_pen_(input-inject)` になる（L5、p006 で例と試験の期待値を直す）。
- p002b の T1 が FAIL して `output_rotate_turn`・`kwl_output_rotated` が変わると、この段の差し込み点も直す（L12）。

## 15b. review-1 への対応（2026-10-13、[review-1](review-1.md)、Q1 の決め）

| # | 対応 |
| --- | --- |
| S1 | 直した。open の経路で `kwl_input_rotated` を `kwl_output_pointer_turn` の前に。回転の cancel の shell の release は pointer を動かさない（`screen_release(…, at_finger 0)` → `shell_release_here`）。`kwl_touch_remove` は今どおり指の場所で（at_finger 1）。§6 |
| M1 | 直した。`kwl_input_rotated` の頭で `kwl_data_drag_cancel` |
| M2 | §12 に足した（tablet-probe を image に、pen の cancel、wl_touch の cancel、後始末、期待の行、点）。open の経路の指と mirror は 2 画面の image が要るので p005 の束 C へ |
| M3 | 直した（Q1 の決め: p001 のとおり）。`kwl_seat_grabs_end`（seat.c、新） |
| M4 | Q1 がユーザーに報告。§12 の 11 に Linux の guest の確かめと FreeBSD の build |
| M5 | 直した。絶対の pointer は `INPUT_PROP_POINTER` が無ければ direct（`kwl_input_attach` に `direct` の引数） |
| M6 | 直した（Q1 の決め）。mirror の head が開いている間も ENOTSUP（`reason=mirror`、`output_rotate_heads`） |
| M7 | 直した。`kwl_cursor_box_outputs(box, geometry, &warp)` が panel・角度・`absolute_slot` の変化で warp の番号を進め、main は `server->pointer_warp` に受ける。host-absolute に確かめ |
| L1・L2・L3・L10 | §6・§2 に記録 |
| L4・L6 | §12 |
| L5・L8・L9・L12 | §15 |
| L7 | header の説明は `keiland-backend-evdev.h` で直した（§16.1 の表のとおり） |
| L11 | host-hid-pen-props に「Digitizer の pen と touch pad → POINTER」、BUG-267 if1（pen と mouse の組）が DIRECT を期待値に（既にあった） |

T1-583（p002b）の不具合も同じ attempt で直した: `POLICY default`（実効 0・選択 90 から選択 0）→ `POLICY any` → `ROTATE 0` が `OK applied=1 saved=-1` で displays.conf に `rotate=… 90` が残った。原因: 選択も実効も 0 なので `kwl_rotate_select` が NOTHING を返し、config の行（policy は書かない）を見ていなかった。直し: NOTHING の時に displays.conf の値（`kwl_displays_rotation`）が頼まれた角度と違えば書いて保存（log `KWL OUTPUT rotate key=… selected=0 saved=0 applied=1 reason=kept`）。p002b の B のため `plan/ws217/tests/config-amd64.mk` に wltest（と p003 の tablet-probe）を足した。

## 16. 実行の記録（2026-10-13 P1、agent/p1、base main `86ee7cf3e`）

### 16.1 変えた所

| file | 内容 |
| --- | --- |
| `userland/desktop/wayland/cursor-box.h`・`.c` | `struct kwl_cursor_physical`・`kwl_cursor_geometry`・`kwl_cursor_axis`、受け箱に `physical[]`・`absolute_slot`、`kwl_cursor_box_warp(box, geometry, …)`・`kwl_cursor_box_outputs(box, geometry)` が丸ごと書く、純粋な `kwl_cursor_geometry_place()`、`kwl_cursor_box_place()` が `absolute_slot` の panel で direct に写す。使う所の無くなった `kwl_cursor_scale_absolute()` を消した |
| `rotate.h` | inline の `kwl_rotate_pixel_back()`（cursor-box.c は rotate.c 無しで link される） |
| `heads.c`・`kwl.h` | `kwl_output_geometry()`・`kwl_output_rotation()`・`kwl_output_absolute()`、`kwl_input_device.direct` |
| `input.c` | pen の `INPUT_PROP_DIRECT` を読み `attach_tablet(…, direct)`、touch screen と絶対の pointer は direct、`kwl_pointer_warp` が outputs を geometry に包む、`kwl_input_geometry()`（`kwl_input_sync` もこれ）、`kwl_input_rotated()`、log `KWL INPUT … kind=tablet abs=1 direct=D` |
| `touch.c`・`touch.h` | `read_report` の写像を `kwl_output_absolute(…, direct 1)` に、`kwl_touch_cancel()`（`screen_gestures_end`・`screen_release` を `kwl_touch_remove` と共有、log `KWL TOUCH cancel all reason=… fingers=N` と既存の `KWL TOUCH cancel client=…`）、`scale_fixed` を消した |
| `tablet.c`・`tablet.h` | `place_pointer` を `kwl_output_absolute(…, input->direct)` に、傾きを `device_tilt()`（direct なら `kwl_rotate_vector`）、`kwl_tablet_cancel()`（log `KWL TABLET cancel pens=N`）、log `KWL TABLET down …`、`KWL TABLET added … direct=D`、`scale_fixed` を消した |
| `output-rotate.c` | TURN の資源を作れた後・park の前に `kwl_input_rotated`、最後に `kwl_input_geometry` |
| `output-switch.c` | open の経路の ROTATED で `kwl_input_rotated`、最後に `kwl_input_geometry` |
| `shot.c` | request `POINTER` → `OK x=X y=Y output=O`（受け箱の場所） |
| `src/drivers/generic/hid-input.c` | `hid_input_pen_properties()`（Pen → DIRECT、Digitizer → POINTER か同じ descriptor の touch screen が DIRECT なら DIRECT、pen 無し → 0） |
| `include/uapi/input-inject.h`・`src/drivers/generic/input-inject.c` | `INPUT_INJECT_KIND_PEN_POINTER 6U`、PEN は DIRECT、PEN_POINTER は POINTER と名前 `Test pen tablet (input-inject)` |
| `userland/tests/peninject/main.c` | `peninject --pointer [SCRIPT]` |
| `userland/desktop/libkeiland-backend/evdev/input-evdev.c`・`keiland-backend-evdev.h` | Linux・FreeBSD も `EVIOCGPROP` を読む（D11） |
| `plan/ws217/tests/` | `host-rotate.c`（`kwl_rotate_pixel_back` の総当たり）、新 `host-absolute.{c,sh}`、新 `host-hid-pen-props.{c,sh}` |
| 他の WS の試験（Q1 の許可、変更への追従） | `plan/ws213/tests/input-thread-host-test.c`（warp を geometry で、helper `warp_outputs`）、`plan/ws143/tests/hid-input-host-test.c`（`compare_devices` で新しい側の pen の property を除く `pen_properties`）、`plan/ws113/tests/host-output-switch.c`（stub の `kwl_input_rotated`・`kwl_input_geometry` の 2 つ。許可の 2 つの外だが、p002a で Q1 が直したのと同じ種類の link の追従） |

### 16.2 command と結果（host、clang は tree の LLVM、host の cc は gcc 14.2.0）

- `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws217-p003/amd64 build/ws217-p003/amd64/bin/wayland`・`… vmunix` → exit 0、warning 0。
- `make -j16 ZEDBSD_CONFIG=plan/ws217/tests/config-amd64.mk BUILD=build/ws217-p003/amd64t …/bin/wayland`・`…/bin/peninject`・`…/vmunix`（input-inject.c を含む）→ exit 0、warning 0。
- `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/ws217-p003/arm64 …/bin/wayland`・`…/vmunix` → exit 0、warning 0。
- `make -j16 KEILAND_ASK=n KEILAND_LINUX_BUILD=build/ws217-p003/linux keiland-linux` → exit 0、warning 0（input-evdev.c・cursor-box.c・tablet.c を含む、install はしていない）。FreeBSD の build は未実施（Makefile は不変、input-evdev.c は Linux と共有）。
- `sh plan/ws217/tests/host-rotate.sh` → PASS（plain・ASan/UBSan）。`host-absolute.sh` → PASS（5 つの panel × 4 角度 × anchor/head × direct/非 direct × 画素/24.8 で値 −5..25 を参照の式と比べる、0° は前の式と一致、90° の (1000,400) → (400,279)、小さい panel の全単射、受け箱の 4 角度・相対の clamp・geometry の丸ごとの書き）。`host-hid-pen-props.sh` → PASS（6 件。前の hid-input.c で流すと 4 件 FAIL になることも確かめた）。`host-scanout-rules.sh`・`host-displays-conf.sh` PASS。
- 回帰: `plan/ws213/tests/input-thread-host-test.sh`（直した後）PASS（plain・ThreadSanitizer）、`cursor-due-host-test.sh` PASS、`plan/ws143/tests/hid-input-host-test.sh`（直した後）PASS（直す前は「tablet pen: main device」と BUG-267 if1 の 2 件の FAIL = 新しい property）、`plan/ws113/tests/host-output-switch.sh`（stub の後）PASS、`plan/ws159/tests/run-host-touchpad.sh` PASS（32 checks）。
- `plan/bugs/BUG-267/tests/run-host-touch-group.sh` は main で既に link が壊れている（`kwl_pointer_warp_to` など ws213-p004 の後の関数の stub が無い）。master の一覧に無いので直していない（削除の候補、Q1 へ）。
- `sh plan/tools/keiland-os-boundary/check.sh` → PASS。`python3 -I plan/tools/style-check.py`（変えた file と新しい試験）→ 新しい指摘 0（base の時から残る WS213 の試験の既存の指摘は除く）。`git diff --check` 空。

### 16.3 Linux・FreeBSD の EVIOCGPROP の影響（Q1 の依頼）

- 分類の入力が増えるだけで、`kwl_input_probe` の順は不変: pen（BTN_TOOL_PEN と pressure）→ touch pad（MT と `INPUT_PROP_POINTER`）→ touch screen（MT）→ keyboard → 絶対の pointer → 相対の pointer。
- 5330 の Linux の記録 `plan/ws159/tests/latitude5330-linux/input-devices.txt`: Touchpad は `PROP=5`（POINTER と BUTTONPAD）→ これまで Linux では MT なので **touch screen** として扱われていた（指が絶対の点として写る）のが、zedBSD と同じ touch pad の層（相対の動き・tap・scroll）になる。PS/2 の mouse は `PROP=1` だが MT でないので不変、他の device は `PROP=0` で不変。touch pad の層は Linux の build に既に入っており（`Makefile.linux:16`）、host 試験 `run-host-touchpad.sh` が PASS。
- touch screen（Linux は `INPUT_PROP_DIRECT`）は不変。pen は display の pen（DIRECT）だけ direct、Intuos などの板（POINTER）は非 direct: 0° では写像は同じ（Linux・FreeBSD は hw180 が無く回転を出さない、D14）。
- QEMU+KVM の Linux guest での確かめは T1 の範囲（WS131 の Debian guest、touch pad の device が無いので分類の行 `KWL INPUT` だけ）。実機の Linux は未実施。

### 16.4 未実施と残り

- QEMU（T1、§12）と実機（5330 の内蔵 panel・pen LCD、p007）は未実施。
- design-reviewer の review（Q1 が回した）の指摘は同じ attempt で直す。
- 消す物（Q1 へ）: `build/tmp/ws217-mut/`（前の hid-input.c で試験が FAIL することを確かめた一時物）、`build/tmp/ws217-absolute.*`・`ws217-hid-pen-props.*`・`ws217-rotate.*` の古い run。

### 16.5 review-1 と T1-583 の直しの後（2026-10-14 P1）

- 変えた所: `seat.c`（`kwl_seat_grabs_end`）、`kwl.h`（その宣言、`kwl_input_attach` の `direct`）、`input.c`（`kwl_input_rotated` の順、絶対の pointer の `INPUT_PROP_POINTER`、`kwl_input_geometry` の warp の番号）、`touch.c`（`screen_release` の `at_finger`、`shell_release_here`）、`cursor-box.[ch]`（`kwl_cursor_box_outputs` の warp の番号）、`output-switch.c`（cancel を pointer の turn の前に）、`output-rotate.c`（NOTHING の時の displays.conf、`reason=mirror`）、`plan/ws217/tests/config-amd64.mk`（wltest・tablet-probe）、`host-absolute.c`（warp の番号）、`host-hid-pen-props.c`（touch pad の fixture）。
- build（warning 0、exit 0）: amd64 ci の `bin/wayland`、試験の config の `bin/wayland`・`bin/wltest`・`bin/tablet-probe`、rpi4 の `bin/wayland`、`keiland-linux`。kernel（hid-input.c・input-inject.c）は review の後に変えていない（§16.2 の vmunix のまま）。
- host: host-absolute・host-rotate・host-hid-pen-props（7 件）・WS213 input-thread（plain・TSan）と cursor-due・WS113 host-output-switch・WS143 hid-input が PASS、keiland-os-boundary PASS、style-check の新しい指摘 0、`git diff --check` 空。
- 未実施: FreeBSD の build（guest が要る、T1 の §12 の 11）、QEMU・実機、open の経路と mirror の拒みの QEMU（2 画面の image、p005 の束）。

### 16.6 T1-592 の P3-3 の FAIL の解析（2026-10-14 P1 第 4 世代、base main `6545a532e`）

T1-592（QEMU Venus、tree `127cc1472`）: 注入の pen（`--pointer` 無し）で `KWL TABLET added … direct=1` と `removed` は出るが `KWL TABLET down` が無く、tablet-probe も proximity・motion・down を受けない。0°・90°、(1000,400)・(600,300)、hover→down→up の script のどれも同じ。`--pointer` を付けると `down direct=0` が出た。`127cc1472` から今の main まで、wayland・input-inject・input.c・peninject・UAPI の変更は無い。

調べたこと（code を読んだ範囲と host 試験）:

1. 注入の device の側: `input-inject.c` の PEN と PEN_POINTER の違いは名前と `info.properties`（DIRECT か POINTER）だけで、axes・capability・event の検査と emit（`inject_write_pen` → `drv_input_device_emit`）は同じ。kernel の input layer（`src/drivers/generic/input.c`）は `properties` を保存して `EVIOCGPROP` と system event の detail に出すだけで、event の配送（capability の検査・queue・subscriber）は property を見ない。peninject の違いは setup の `kind` だけ。
2. compositor の分類: `kwl_input_probe` の pen の枝は `INPUT_PROP_DIRECT` で `direct` を決めるだけで、`attach_tablet`・`input_attach`（I thread への attach）・I thread の `input_report`・main thread の `apply_report` → `kwl_tablet_frame` は `direct` を見ない。`device->direct` を読むのは tablet.c の `place_pointer`（`kwl_output_absolute`）・`device_tilt`・log の 3 か所だけ（`grep -- '->direct'`）。名前（`Test pen (input-inject)`）を見る所も無い。
3. tablet.c の direct の経路（host 試験 `plan/ws217/tests/host-tablet-direct.sh`、新）: 実の tablet.c・cursor-box.c・plane.c・rotate.c に heads.c と同じ式の `kwl_output_absolute` と seat の fake を付け、peninject の hover・down・up の report を入れた。direct=1・0 の 0° と 90° の 4 件で、surface が proximity_in・down・up・proximity_out を 1 回ずつ聞き、`KWL TABLET down` が出て、場所は 0° (1000,400)、90° direct (400,279)・`tilt=0,-20`、90° 非 direct (624,640)。PASS（plain・ASan/UBSan）。**direct の経路は report が届けば down を出す**。
4. 再現（同じ試験の LATE-OPEN）: compositor が node を open するのは device の event（`KWL EVENT input changed`）の後の main の pass の scan で、sessiond が node を利用者に渡す前なら `KWL_INPUT_SETTLE_SCAN_MS`（100 ms）ごとに再び scan する（BUG-264）。open の前に書かれた event は compositor の reader に届かない。tablet.c は open の時に key の状態（`EVIOCGKEY`）を読まない（axes は `EVIOCGABS` で読む）ので、接近（`BTN_TOOL_PEN 1`）が open より前だと、後の report には tool が無く（`pen_near` 0）、`apply_report` は何も配らない。open の後に down・up の report だけが届く場合を host で流すと `LATE-OPEN direct=1 proximity_in=0 down=0 up=0`（direct=0 も同じ）: **T1-592 の症状と同じ**。
5. 手順: §12 の 3 の script（`size 1279 799`・`down …`・`up`）は宣言と接近の間に wait が無い。peninject は宣言の直後（μs）に down の frame を書くので、compositor の open（数 ms〜100 ms 余り）より前になる。WS079 の pen の試験の script は全部 `size` の後に `wait 3000` を置いている（`plan/ws079/tests/p003-scripts.py`）。

結論: 注入の device と compositor の direct の経路に誤りは見つからなかった。症状は、宣言の直後の接近が node の open より前に出て失われることで説明でき、host で同じ症状を再現した。T1 の報告には script の本文が無いので、`--pointer` の回だけ down が出た理由（wait の有無か open の速さ）は確かめられていない。§12 の 3・5 の script を直した（宣言の後に `wait 3000`）。code は変えていない。

残り（Q1 の判断）: 接近したままの pen を後から open した場合（compositor の起動・hotplug の遅れ・seat の pause の後）、pen が一度離れるまで何も届かない。直すには open の時に key の状態を読む backend の関数（`EVIOCGKEY`、zedBSD・Linux・FreeBSD の 3 つの backend）が要り、p003 の範囲の外なので実装していない（Future Work の候補）。

T1 への再試験の依頼（P3-3・P3-5、Q1 が T1 に送る文案）:

> ws217-p003 の P3-3（pen の direct）と P3-5（pen の cancel）の再試験。image・起動は T1-592 と同じ（`AAT_CONFIG=plan/ws217/tests/config-amd64.mk plan/tools/aat/build-image.sh`、`aat-input start --width 1280 --height 800`、`keiland-shot --request "POLICY any"`）。注意: aat-input は inject の open を 4 つ持つ（`INPUT_INJECT_OPENS_MAX` 4）ので、peninject を流す時は aat-input を止めておく（T1-592 と同じやり方でよい）。**どの script も `size` の直後に `wait 3000` を置く**（compositor が node を open する前の event は届かない、phase.md §16.6）。script の本文は結果に貼る。
> 1. P3-3: tablet-probe の窓を (600,300) の上に置き、0° で script A = `size 1279 799` / `wait 3000` / `hover 600 300` / `wait 300` / `down 600 300 2000 20 0` / `wait 300` / `up`。期待: `KWL INPUT device=… kind=tablet abs=1 direct=1` が `hover` より前、`KWL TABLET down device=… x=600 y=300 direct=1 tilt=20,0`、tablet-probe に proximity_in・motion・down・up・proximity_out。
> 2. P3-3 の 90°: `ROTATE 90` の後、script B = `size 1279 799` / `wait 3000` / `down 1000 400 2000 20 0` / `wait 300` / `up` → `KWL TABLET down … x=400 y=279 direct=1 tilt=0,-20`。同じ script B を `peninject --pointer` で → `name="Test pen tablet (input-inject)" … direct=0`、`KWL TABLET down … x=624 y=640 direct=0 tilt=20,0`。
> 3. P3-5: 90° で tablet-probe の窓の上の物理の点（窓の場所を見て決める）に script C = `size 1279 799` / `wait 3000` / `down X Y 2000` / `wait 5000` / `move X Y 2000` / `wait 300` / `up`。`wait 5000` の間に `keiland-shot --request "ROTATE 270"`。期待: `KWL TABLET cancel pens=1`、tablet-probe に up と proximity_out、続く `move` で proximity_in（down は来ない）。最後に `ROTATE 0`・`POLICY default`。
> 4. もし 1 で `wait 3000` を置いても down が無ければ、同じ回で `peninject -d 8000` を別に流して node に event が出ていること（`PENDUMP` の行）と、compositor の `KWL INPUT …`・`KWL EVENT input changed` の行の順を記録して返す（解析は P1）。

## Event

- 2026-10-13 / ws217-p003-design-1: P1 が q959 で第 1 版を書いた（source は main `86ee7cf3e`）。review を待たずに実装へ。
- 2026-10-13 / ws217-p003-impl-1: P1 が実装（§16）。build と host 試験 PASS、他の WS の試験 3 つを変更に追従（Q1 の許可）。
- 2026-10-13 / ws217-p003-review-1: design-reviewer の [review-1](review-1.md)（条件つきで進めてよい、致命的 0・重大 1・中 7・軽微 12）。Q1 の決め（S1・M1・M2・M5・M7 は推奨どおり、M3 は grab を終える、M6 は mirror で拒む、M4 は Q1 がユーザーに報告）。
- 2026-10-14 / ws217-p003-impl-2: P1 が review-1 の全部と T1-583（p002b）の saved=-1 を直し、試験の image に wltest・tablet-probe を足した（§15b・§16.5）。
- 2026-10-14 / ws217-p003-t1-592: T1-592 で P3-3（direct の pen）が FAIL。P1（第 4 世代）が解析し、code の誤りは無く試験の手順（宣言の直後の接近が node の open より前）と判断、host で症状を再現（§16.6）。§12 の 3・5 の script を直し、T1 への再試験（P3-3・P3-5）の文案を書いた。
