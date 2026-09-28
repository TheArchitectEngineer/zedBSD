# WS079 設計 A: ペンの入力・上の右端からのスワイプ・Notes（ws079-p001）

2026-09-28。[WS079](ws.md) の p001 の設計のうち、入力（kernel と compositor）、gesture、Notes の部分。
PDF の形式・編集の metadata の埋め込み方・libpdf の構成・PDF Viewer は [design-pdf.md](design-pdf.md)（別の設計）が持つ。
この文書はそれを参照するだけで決めない。決定と書いたものも、§8 の判断が決まるまでは案である。

## 1. 今の事実（2026-09-28 のコード）

| 事柄 | 今 | 場所 |
| --- | --- | --- |
| USB HID | report descriptor を解析する（boot protocol ではなく report protocol）。usage page は Generic Desktop（X・Y・Wheel）・Keyboard・Button（1〜5）だけを event にする。**Digitizer page（0x0D）は無視**。Physical Min/Max・Unit Exponent・Unit の global は読み飛ばす（値を持たない）。collection の usage は数だけ数え、種類を覚えない。ABS_X を持つ device の名前は「USB HID tablet」 | `src/drivers/usb/usb-hid.c`（`usage_to_event`・`parse_global`・`parse_main`・`usb_hid_publish_report`）、`include/drivers/usb/hid-report.h` |
| kernel の入力の層 | `drv_input_device_register` で capability（type・code）と absolute 軸（`input_absinfo`）を宣言し、`drv_input_device_emit` で 1 event ずつ出す。宣言に無い code は捨てる。`/dev/input/eventN`（evdev 互換の ioctl、queue は device ごとに 256 event）。device は最大 8 | `src/drivers/generic/input.c`、`include/kern/input-device.h`、`include/kern/input-capability.h`、`include/kern/input-queue.h` |
| UAPI の code | Linux の evdev と同じ番号。`ABS_X`・`ABS_Y`・`ABS_MT_*`・`BTN_LEFT`〜`BTN_EXTRA`・`INPUT_PROP_POINTER/DIRECT` はある。**`ABS_PRESSURE`・`ABS_TILT_*`・`BTN_TOOL_PEN`・`BTN_TOUCH`・`BTN_STYLUS*` は無い** | `include/uapi/input.h`（libc の `include/libc/dev/evdev/input-event-codes.h` はこれを読むだけ） |
| devfs の権限 | `/dev/input/event*` は 0640 root:wheel。login の session では sessiond が seat の device を user に移す | `src/kern/devfs.c` |
| compositor の入力 | `/dev/input/eventN` を読み、REL_X/Y か ABS_X/Y を持てば pointer、文字の key を持てば keyboard。SYN_REPORT までを 1 frame（最大 64 event）にまとめて適用。absolute は出力の全面へ写す（`scale_absolute`）。0x100〜0x15f の button の塊（`BTN_TOUCH` 等）は捨てる。wl_touch は無い | `userland/desktop/wayland/input.c`、`seat.c`、`zwl.h` |
| global の一覧 | compositor は手書きの wire（`protocol.c` の `globals[]`）。client の libwayland も手書きの interface の表（`glass-protocol.c` 等） | `userland/desktop/wayland/protocol.c`、`userland/desktop/libwayland/` |
| 角の gesture | App Home: 左上の 28×28（`HOME_CORNER`）か launcher（x<40、y<34）で押し、右下へ 14 px 以上ずつ動くと gesture、`(dx+dy)/2 / 360` が進み、離したとき 0.30 以上で開く | `userland/desktop/wayland/home.c` |
| その他の gesture | Wiseview: 下の端（20 px）から上へ、240 px、0.35 | `userland/desktop/wayland/shell.c` |
| system bar | 高さ 34（`ZWL_GLASS_BAR`）。左に launcher、右端に network・battery（絵だけ）・時計。docked の窓は title bar の control を bar の application の zone（左〜中央）に出す | `shell.c`、`network.c`、`titlebar-shell.c` |
| app の起動 | `home_launch`: `fork` して `/bin/sh -c`、compositor の socket を環境に。窓の対応は launch 後 5000 ms 以内の map | `home.c` |
| 窓の識別 | `xdg_toplevel.set_app_id` を `surface->app_id[64]` に保つ | `protocol.c`、`zwl.h` |
| fullscreen | client の `set_fullscreen` で原点・出力の大きさ。最前面の窓が fullscreen なら direct scanout の mode | `protocol.c`、`display.c` |
| Vulkan の app | terminal・files・mview が `vkCreateWaylandSurfaceKHR` で描く | `userland/desktop/terminal/render.c` ほか |
| QEMU | 10.0.11。`usb-wacom-tablet`（「QEMU PenPartner Tablet」）・`usb-tablet`・`virtio-tablet` がある。筆圧を段階で出す装置は無い（PenPartner は host の mouse の button から作るので、筆圧は実質 0 か最大と見込む。p002 で確かめる、未確認） | `qemu-system-x86_64 -device help` |

## 2. ペンの入力: USB HID の digitizer → kernel

### 2.1 HID の usage と evdev の event

HID の Pen の application collection（Usage Page 0x0D、Usage 0x02 Pen か 0x01 Digitizer）の中の field を次のように写す。
番号は Linux の evdev と同じ（既存の UAPI の方針に揃える）。

| HID の usage | 意味 | evdev | 備考 |
| --- | --- | --- | --- |
| 0x01:0x30 X / 0x01:0x31 Y（絶対） | 位置 | `EV_ABS` `ABS_X` 0x00 / `ABS_Y` 0x01 | absinfo の min/max は logical、resolution は Physical と Unit（cm か inch、Unit Exponent）から単位/mm |
| 0x0D:0x30 Tip Pressure | 筆圧 | `EV_ABS` `ABS_PRESSURE` 0x18 | 4096 段階なら logical 0..4095。kernel は正規化しない（生の値と absinfo を出す） |
| 0x0D:0x3D X Tilt / 0x3E Y Tilt | 傾き | `ABS_TILT_X` 0x1a / `ABS_TILT_Y` 0x1b | Unit が度（English Rotation / SI Rotation）なら resolution を単位/radian で入れる（Linux と同じ） |
| 0x0D:0x32 In Range | 近接 | `EV_KEY` `BTN_TOOL_PEN` 0x140 か `BTN_TOOL_RUBBER` 0x141 | どちらかは Invert で決める（下） |
| 0x0D:0x42 Tip Switch | 接触 | `BTN_TOUCH` 0x14a | |
| 0x0D:0x45 Eraser | 消しゴムの端の接触 | `BTN_TOUCH`（道具は RUBBER） | |
| 0x0D:0x3C Invert | 消しゴムの端が近い | 道具を `BTN_TOOL_RUBBER` に | |
| 0x0D:0x44 Barrel Switch | 側面の button 1 | `BTN_STYLUS` 0x14b | |
| 0x0D:0x5A Secondary Barrel Switch | 側面の button 2 | `BTN_STYLUS2` 0x14c | |
| 0x0D:0x5B Transducer Serial Number | ペンの個体 | `EV_MSC` `MSC_SERIAL` 0x00（下位 32 bit） | 無ければ出さない |
| 0x0D:0x36 Height（あれば） | 高さ | `ABS_DISTANCE` 0x19 | 任意 |

device の property: collection の usage が Pen（0x02、画面一体の液晶タブレット）なら `INPUT_PROP_DIRECT`、Digitizer（0x01、板のタブレット）なら
`INPUT_PROP_POINTER`。名前は「USB HID pen」。

### 2.2 状態の合成（1:1 の写しでは足りない所）

In Range・Invert・Tip Switch・Eraser は組み合わせて道具と接触を決める。decode の後に小さな状態機械を通す:

- 道具 = In Range なら（Invert か Eraser が 1 なら RUBBER、でなければ PEN）、In Range が 0 なら無し。
- 接触 = Tip Switch か Eraser。
- 範囲に入るとき: `BTN_TOOL_*`=1 → 軸 → `BTN_TOUCH` → `SYN_REPORT`。出るとき: `ABS_PRESSURE`=0 → `BTN_TOUCH`=0 → `BTN_TOOL_*`=0 → `SYN_REPORT`。
  道具が PEN と RUBBER の間で替わるときは、古い道具を 0 にして SYN の後に新しい道具を 1 にする（同じ report でも 2 つの frame に分ける）。
- Invert が無い device（Eraser だけ）は Eraser の立ち上がりで道具を RUBBER にする。

置き場は新しい `src/drivers/usb/hid-digitizer.c`（`hid-digitizer.h`）。入力は decode 済みの値、出力は event の列の純粋な関数にして、host で試験できるようにする。

### 2.3 kernel の変更（HAL の API に触れない）

| 変更 | 場所 |
| --- | --- |
| §2.1 の code を足す（`ABS_PRESSURE`・`ABS_DISTANCE`・`ABS_TILT_X/Y`・`BTN_TOOL_PEN`・`BTN_TOOL_RUBBER`・`BTN_TOUCH`・`BTN_STYLUS`・`BTN_STYLUS2`・`MSC_SERIAL`）。すべて既存の `ABS_MAX`（0x3f）・`KEY_MAX`（0x2ff）の内 | `include/uapi/input.h` |
| parser: collection の usage の stack（application・physical）を持ち、Pen の collection の中の 0x0D の field を §2.1 に写す。Physical Min/Max・Unit Exponent・Unit を global の状態に持ち、absinfo の resolution を出す。Pen 以外の digitizer の collection（Finger・Touch Screen）は今どおり無視する | `src/drivers/usb/usb-hid.c`（`usage_to_event`・`parse_global`・`parse_main`・`add_absolute_axis`） |
| publish: pen の device は §2.2 の状態機械を通して emit | `usb-hid.c`（`usb_hid_publish_report`）、新 `hid-digitizer.c` |
| queue: 200 Hz × 約 10 event のペンでは 256 event は約 130 ms。p002 で溢れ（`SYN_DROPPED`）を測り、要れば pen の device だけ容量を上げる | `include/kern/input-queue.h`、`src/drivers/generic/input.c` |

範囲外（p002 では扱わない）: pad（ExpressKey・ring）、touch の指、Wacom の vendor の protocol（vendor page 0xFF0D や report ID 2 の独自の形式で
しか報告しない機種）。実機の機種が決まってから（§8 D1）、その機種が標準の digitizer の usage を出すかを確かめる。

### 2.4 QEMU での合成の入力

QEMU には 4096 段階の筆圧のペンが無い。**最小で安全な案: 試験用の kernel の注入の device `/dev/input/inject`**（Linux の uinput の小さな版）。

- kernel option `CONFIG_INPUT_TEST_INJECT|bool|Test input injector|amd64|n|`（`config/kernel-options.list`、既定 n、ci の config も n）。
  既定の image と release には入らない。試験の build だけ y にする。
- node は 0600 root。`write` の形は `struct input_event` の列。最初の `ioctl(INJECT_IOC_CREATE, struct input_inject_setup)` で
  名前・capability（type と code の対）・absinfo・property を宣言し、`drv_input_device_register` で普通の device を 1 つ作る。
  以後の write は `drv_input_device_emit` にそのまま渡す（宣言に無い code は input の層が捨てる）。close で unregister。
  UAPI は新 `include/uapi/input-inject.h`。実装は新 `src/drivers/generic/input-inject.c`（数百行）。
- guest の道具 `peninject`（試験の build だけ rootfs に入れる。`plan/ws079/tests/peninject/`）: 台本の file を読み、
  「`down x y p tiltx tilty`」「`move ...`」「`up`」「`button stylus 1`」「`tool rubber`」「`wait ms`」を event にして時刻どおり書く。
  pen の宣言は 4096 段階・傾き ±60 度・2 つの button・消しゴムで、実機の典型と同じ形にする。
- 経路: 注入の device → evdev → compositor → client。HID の解析（§2.1・§2.2）は注入では通らないので、**host 試験**で合成の report descriptor
  （4096 段階・傾き・消しゴム・2 button・In Range・report ID 付き）と report の列を `drv_hid_report_layout_parse` と `hid-digitizer.c` に通して確かめる。
- 補助: QEMU の `usb-wacom-tablet` を付けて起動し、USB の列挙と解析が壊れない（落ちない、既存の keyboard・`usb-tablet` が動く）ことを確かめる。
  筆圧の段階の試験には使わない。

退けた案: compositor の中の再生（kernel の経路を通らない）、QEMU の独自の USB device（QEMU の改造が要る）、HID の report を注入する uhid 型
（usb-hid.c から USB に依らない HID の核を切り出す refactor が要り、大きい。host 試験で解析を覆えるので不要）。

## 3. compositor: `zwp_tablet_manager_v2`

### 3.1 device の分類

`input.c` の `probe_device` に 1 つ足す: `BTN_TOOL_PEN` と `ABS_PRESSURE` と `ABS_X/Y` を持つ node は **tablet**（pointer にしない）。
absinfo（X・Y・PRESSURE・TILT の min/max/resolution）を開くときに読む。1 つの tablet の device が 1 つの `zwp_tablet_v2` になる。

### 3.2 protocol（tablet-unstable-v2、version 1）

| object | compositor が送るもの |
| --- | --- |
| `zwp_tablet_manager_v2`（global） | `get_tablet_seat(seat)` → `zwp_tablet_seat_v2` |
| `zwp_tablet_seat_v2` | 作られたとき、今ある tablet ごとに `tablet_added`、既知の tool ごとに `tool_added`。後から来た device・tool も同様 |
| `zwp_tablet_v2` | `name`（evdev の名前）、`id`（USB の vid・pid）、`path`（`/dev/input/eventN`）、`done`。抜かれたら `removed` |
| `zwp_tablet_tool_v2` | `type`（pen=0x140、eraser=0x141）、`hardware_serial`（MSC_SERIAL があれば）、`capability`（pressure、tilt、あれば distance）、`done`。以後 `proximity_in(serial, tablet, surface)`・`proximity_out`・`down(serial)`・`up`・`motion(x, y)`・`pressure`・`tilt`・`button(serial, BTN_STYLUS*, state)`・`frame(time)` |
| `zwp_tablet_pad_v2` | **v1 では作らない**（pad は §2.3 の範囲外）。`pad_added` を送らなくても protocol に反しない |

- tool は (tablet, 種類, serial) で 1 つ。最初に近接したときに作り、tablet が消えるまで保つ。消しゴムの端は別の tool（eraser）。
- 1 つの evdev の frame（SYN_REPORT まで）を 1 つの `frame` にまとめる。順は proximity_in → down → motion・pressure・tilt → button → up → proximity_out → frame。
- 筆圧: `p = (raw − min) × 65535 / (max − min)`、0..65535 に切る。4096 段階の 4095 は 65535。
- 傾き: resolution（単位/radian）があれば `deg = raw × 180 / (π × res)`、無ければ logical の min..max を −60..+60 度へ線形に写す。wl_fixed で送る。
- 位置: tablet の全域を出力の全面へ写す（今の `scale_absolute` と同じ、§8 D2）。surface の局所座標へ直して `motion` に。

### 3.3 focus と cursor

- tool の focus は pointer とは別。近接中は tool の点の下の surface が focus。`down` の間は down した surface へ暗黙の grab（`up` まで他へ移らない）。
  up の後、点が別の surface に居れば `proximity_out` → `proximity_in`。
- compositor の自分の UI（system bar・title bar・App Home・Wiseview・§4 の gesture・menu）は、ペンを **pointer として**扱う（下の fallback と同じ変換）。
  つまりペンの点は先に `shell.c` の hit の判定を通り、compositor が取らなかったときだけ client の surface へ行く。
- cursor: 画面の cursor は 1 つ。最後に動いた device の位置に出す。ペンが tablet を bind した client の surface の上に居る間は、
  その client の `zwp_tablet_tool_v2.set_cursor`（surface か null＝隠す）を使い、それ以外は矢印。Notes は近接中の小さな点の cursor を置く。

### 3.4 tablet を bind しない app への fallback

focus の surface の client が `zwp_tablet_seat_v2` を持たないとき、ペンを pointer として渡す:

| ペン | pointer |
| --- | --- |
| 近接中の動き | `server->pointer_x/y` を動かし `wl_pointer.motion`（今の absolute の pointer と同じ） |
| 接触（pen でも eraser でも） | `BTN_LEFT` の押し・離し |
| `BTN_STYLUS` / `BTN_STYLUS2` | `BTN_RIGHT` / `BTN_MIDDLE` |
| proximity_out | 何もしない（pointer は留まる） |

どちらで渡すかは proximity_in（か down）のときに client で決め、stroke の途中では替えない。

### 3.5 置き場

compositor: 新 `userland/desktop/wayland/tablet.c`（device の状態・tool・focus・fallback・送信）、`input.c`（分類と frame の振り分け）、
`protocol.c`（global に `zwp_tablet_manager_v2` v1）、`zwl.h`。client: `userland/desktop/libwayland/tablet-protocol.c` と
`tablet-unstable-v2-client-protocol.h`（既存の `glass-protocol.c` と同じ手書きの表）。wayland-protocols の XML の著作権（MIT）の表示を file の頭に残す。

## 4. 上の右端から左下へのスワイプ

### 4.1 定義（出力の座標、px）

W は出力の幅。App Home の左上の gesture の鏡像にし、数値も揃える。

| 項目 | 値 |
| --- | --- |
| 始まりの zone | `x ≥ W − 28` かつ `y < 28`（右上の 28×28。system bar の右端の時計の上） |
| 始まり | 左 button の押し、ペンの接触（`down`）、touch の接触（将来）のどれか。ペンの hover だけでは始まらない |
| 追う量 | `dx = start_x − x`（左が正）、`dy = y − start_y`（下が正） |
| 腕を上げる | `dx ≥ 14` かつ `dy ≥ 14` で gesture になる（`HOME_DRAG_START` と同じ）。押してから 1500 ms 以内に腕が上がらなければ取り消し |
| 方向 | 腕を上げた後、`dx` と `dy` の小さい方が大きい方の 0.36 倍以上（左下の 45 度の ±25 度）。外れたまま離せば取り消し |
| 距離で成立 | 離したとき `(dx + dy) / 2 ≥ 108`（= 360 × 0.30、Home と同じ） |
| 速さで成立 | 離したとき `(dx + dy) / 2 ≥ 40` かつ、離す前の 100 ms の対角の速さが 0.8 px/ms 以上 |
| 途中の見た目 | 右上の角から Notes の印の glass の円が `(dx+dy)/2 / 360` に比例して大きくなり点を追う。成立で Notes へ、取り消しで角へ戻る（200 ms） |

### 4.2 他の UI との共存

- `shell.c` の button の鎖で、greeter・lock・Wiseview の後、App Home の直後に置く（左上と右上で zone は重ならない）。
- 右上の 28×28 は gesture のもの。時計には押しの動作が無いので、腕が上がらずに離した押しは何もしない（今と同じ見た目）。
  network の icon は時計の左にあり、p003 で icon の hit の箱が zone と重ならないことを layout の値で確かめる（重なるなら zone を優先し icon を左へ）。
- docked の窓の control は bar の application の zone（左〜中央）で、右端とは重ならない。
- 窓が fullscreen（bar が見えない、direct scanout）でも右上の zone は gesture が取る（§8 D3）。greeter と lock の間は無効。
- 始まりの点が zone でなければ今の経路（窓の title bar の drag など）のまま。

### 4.3 成立したときの動作

1. app_id が `notes` の toplevel を最近 raise された順に探す。
2. **在る**: その窓を最前面に（Wiseview の tile の click と同じ raise の関数）、keyboard の focus を移し、compositor から
   `xdg_toplevel.configure` を fullscreen の state・出力の大きさで送る（xdg-shell は compositor からの state を許す）。Notes はそれに従う。
   既に最前面で fullscreen なら何もしない。
3. **無い**: `home_launch` と同じ経路で `/bin/notes --fullscreen` を起こす。Notes は最初の map の前に `set_fullscreen` を頼む。
   起動の待ち（5000 ms）の間は次の gesture で二つ目を起こさない。

### 4.4 入力の源

gesture は「接触の列」（開始・動き・終了、源の種類）を受ける小さな認識器にし、pointer（`zwl_shell_button`・`zwl_shell_motion` の経路）、
ペン（§3.3 で pointer として shell に回る）、touch（今は wl_touch と touch の evdev が無い。touch の device が入る WS で同じ認識器につなぐ）を同じに扱う。
置き場は新 `userland/desktop/wayland/corner.c`（home.c から角の判定の共通部分を切り出せればそうする）。

## 5. Notes（`userland/desktop/notes`、`/bin/notes`、画面の名前「Notes」）

### 5.1 文書の model

```c
struct notes_point {		/* 1 つの sample、page の座標（pt、左上が原点） */
	float x, y;
	uint16_t pressure;	/* 0..65535（§3.2 の正規化のまま） */
	int16_t tilt_x, tilt_y;	/* 度 × 100 */
	uint32_t time_ms;	/* stroke の始まりからの ms */
};

struct notes_stroke {
	uint32_t id;		/* 文書の中で一意、増えるだけ */
	uint8_t tool;		/* NOTES_TOOL_PEN（v1 はこれだけ） */
	uint32_t color;		/* RGBA8 */
	float width;		/* 基準の太さ（pt） */
	float min_ratio, gamma;	/* 太さの曲線（下） */
	struct notes_point *points;
	size_t point_count;
	float bounds[4];	/* 描画の外形の箱（消しゴム・damage 用） */
};

struct notes_page { struct notes_stroke *strokes; size_t stroke_count; uint8_t background; };
struct notes_document { struct notes_page *pages; size_t page_count; float page_width, page_height; };
```

- page の大きさは既定 A4 縦（595.28 × 841.89 pt、§8 D4）。背景は無地・罫線・方眼（v1 は無地と罫線）。
- 太さの曲線: `w(p) = width × (min_ratio + (1 − min_ratio) × (p / 65535)^gamma)`。既定 `min_ratio = 0.15`、`gamma = 0.7`。
  筆圧の無い源（mouse・fallback）は `p = 0.6 × 65535` の一定。
- sample は生の値を保つ（前の点と 0.25 px 未満は捨てる）。滑らかさは描画の側（§5.3）で出す。
- 消しゴム: **stroke** の mode は、消しゴムの円（半径 = 太さの設定）に外形が触れた stroke を丸ごと消す。**部分**の mode は、円の内の点で
  stroke を切り、外の区間を新しい id の stroke にする（境界は線分と円の交点を補間して足す）。ペンの消しゴムの端（eraser の tool）は
  toolbar で選んだ mode の消しゴムになる。
- undo: 操作の記録（`add_stroke`、`remove_strokes`（消した stroke を保つ）、`split`（消した 1 本と足した n 本）、`add_page`・`remove_page`・`move_page`）。
  逆の操作で戻す。undo・redo の stack は 1000 まで、memory の中だけ（PDF に保存しない）。

### 5.2 UI

- 全画面の canvas。1 page を画面の高さに合わせて中央に、外は薄い灰色。拡大は v1 に無い（Future）。
- 最小の toolbar: 上の中央の glass の帯（libkeiland の glass）。項目: ペン、消しゴム（もう一度押すと stroke と部分を切り替え）、
  色 5 つ（黒・青・赤・緑・橙）、太さ 3 つ、undo、redo、page の「< 3 / 12 >」、page を足す、「…」（開く・保存・名前を付けて保存・page を消す・全画面を解く）。
  fullscreen では system bar が見えないので file の操作は「…」に置き、窓のときは xdg_menu_v1（WS070 の System Menu）にも同じ model を出す。
- page の移動: toolbar の矢印、PageUp・PageDown。
- ペンが近接中は toolbar の上では描かない（押しは toolbar の button）。mouse でも描ける。
- key: Ctrl+Z・Ctrl+Shift+Z、Ctrl+S、Ctrl+O、E（消しゴム）・P（ペン）、Esc（全画面を解く）。
- 側面の button: `BTN_STYLUS` を押している間は消しゴム、`BTN_STYLUS2` は何もしない（§8 D5）。
- 窓の題: 「Notes — <file の名前>」（未保存は「Notes — 新しいノート」）。Keiland の名前は出さない。

### 5.3 描画（Vulkan）

- terminal・files と同じ `vkCreateWaylandSurfaceKHR` の WSI。
- stroke の形は共有の module `stroke-geometry.c` が作る: sample の間を centripetal Catmull-Rom で補間し（間隔 ≤ 1 px）、各点の太さ w を
  §5.1 の曲線で出し、法線の ±w/2 で帯を作る。端と継ぎ目は丸（三角の扇）。出力は (1) 描画用の三角形の列（頂点に「帯の中心からの距離 −1..+1」を持つ）と
  (2) 外形の多角形（閉じた path）。
- anti-alias は MSAA に頼らない: fragment shader で距離の属性から `alpha = 1 − smoothstep(1 − fwidth(d), 1, |d|)`。Venus と i915 の multisample の差を避ける。
- 確定した stroke は page の画像（画面の解像度の offscreen の VkImage）に焼き、変わったとき（stroke の追加・消去・page の移動・大きさの変更）だけ作り直す。
  描いている最中の stroke は毎 frame その上に描き、頂点の buffer に追記する。
- 遅れ: ペンの event を受けたら次の `wl_surface.frame` で描く。ペンの frame を貯めず、最新の点まで描く。

### 5.4 保存と開く（libpdf）

形式・metadata の名前と中身・libpdf の API は [design-pdf.md](design-pdf.md) が決める。Notes の側の流れだけをここに置く:

- **保存**: 各 page について、stroke ごとに §5.3 (2) の外形の多角形を色で塗る path として libpdf の書き出しへ渡す（画面と同じ形になる。他の viewer でも同じに見える）。
  同時に `notes_document` の全体（page の大きさ・背景・stroke の点・筆圧・傾き・時刻・道具・色・太さの曲線）を編集の metadata として libpdf へ渡す。
  `<name>.pdf.tmp` に書いて fsync、`rename` で置き換える。
- **開く**: libpdf で開き、Notes の metadata があり version が読めれば model を作り直す（page の path は読まない）。
  metadata の無い PDF（他の app の PDF）は v1（p005）では「Notes の PDF ではない」と出して開かない。他の PDF への書き込み（PDF Viewer の「書き込む」で
  一般の PDF を開く）は、libpdf の読み込み（p006）の後に、元の page を背景として描き stroke を足して保存する形で、design-pdf.md と合わせて決める（§8 D7）。
- command line: `/bin/notes [--fullscreen] [file.pdf]`。1 process 1 文書。PDF Viewer の「書き込む」は `/bin/notes <file>` を起こす。
- 閉じるとき未保存の変更があれば「保存しますか」の dialog（既存の files の dialog の見た目に揃える、§8 D6）。

## 6. 範囲外（Future Work の候補）

pad（ExpressKey・ring）、touch の指の入力と wl_touch、複数の出力への写し方の選択、Notes の拡大・蛍光ペン・図形・文字・layer・選択と移動、
ペンの回転（`ABS_Z`）・airbrush の wheel、画面上の手書きの予測。

## 7. Phase の割り振りと試験

ws.md の案を保ち、p003 を 2 つに分けることを提案する（gesture の pointer の部分は p002 に依らず、tablet の protocol と試験の形が違うため）。

| Phase | 範囲 | 依存 | 試験 |
| --- | --- | --- | --- |
| p002 | §2: UAPI の code、parser（collection の usage・Physical/Unit）、`hid-digitizer.c`、注入の device と `peninject`、queue の溢れの測定 | p001 | host: 合成の descriptor（4096 段階・傾き・Invert/Eraser・2 button・report ID）と report の列 → event の列が §2.2 の順と一致（範囲の出入り、PEN↔RUBBER の切り替え、傾きの resolution）。QEMU: 注入の build で `peninject` の台本を流し、guest の小さな読み手（`plan/ws079/tests/inputdump`）が `/dev/input/eventN` の event と absinfo を file に書き、serial.py でその file を読んで照合。`-device usb-wacom-tablet` 付きの起動で落ちない。回帰: build の warning 0、`boot-test.sh`（注入なしの既定の build） |
| p003 | §3: tablet の分類、`zwp_tablet_manager_v2`、focus・cursor、fallback、libwayland の表 | p002 | QEMU（注入の build）: 試験の client `wltablet`（tablet を bind し、受けた event を file に書く）で proximity・down・pressure（4095 → 65535）・tilt・button・frame の順を照合。fallback: terminal の上でペンの接触が click になる（画面を撮る）。App Home の左上の gesture と network の icon が今どおり動く |
| p010（新） | §4: gesture の認識器（pointer・ペン）、Notes の起動・raise・fullscreen | p003 | QEMU: 注入（ペン）と QMP の pointer の操作で右上から左下へ動かし、Notes の代わりの試験用の client（app_id `notes`）が起動して fullscreen になる（画面を撮る、serial.py で process 数を確かめる）。2 回目で二つ目が起きず raise される。否定: 短すぎ・方向違い・zone の外・1500 ms 超えは起動しない。p005 の後に本物の Notes で再び |
| p005 | §5: Notes v1 | p003、p004、（p010 は gesture の確認だけに要る） | host: model（undo/redo の往復、部分消しの切断と境界の点、太さの曲線）、`stroke-geometry.c`（外形の閉じ方・幅）、保存 → 開く の往復で model が一致（libpdf の host build）。host の外部 viewer（`pdftoppm`・`qpdf --check` が host に在れば、無ければ未実施と書く）で保存した PDF が描ける。QEMU: `peninject` で筆圧 0→4095 の直線を描き、太さが変わることを画面で確かめ、消しゴム・undo・page を足す・保存・開き直しで同じ画面 |

実機: ペンの機種が決まるまで p002・p003 の実機の確認は未実施（§8 D1）。QEMU の証拠と実機の証拠は分けて記録する。

## 8. ユーザーの判断が要る点（既定の案つき）

| # | 問い | 既定の案 |
| --- | --- | --- |
| D1 | 実機のペンタブレットの機種 | 未定。決まったら標準の HID digitizer の usage を出すか確かめる。Wacom の vendor の protocol だけの機種なら、その機種の quirk を別の Phase に |
| D2 | tablet の範囲の写し方 | 出力の全面へ（縦横比は合わせない）。縦横比を保つ（余りを捨てる）mode は後で |
| D3 | 全画面の app の上でも右上の gesture を効かせるか | 効かせる（greeter・lock を除く）。全画面の app は右上の 28×28 から始まる押しだけを失う |
| D4 | Notes の page の既定の大きさ | A4 縦。画面の比率の page や無限の canvas は後で |
| D5 | ペンの側面の button | button 1 を押している間は消しゴム、button 2 は無し |
| D6 | 保存の方針 | 明示の保存と、閉じるときの確認。自動保存（例 `~/Documents/Notes/`）はしない |
| D7 | 一般の PDF への書き込み（PDF Viewer の「書き込む」） | p005 は Notes の PDF だけ。一般の PDF は p006 の後、design-pdf.md と合わせて決める |
| D8 | keyboard から Notes を出す shortcut（例 Super+N） | 作らない（要るなら p010 に足す） |

## 追記: 画面の端のジェスチャーの整理（2026-09-28 ユーザー）

ユーザー:「左上からスワイプして出したホーム画面は、下端から上にスワイプするとデスクトップに戻る。また、右上からスワイプすれば、ノートに移動する。
ノートは今のところただのアプリなので、下端からスワイプすると、デスクトップの既定の動作で、ウィンドウリストが出る。この整理にしたいです。」

| 今の画面 | 左上から右下 | 右上から左下 | 下端から上 |
| --- | --- | --- | --- |
| デスクトップ（アプリの窓、Notes の全画面を含む） | Home を開く | Notes（起動・最前面・全画面） | Wiseview（窓の一覧、今の既定。`WISEVIEW_EDGE` 20 px） |
| Home | （開いている） | Home を閉じて Notes へ | **Home を閉じてデスクトップへ**（Wiseview は開かない） |

整合のための決まり（main）:
- 下端のジェスチャーは今の画面で意味が変わる: Home の上では「閉じる」、それ以外では Wiseview。Home が出ている間は Wiseview を開かない。
- Home の今の閉じ方（右下から左上へのドラッグ、残ったデスクトップの端のクリック、Esc 等）はそのまま残す。
- 右上のスワイプは Home の上でも効く（Home を閉じる動きと Notes の全画面を続けて見せる）。Notes が既に最前面・全画面なら何もしない。
- 端のジェスチャーは端の数 px から始めたものだけにする。Notes で pen が紙の下の端の近くを書いても、紙の中から始まった線は Wiseview にならない
  （pen の入力は p003 の後に同じ判定に入れる。書き込み中の誤動作は実機の pen で確かめる）。
- 右上の 28 px の範囲はシステムバーの右端の操作（時計・電池・network）と重ならないことを確かめる（p010）。
- main の判断（2026-09-28、p010 の報告）: 全画面の窓の上でも端のジェスチャー（左上 28×28・右上 28×28・下端 20 px）を compositor が取る
  （ユーザーの表の「全画面の Notes の上で Home・Wiseview」に合わせる。D3 を右上から 3 つの端に広げた）。Home の上で Notes が既に最前面・全画面なら、
  右上のスワイプは Home を閉じるだけ。ジェスチャーの表示の間は全画面の直接の scanout を離れて合成する（Venus で 400〜1100 ms の切り替え、i915 は未計測）。
