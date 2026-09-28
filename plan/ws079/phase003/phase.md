<!-- awesome-plan project=zedbsd record=ws079-p003 -->

# WS079 Phase 003: compositor の `zwp_tablet_manager_v2` と pointer の fallback

<!-- awesome-plan-current:start -->
Status: in-progress（2 回目の区切り: compositor の tablet・fallback・libwayland・試験の client まで実装し、QEMU の guest で確かめた。実機は未実施。clearance は main の判断）
Disposition: normal
Parent: [WS079](../ws.md)
Design: [design-input-notes.md](../design-input-notes.md) §3
Resume point: 下の「残り」
<!-- awesome-plan-current:end -->

## 範囲

[設計](../design-input-notes.md) §3: tablet の分類、`zwp_tablet_manager_v2` v1（seat・tablet・tool、pad なし）、focus と暗黙の grab、
tablet を bind しない client への pointer の fallback、compositor の自分の UI はペンを pointer として扱う、libwayland の client の表。
加えて（main の依頼）p002 の試験の経路（注入の device を guest で確かめる）を先に仕上げる。gesture（p010）は範囲外。

## 2026-09-28 の作業（subagent、1 回目: p002 の試験の経路）

### 変更

| 場所 | 内容 |
| --- | --- |
| `userland/base/tests/peninject/`（`plan/ws079/tests/peninject/` から移した） | 他の試験の program（`userland/base/tests/*`）と同じく `ZEDBSD_USERLAND_PACKAGE` で登録（`peninject`、platform `*`、既定 n、分類 base、`/bin/peninject`、台本は `/usr/share/peninject/stroke.pen`）。userland の通常の link の規則（`AMD64_USER_BASIC_COMMAND`）で link される。program を 3 つの mode にした: 台本の再生（従来）、`-c`（拒否の確認）、`-d MS`（名前「Test pen (input-inject)」の evdev の node を探し、名前・5 軸の absinfo・event を印字）。台本に `hover`・`lift` を足し、`stroke.pen` は `size` の後に 1.5 秒待つ（読み手が node を開く間） |
| `plan/ws079/tests/config-amd64-pen.mk`・`build-pen-image.sh`・`pen-guest.sh`（新） | 試験の image: lean な Venus の image（`plan/tools/titlebar/config-amd64-menu.mk`）に `CONFIG_INPUT_TEST_INJECT := y` と `peninject`。guest は runtime `build/ws079-run` |

### 確認（実行したもの、QEMU の guest。実機は無い）

| 確認 | 結果 |
| --- | --- |
| `plan/ws079/tests/build-pen-image.sh`（worktree の `build/amd64`。sysroot は共有の `build/amd64/sysroot`（09-28 12:53）の写し。`make toolchain` は worktree で LLVM の source の展開を始めるため使わない） | exit 0。`build/amd64/bin/peninject`（warning 0、`-Werror`） |
| guest（Venus、`pen-guest.sh start`）: `ls -l /dev/input-inject` | `crw------- root wheel`（0600） |
| `peninject -d 9000 & peninject /usr/share/peninject/stroke.pen`（root） | 再生 exit 0。node は `/dev/input/event4`、名前「Test pen (input-inject)」。absinfo: X 0..21600、Y 0..13500、PRESSURE 0..4095（res 0）、TILT_X/Y −60..60（res 57）。ramp の値の列 0,63,127,…,4031,4095（64 段、刻み 63〜64）、`BTN_TOOL_PEN`=1 → 軸 → `BTN_TOUCH`=1、`BTN_STYLUS` 1/0、up で touch 0 → tool 0、`BTN_TOOL_RUBBER`=1 で touch 1/0 → tool 0。計 182 event、injector を閉じると読み手の read が 0（EOF）で終わり、node が消える。出力 `build/ws079-p003/pendump.txt` |
| `peninject -c`（root） | 15 件すべて ok（`build/ws079-p003/pencheck.txt`）: 非 root の open は node の mode（0600）で **EACCES**。node を 0666 にすると driver が **EPERM**（zedBSD の errno 47）。2 つ目の open が **EBUSY**。setup の面積 0・magic 違い、pressure 4096、tilt 61、X が面積の外、EV_REL、BTN_LEFT、button の値 2、端数の write、65 event の write が **EINVAL**。拒否の後も正しい event（pressure 4095）は通る |

設計との差: 依頼の「非 root は EPERM」は、実際は devfs の mode（0600）が先に EACCES で断り、driver の EPERM はその後ろの二重の守り。
両方を確かめた（`-c` は chmod 0666 にして driver の EPERM を見てから 0600 に戻す）。

未実施（p002 の残りのうち）: QEMU の `usb-wacom-tablet` を付けた起動、INPUT_PROP の判断、arm64・pcat の build。

## 2026-09-28 の作業（subagent、2 回目: compositor と libwayland）

### 変更

| 場所 | 内容 |
| --- | --- |
| `userland/desktop/wayland/tablet.c`・`tablet.h`（新） | §3 の本体。tablet は最大 4、tool は tablet ごとに pen 先と消しゴム端の 2 つ。`zwl_tablet_add`（absinfo・名前・USB の vid/pid を読み、全 tablet seat に `tablet_added` → name・id（USB のときだけ）・path・done）、`zwl_tablet_remove`（tool・tablet に removed）、`zwl_tablet_frame`（1 report = 1 回の配送）、`zwl_tablet_request`（manager: get_tablet_seat・destroy、seat・tablet: destroy、tool: set_cursor・destroy）、`zwl_tablet_object_gone`（surface が消えたら proximity_out、touch は up なしで捨てる）。tool は最初の近接で全 seat に `tool_added` → type（0x140/0x141）・capability（pressure 2、tilt 1）・done。後から作られた seat には既知の tablet と使われた tool を送る |
| 同 | 配送: ペンは常に pointer を動かす（tablet 全域 → 出力全面、`place_pointer`）。hover は先に `zwl_seat_motion_shell`（lock・DnD・resize・glass の移動や画面）に通し、取られなければ `zwl_seat_pointer_update` の surface の client が tool を持ち、点が surface の上なら tablet protocol（proximity_in/out、motion、pressure、tilt）。持たない client には `wl_pointer.motion`。touch の開始は先に `zwl_seat_button_shell(BTN_LEFT)`（system bar・title bar・App Home・Wiseview の端・desktop の端など）に通し、取られれば pointer の経路、取られなければ tablet の client に `down`（暗黙の grab: up まで同じ surface、surface の外の座標も送る）か、他の client に BTN_LEFT。barrel button は tablet の client に `button`（BTN_STYLUS/BTN_STYLUS2）、他には BTN_RIGHT/BTN_MIDDLE。経路は touch の開始で決め、終わるまで替えない。pressure は `(raw−min)×65535/(max−min)`、tilt は resolution（単位/radian）があれば度に、無ければ min..max を ±60°、wl_fixed。同じ値の pressure・tilt は再送しない（proximity_in の直後は全軸を 1 度送る）。1 report の event は 1 つの `frame` で閉じる（tool が離れるときは up と proximity_out を 1 frame に）。`set_cursor` は tool がその client の surface の上にある間だけ効き、離れると矢印に戻す |
| `userland/desktop/wayland/seat.c`・`zwl.h` | `zwl_seat_motion`・`zwl_seat_button` を compositor の部分（`zwl_seat_motion_shell`・`zwl_seat_button_shell`、取ったかを返す）と client への配送（`zwl_seat_motion_deliver`・`zwl_seat_button_deliver`）に分けた（元の 2 関数は両者を順に呼ぶだけで振る舞いは不変）。`zwl_kind` に `ZWL_TABLET_MANAGER`・`ZWL_TABLET_SEAT`・`ZWL_TABLET`・`ZWL_TABLET_TOOL`、`zwl_object` に `tablet_seat_number`・`tablet_slot`・`tool_slot`、`zwl_input_device` に `tablet` |
| `userland/desktop/wayland/input.c` | 分類: `BTN_TOOL_PEN`＋`ABS_PRESSURE`＋有効な `ABS_X/Y` の node は tablet（pointer にしない、`attach_tablet`）。SYN_REPORT で tablet は `zwl_tablet_frame` へ。close・cleanup で `zwl_tablet_remove`。tablet がある間は wl_seat の capability に pointer（fallback のため） |
| `protocol.c`・`objects.c`・`Makefile` | global 19 `zwp_tablet_manager_v2` v1 と dispatch、surface の破棄で `zwl_tablet_object_gone`、`tablet.c` を source に |
| `userland/desktop/libwayland/tablet-protocol.c`（新）・`include/libc/wayland/tablet-unstable-v2-client-protocol.h`・`include/libc/tablet-unstable-v2-client-protocol.h`（新）・`exports.map`・`Makefile` | client の表と request の wrapper（manager・seat・tool・tablet、pad は `pad_added` が名指す interface の記述だけ）。event は generic dispatch。`API-PROVENANCE.md` に pinned の記述（Debian wayland-protocols 1.44-1 の `tablet-unstable-v2.xml`、SHA-256 `db291b57…294a9`）と MIT の表示 |
| `userland/base/tests/tablet-probe/`（新）・`platform/amd64/vmunix.mk` | 試験の client（amd64、既定 n）。tablet を bind して全 event を `TABLETPROBE ...` で記録し、touch を筆圧に比例した点で描く（pen は青、消しゴムは赤）。`--pointer` は tablet を bind せず wl_pointer を記録（黒い点）。dynamic の link の規則を seat-probe と同じ形で足した |
| `userland/base/tests/peninject/main.c` | 規約の全文に合わせた（台本の語を表から enum にし、条件の中の関数呼び出しと goto を無くした）。振る舞いは不変 |
| `plan/ws079/tests/p003-scripts.py`・`p003-guest.sh`（新）、`pen-guest.sh`（`test` 命令）、`config-amd64-pen.mk`（`tablet-probe`） | guest の試験。画面の座標から peninject の台本を作り、compositor（glass、1280x800）を起こして段ごとに確かめ、画面を撮る |

### 確認（実行したもの。すべて QEMU の Venus の guest。実機の証拠は無い）

| 確認 | 結果 |
| --- | --- |
| `plan/ws079/tests/build-pen-image.sh`（最終の tree） | exit 0。自分の code は `-Werror` で warning 0（log の warning は openssl・openssh の既存のもの） |
| `plan/tools/style-check.py`（tablet.c・input.c・seat.c・tablet-protocol.c・tablet-probe・peninject）、`git diff --check` | 残りは短い対称の条件演算子 3 つだけ（規約で許される形）、whitespace の誤り無し |
| `peninject -c`・`-d`（最終の image） | 15/15 ok、dump は 1 回目と同じ（182 event、node 名・absinfo 一致） |
| `plan/ws079/tests/p003-guest.sh build/ws079-p003`（tablet・pointer・home・terminal） | status=0、28 件の log の照合がすべて ok、compositor・client の log に ERROR/FAILED/protocol error 無し（`build/ws079-p003/p003-guest.txt`・`tablet.log`・`pointer.log`・`zdesktop-input.txt`） |
| 　tablet（`tablet-probe`） | tablet の name「Test pen (input-inject)」・path `/dev/input/event4`・done、tool type 0x140 と 0x141、capability 2 と 1、proximity_in（tablet と surface がこの client のもの）、down 3・up 3、pressure 0 → 1632 → … → 65535（4095 → 65535）、tilt −30.16°〜+30.16°（raw ±30、res 57/radian）、button 0x14b 1/0、proximity_out、frame 81。暗黙の grab: 窓の右端の近くで down して窓の外へ動かすと motion x が 830（窓幅 640 の外）まで同じ surface に届き、lift で proximity_out。injector を閉じると tool removed ×2・tablet removed |
| 　pointer（`tablet-probe --pointer`） | pen の touch で `button=0x110` 1/0、barrel 1 で `0x111` 1/0、barrel 2 で `0x112` 1/0、motion 23 |
| 　home | pen で system bar の launcher を tap すると App Home が開く（compositor の UI は pen を pointer として扱う） |
| 　select（`SELECT_ARGS="188 262 168" p003-guest.sh build/ws079-p003 select`、terminal） | pen の drag で「kei pen」が選択され、barrel 2（BTN_MIDDLE）で primary selection が prompt に貼られた |
| 回帰: `pen-guest.sh test plan/ws035/tests/zdesktop-p076.sh build/ws079-p003/p076`（QMP の mouse で popup・toplevel の move・resize・dock） | PASS（52 件）。seat.c の分割の前後で mouse の経路は変わらない |

画面（QEMU、Venus）:
- `build/ws035-shots/ws079-p003-20260928-tablet.png`（筆圧で太くなる青の線、消しゴムの赤の線、窓の端で切れる grab の点、cursor は pen の位置）
- `build/ws035-shots/ws079-p003-20260928-pointer.png`（tablet を bind しない client に pen の drag が BTN_LEFT の線として届く）
- `build/ws035-shots/ws079-p003-20260928-home.png`（pen の tap で開いた App Home）
- `build/ws035-shots/ws079-p003-20260928-terminal.png`・`-select.png`（terminal で pen の選択と中 button の貼り付け）

### 設計からの差・判断

- tool の focus は「pointer の surface」（`zwl_seat_pointer_update` が選ぶ focus の窓か、その下の sub-surface）で、点がその surface の矩形の中にあるときだけ proximity_in。focus でない窓の上の hover は tablet の event を出さない（pointer と同じ規則。focus を移すのは click で、glass の shell が raise する）。
- tablet の client の surface の外（title bar・desktop）に pen があるときは、その client に pointer の motion も送らない（tablet を bind した client は pen を tablet としてだけ受ける）。
- `BTN_LEFT` の押下の bit（`buttons_down`）は tablet の経路でも shell の関数が立て、up で下ろす（client の `xdg_toplevel.move/resize` の serial には down の serial を記録）。
- pad、`hardware_serial`（MSC_SERIAL は p002 で未実装）、distance、cursor-shape の `get_tablet_tool_v2` は範囲外のまま（cursor.c は tablet tool を受けない）。
- 端の gesture との関係（design-input-notes の追記、main の決まり）: pen の touch は先に shell に通るので、Wiseview の下端 20 px・desktop の左右の端から始めた touch は今の pointer と同じく gesture になり、紙の中から始めた線は client のもの。右上の gesture（p010）は同じ `zwl_seat_button_shell` の鎖に入れれば pen にも効く。

### main の merge（p010 の右上のスワイプ）との統合

main を merge すると `seat.c` が衝突した（p010 は `zwl_seat_motion`・`zwl_seat_button` に全画面の窓の上の端の gesture（`zwl_glass_edge_motion`・
`zwl_glass_edge_button`）と `server->input_time` を足していた）。端の gesture の block は `zwl_seat_motion_shell`・`zwl_seat_button_shell` の末尾に
「取った」（1 を返す）として入れ、`zwl_seat_button_shell` は event の時刻を引数に取るようにした（`input_time` のため。tablet.c の呼び出しも直した）。
merge 後の tree で image を作り直し、同じ試験を全部やり直した: `p003-guest.sh`（28 件 ok、status=0）、`select`、`peninject -c`/`-d` は merge 前の最終の run で、
`zdesktop-p076.sh` PASS（52 件）。加えて `p003-guest.sh build/ws079-p003 corner`: pen で右上（1270,10）から左下へ 160 px 動かすと
`ZWL CORNER press source=pointer`・`armed`・`commit via=distance progress=159`（試験の image に `/bin/notes` が無いので `notes missing`）。pen は shell の
経路を通るので p010 の認識器に pointer として届く。source を pen として渡す（`ZWL_CONTACT_PEN`）のは p010 の resume の項目のまま（この phase では gesture に触れない）。

### 未実施・制限

- 実機（10 インチの LCD と AES の pen、未着）: 未実施。INPUT_PROP_DIRECT/POINTER（p002 の残り 3）の扱いも未決定（今は全 tablet を出力の全面に写す、D2 の既定）。
- 複数の tablet・複数の出力、pen と mouse の同時使用の細部、SYN_DROPPED の直後の状態の回復（report を捨てるだけ）。
- arm64・pcat の build（tablet.c は compositor と同じく amd64 だけ）。
- sysroot: worktree の `build/amd64/sysroot` は共有の写しに新しい header 2 つを手で足した（`make toolchain` は worktree で LLVM の source を展開し直すため使っていない）。main の sysroot は header の directory を丸ごと写すので、次の sysroot の build で入る。

## 残り（resume の条件）

1. main の review と clearance の判断（QEMU の証拠だけ）。
2. 実機の pen が届いたら: HID の descriptor（p002）と、この phase の経路を実機で確かめる。
3. p010（右上の gesture）が pen の touch を同じ shell の鎖で受けることの確認は p010 で。
