<!-- awesome-plan project=zedbsd record=ws217-p003 -->

# ws217-p003: 入力の座標を回転に合わせる（touch panel・pen・絶対の pointer、anchor）

Parent: [WS217](../ws.md)
Status: in-progress（2026-10-13 P1 q959: 第 1 版の設計。design-reviewer の review を待たずに実装へ進む、指摘は同じ attempt で直す）
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
- `kwl_input_probe`: pen は `bit_is_set(capabilities->properties, INPUT_PROP_DIRECT)`、touch screen は 1、絶対の pointer は 1、他は 0。`attach_tablet` が `direct` を受ける。
- log: `KWL TABLET added … tilt=N direct=D`（行の末尾に足す。既存の試験は行の頭で照合している）。

## 5. 傾き（p001 §6.1）

- tablet.c の `send_axes()`: direct の pen は `tilt_fixed` の 2 つの値（度の wl_fixed）を `kwl_rotate_vector(kwl_output_rotation(server, KWL_PLANE_ANCHOR), …)` で論理の向きに回してから比べて送る。非 direct は回さない。
- `kwl_output_rotation(server, slot)`（heads.c、新）: anchor は `compose->rotation`、他は 0（p005 まで）。

## 6. ROTATED の入力の手順（p001 §4.6 の A の 5・8、C5）

- `kwl_input_rotated(server)`（input.c、新）: `kwl_touch_cancel(server, "rotated")`（全部の touch screen の指: shell の press は指の場所で離す、pointer の左の button を離す、drag の取り消し、OSK の touch の取り消し、title・edge の group を捨てる、client の指は wl_touch.cancel、以後その指は上がるまで `ROUTE_IGNORED`。今の `kwl_touch_remove` の notify の部分を device を忘れずに全部の screen に）、`kwl_tablet_cancel(server)`（下りている pen は up、全部の pen は proximity_out。tool は近いままなので、次の report で新しい写像の点から proximity_in）。
- 呼ぶ所:
  - 実行中の回転（output-rotate.c の `kwl_output_apply_rotation`）: ROTATED の時、新しい turn と壁紙を作れた後・park と大きさの書き換えの前（`output_rotate_turn` の中、資源の失敗では何も変えない）。指の shell の press は古い論理の場所で離れる。
  - open の経路（output-switch.c の `kwl_output_rotated`）: ROTATED の時（大きさが既に変わっているので、shell の press は古い場所で新しい大きさの上で離れる。open の中の指は稀）。
- geometry の書き: ROTATED の時の `kwl_output_pointer_turn` の warp が新しい geometry を pointer と同じ区間で書く（§2）。open の経路で別の panel に移る時（pointer の warp が無い）は、`kwl_input_geometry(server)` を直に呼ぶ。両方の経路の最後にも `kwl_input_geometry` を呼ぶ（同じなら書かない）。
- 窓の move/resize の grab（mouse の button を押したままの移動）は終えない（設計との違い 1）。

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
- `host-hid-pen-props.{c,sh}`（新）: kernel の hid-report・hid-digitizer・hid-touch・hid-input を freestanding で build し、記録する input layer の fake で `drv_hid_input_prepare` → `drv_hid_input_publish`。BUG-267 の if1（Pen）→ DIRECT、if0（Touch Screen の node）→ DIRECT（既存）、合成の Digitizer だけの pen → POINTER、合成の Digitizer の pen と touch screen の 1 つの descriptor → DIRECT（補い）、mouse → 0。
- build: amd64（`bin/wayland`・`vmunix`・`bin/peninject`、ci と試験の config）、rpi4（`bin/wayland`・`vmunix`）、`keiland-linux`、warning 0。`plan/tools/keiland-os-boundary/check.sh`。
- 回帰: `host-rotate.sh`・`host-scanout-rules.sh`、WS213 の `cursor-due-host-test.sh`・`input-thread-host-test.sh`（後者は warp の引数が変わるので直しが要る、§13）、WS143 の `hid-input-host-test.sh`（pen の property が増えるので比べ方の直しが要る、§13）、WS079 の `run-hid-touch.sh`。

## 12. T1 の試験（束 A の p003 の分、QEMU、policy ANY）

p001 §12.2 の 3 と 7 の C5 の行。image は p002b と同じ（`AAT_CONFIG=plan/ws217/tests/config-amd64.mk plan/tools/aat/build-image.sh`）。`aat-input start --width 1280 --height 800`（物理の大きさ）、`keiland-shot --request "POLICY any"`、`--request "ROTATE 90"` の後:

1. touch: `aat-input tap X Y`（物理）→ log の `KWL TOUCH … x=… y=…` が論理 (Y, 1279−X)。270 は (799−Y, X)、180 は (1279−X, 799−Y)、0 は (X, Y)。
2. 絶対の pointer: `aat-input move-to X Y` → `keiland-shot --request POINTER` が同じ式。
3. pen（direct）: `peninject` の `size 1279 799`、`down X Y 2000 20 0` → `KWL TABLET down … x=Y y=1279−X direct=1 tilt=0,-20`（90°: 傾き (20,0) → (0,−20)）。`--pointer` の pen は 90° でも回さずに論理の画面全体へ: `x=⌊X·799/1279⌋ y=⌊Y·1279/799⌋`、`direct=0 tilt=20,0`。
4. C5: 指を `touch-down 0 X Y` で押したまま `ROTATE 270` → `KWL TOUCH cancel … reason=rotated`、続く `touch-move` は何も起こさない、`touch-up` の後の `tap` は新しい写像。90↔270（大きさが同じ）と 0↔180（policy ANY の turn）でも同じ。
5. 0° の回帰: AAT の smoke（p002b の D と同じ回でよい）。

## 13. 他の WS の試験（Q1 に許可を頼む）

1. `plan/ws213/tests/input-thread-host-test.c`（WS213 incomplete、`kwl_cursor_box_warp` の 3 か所）: outputs の配列を `struct kwl_cursor_geometry` に入れて渡す形に。
2. `plan/ws143/tests/hid-input-host-test.c`（WS143 incomplete、`compare_devices`）: 古い usb-hid の写しは pen に property を付けないので、新しい側の pen の device の property（DIRECT か POINTER）を除いて比べる。

## 14. 設計との違い（第 4 版との）

1. 窓の move/resize の grab（mouse）は終えない。回転は Settings の押しの後（button は離れている）か試験の request で起こり、button を押したままの回転は無い。押したままでも、今の output switch と同じく窓は refit され pointer と一緒に動く。
2. `mirror_fit[]` は p006 で足す（§2）。
3. head の物理・回転は p005 まで論理・0（§2）。

## 15. 制限

- p006 まで、全部の絶対の device は anchor に写る（今と同じ）。
- 机の上の板（非 direct）は論理の画面全体に写り、縦の画面では縦横比が崩れる（p001 §14）。
- 回転の最中に押していた pen は、上げて置き直すまで hover として届く（down は送り直さない）。

## Event

- 2026-10-13 / ws217-p003-design-1: P1 が q959 で第 1 版を書いた（source は main `86ee7cf3e`）。review を待たずに実装へ。
