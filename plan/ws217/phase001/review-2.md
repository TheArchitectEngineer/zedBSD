<!-- awesome-plan project=zedbsd record=ws217-p001-review-2 -->

# ws217-p001 第 2 版の敵対的レビュー（review-2）

- 対象: [phase.md](phase.md) 第 2 版（2026-10-13 P2）と [ws.md](../ws.md) の「ユーザーの決定」。照合した source は main `5f096bb42`（設計が引く `a07ad4cc7` は main の祖先。`a07ad4cc7..5f096bb42` の差分は WiFi と plan だけで、`userland/desktop`・`src/drivers/generic`・`include/uapi` は同じ）。
- 文書・source は変えていない。`.internal/` は読んでいない。build・QEMU は流していない（設計の review のため）。
- 重大度は review-1 と同じ: 致命的（このまま実装すると壊れる・直すまで p002 に入れない）／重大（受け入れ・規則・主要な経路を外す）／中（局所の誤り・欠落）／軽微。

## 判定（p002 を始めてよいか）

**このままでは不可。第 3 版で C3・C4 と、p002 に入る中の M1〜M4 を直せば始めてよい**（どれも文の直しで済み、新しいユーザーの判断は要らない）。S7〜S9 と M5〜M10 は p003〜p006 の範囲なので、その段に入る前に直せばよいが、同じ版で直すのが安い。ユーザーに出す判断は 2 つ: S8（mirror の中の結び付けを写すか制限にするか）と M8（WS141 p015 の前の Pi で回転を許すか）。review-1 の 32 件の対応は、下の表のとおり大半が source と合う。

## 致命的

### C3. turn pass の swapchain の render pass を `compose_pass` と同じにすると、部分 frame ごとに画面が壊れる（§4.2 の 6）

- 根拠: §4.2 の 6 は turn pass を「swapchain の画像（`initialLayout UNDEFINED`→`PRESENT_SRC_KHR`、既存の `compose_pass` と同じ）」で、描く所を「物理の damage に scissor で限る」とする。`compose_pass` は `loadOp = CLEAR`・`initialLayout = UNDEFINED`（`wayland/compose.c:1715-1724`）。今の経路は部分 frame に `pass_load`（`LOAD`、`PRESENT_SRC_KHR`→`PRESENT_SRC_KHR`、`backdrop.c:318-327`）を使い、`renderArea` を damage の box にする（`compose.c:2789-2802`）。
- 起こること: `renderArea` が全体なら、CLEAR が全体を背景色で塗り、scissor の中だけ描き直すので、部分 frame のたびに damage の外が背景色になる。`renderArea` を box にしても、box の中の矩形の外（2 つ以上の矩形の間）が背景色になる。さらに `UNDEFINED` からの layout の遷移は画像の全体の内容を捨ててよいので、圧縮や tile の内部の形を持つ driver では box の外も壊れる。age の帳簿（§4.3）が前提にしている「swapchain の画像は前の内容を保つ」が成り立たない。
- 直し方: turn pass は「`turn.whole` か `compose_region()` が 0（全体）」の時だけ `compose_pass`（CLEAR）、部分の時は `pass_load`（LOAD、`renderArea` = 物理の damage の box、scissor は矩形ごと）と書く。acquire の semaphore の待ちは両方の pass の外部 dependency（`COLOR_ATTACHMENT_OUTPUT`）で足りる。§11.2 の 2 の `SHOT partial` で「damage の外に背景色の帯が無い」を確かめる項目にする。

### C4. 保存した回転を起動時に当てる所が glass の画像を作った後なので、壁紙が引き伸ばされ、壁紙を変えると mapped memory の外へ書く（§4.6「起動時」・§4.6 の 0・§8）

- 根拠: `kwl_compose_open` は `compose_display`（native の大きさで `server->width/height` を決める、`compose.c:114, 1381-1385`）の直後に `kwl_glass_open` を呼ぶ（`compose.c:158-165`）。glass は壁紙と blur の host image を `server->width/height` で作る（`glass.c:766, 1159`）。設計は回転を `compose_output_open_parked` の `compose_targets` の前（`display.c:607-613` → `compose.c:385`）で当てるので、その時には glass の画像はもう物理の大きさで出来ている。glass の作り直し `kwl_glass_resize` の呼び手は `output_resized` だけ（`output-switch.c:668`）で、open の経路は呼ばない。壁紙を後で変えると、loader の大きさの一致（`glass.c:730-731`）が外れて `wallpaper_draw` が**今の** `server->width/height` で行を書く（`glass.c:843`）。
- 起こること: `rotate=KEY 90` を保存した 1920x1080 の panel で起動すると、`server->width/height` は 1080x1920、壁紙の画像は 1920x1080 のまま。glass の uv は `server->width` を基準にする（`glass.c:2761-2764`）ので、起動直後から壁紙が縦横に引き伸ばされる。Settings で壁紙を変えると、1920x1080 の mapped image に 1080 px × **1920 行**を書き、確保した memory を越える（compositor の memory 破壊）。完了の条件 1（再起動後も保たれる）の一番普通の経路で起こる。同じことは、game mode の中の `set_rotation`（§4.6 の 0: config だけ書き、次の open で当てる）と、output switch・hotplug の reopen（新しい display の key の `rotate=` を open で当てる）でも起こる。今の switch は `width != old_width` の時だけ `output_resized` を呼ぶ（`output-switch.c:257-259`）が、§1.1 はこの比較を「mode の読み手」に分類しており、mode 同士の比較に直すと、同じ mode で回転だけ違う display への switch で `output_resized` が呼ばれない。
- 直し方: (a) 起動時の回転は `compose_display` の中、`display_name` が決まった後（`compose.c:1358`）で config を読んで（`kwl_heads_config_load` をここへ移す）当て、glass・scene・layers を最初から論理の大きさで作る。(b) open で回転を当てる全部の経路（game mode の後の `enter_window_mode`、switch、reopen）は、open の前と後の**論理**の大きさを比べ、違えば `output_resized` 相当（glass の作り直し、窓の refit、pointer、`kwl_outputs_changed`、`kwl_input_bindings_resync`、`kwl_displays_tell`）を必ず走らせる、と §4.6 に書く。`output-switch.c:257-259` の判定は「論理同士」として §1.1 の表から外す。(c) p002 の T1 の 1 に「`KWL GLASS` の壁紙の大きさの行が論理の大きさ」と「回転したまま壁紙を変える」を足す（`--rotate` も `--file` も同じ所で当てるなら、2 つの比較ではこの誤りは見えない）。

## 重大

### S7. touch を head に結ぶと、touch.c の anchor を前提にした判定が誤って働く（§5.2・§5.3・p006）

- 根拠: touch.c は指の位置を anchor の大きさで判定・制限する: keyboard の zone（`touch.c:1019-1021`）、edge の group（`touch.c:1608, 1613, 1758-1761, 1848`）、shell へ渡す点の clamp（`touch.c:2082-2085`、`server->width - 1` まで）。角の gesture も anchor（`corner.c:632-635`）。§5.2 は「画面の端の判定は論理のまま」とだけ書き、§5.3 の p006 の範囲は `kwl_pointer_warp_to(slot)` だけ。
- 起こること: 右側の head（例: 縦置きのペン LCD）に結んだ指で drag すると、shell の点が anchor の右端に clamp され、窓の move も選択も head の上で動かない。head の左の縁の指は anchor の右の縁の swipe にも当たらず、head の上で keyboard の zone・edge の gesture は働かないか、anchor の座標で誤って働く。完了の条件 6 の「結び付け」の主な使い方（LCD を 2 台目にして指と pen を使う）で起こる。§11.2 の 6 は「`KWL TOUCH` の座標が head の矩形に入る」しか見ないので、drag の clamp は見えない。
- 直し方: contact が始まった時の結び付けの slot と、その出力の論理の矩形を contact に持ち、clamp・edge・keyboard の zone・角をその矩形で判定する（head では edge の gesture を出さない、と決めてもよい。どちらかを書く）。p006 の範囲と変える file（`touch.c`・`corner.c`・`edge.c`）に足し、§11.2 の 6 に「head の上の drag で窓が動く」「head の縁の swipe の扱い」を足す。

### S8. mirror の中の結び付けを anchor に写すと、各 display が自分の回転を持つ mirror で pen と指の位置が別の所になる（§5.3）

- 根拠: §5.3 は「key の display が mirror の時は anchor」に写す。mirror の head は anchor の論理の絵を head の論理に fit して、head の回転で表示する（§4.4、fit は `heads.c:1199-1214`）。ユーザーの確認は「mirror でも各 display が自分の向きを持つ」。
- 起こること: 横置きの内蔵 panel を anchor、縦置きのペン LCD を mirror の head にすると、LCD の pen の生の値を anchor の物理（横）と anchor の回転（0）で写すので、LCD の上で縦に動かすと cursor が横に動き、letterbox の分もずれる。今（回転無し）でも letterbox の分はずれるが、回転で軸まで入れ替わる。
- 直し方: mirror の head に結んだ direct の device は「生 → head の物理 → head の回転の逆 → head の論理 → fit の矩形の逆写像 → anchor の論理」で写す（`kwl_output_absolute` に mirror の分岐）。作らないなら「mirror では結び付けは効かない（anchor へ）」を製品の制限として D に出す（推奨は写す。§11.2 の 6 に mirror の 1 件）。

### S9. head の回転（§4.6 の 7）が入力の cancel・窓の refit・head が動いた時の窓と pointer の追従を飛ばす（p005）

- 根拠: §4.6 の 7 は head の時「1・2・4、配置の再検査、6」で、3（touch の cancel・grab の終了）と 5（`output_resized`）が無い。§4.4 は配置が無効になると `kwl_displays_place_right` で head を置き直すが、置き直した head の上の窓と pointer を動かす `heads_windows_follow`（`heads.c:617-619` の apply の経路は呼ぶ）を書いていない。
- 起こること: head を 90° にすると、その head で最大化・fullscreen の窓は横の大きさのまま、head に結んだ指は古い軸の座標で続き、anchor の回転で head が置き直されると head の上の窓が plane のどの出力の上でもない所に残る（届かない窓）。完了の条件 2 の head の分を外す。
- 直し方: head の回転にも「その head に結んだ device の cancel」「その head の上の窓の refit」を足し、置き直しの前後の矩形で `heads_windows_follow` を呼ぶ、と §4.4・§4.6 の 7 に書く。§11.2 の 5 に「head の上の最大化の窓」「anchor の回転で head が動いた時の窓」を足す。

## 中

### M1. `--width/--height` の書き手は `kwl_output_size_set()` を呼べない（§4.1、C2 の直し）

- 根拠: 引数は `kwl_compose_open` より前に読まれ（`main.c:411-436`）、`compose` は `kwl_compose_open` の中で `calloc` される（`compose.c:99-103`）。`compose->mode_*` を書く関数は引数の読みの時に呼べない。既定の大きさ `server.width = 320`・`server.height = 240`（`main.c:104-105`）も §1.1 の「書く所（全部）」に無い。
- 直し方: `--width/--height` は「頼まれた大きさ」（例 `server->size_width/size_height`）に書き、`compose_display`（`compose.c:1381-1385`）で native か頼まれた大きさを mode にして `kwl_output_size_set()` に通す。p002 の受け入れの grep は `server.width =` の形も含め、既定値と頼まれた大きさの field を表に入れる。

### M2. `output_resized` が glass を「先に捨てて作り直す」ので、§4.6 の 1 と 5 が矛盾する。backdrop は先に作れない（§4.6、M6 の直し）

- 根拠: §4.6 の 5 が呼ぶ `output_resized` は `kwl_glass_resize` を呼び（`output-switch.c:666-670`）、これは quiesce と `vkDeviceWaitIdle` の後に古い壁紙と blur を**先に捨て**、作り直して CPU で描き直す（`glass.c:565-597`）。§4.6 の 1 で先に作った壁紙は捨てられ、同じ CPU の描きが 2 回走り、作り直しが失敗すると壁紙の無い glass になる（M6 の「何も変わらない」が成り立たない）。backdrop は M の record の中で最初の使用の時に作られ（`backdrop.c:68-80`）、§4.6 の 1 の「先に作る」の対象にならない。`kwl_backdrop_destroy` は共有の `pass_load` も壊す（`backdrop.c:180-184`）。
- 直し方: `output_resized` を「glass を作り直すか」で分け、回転では 1 で作った壁紙・blur を 4 で入れ替えて glass の作り直しを飛ばす。backdrop は 4 で古い 2 つの target（pass は残す）を R の step 1 の破棄に回して `BACKDROP_UNTRIED` に戻し、次の record で新しい大きさで作る（失敗は今どおり `BACKDROP_FAILED`、致命的でない）と書く。

### M3. turn.image に描く pass の選び方が書かれていない（§4.2 の 2・3）

- 根拠: 今の code は `compose->pass`・`compose->pass_load` を直に選ぶ: frame の始め（`compose.c:2789-2791`）、scene の写しの後の再開（`compose.c:2876`）、backdrop の後の再開（`backdrop.c:127`）。§4.2 は `turn_clear_pass`・`turn_load_pass` を作り、layout・画像・大きさの helper を足すが、この 3 か所で pass を取り替える helper が無い。§4.2 の 3 が挙げる `backdrop.c:326-327` は `pass_load` の作成で、barrier ではない。
- 起こること: どこかに `pass_load`（`PRESENT_SRC_KHR` の layout）が残ると、`SHADER_READ_ONLY_OPTIMAL` の turn.image を違う layout で開く（未定義の動作。i915・venus では見えないことがあり、T1 で検出できない）。
- 直し方: `compose_target_pass(compose, slot, load)` を足し、上の 3 か所と head の `heads_record_extended`（`heads.c:1262`）をこれから取る、と §4.2 の 3 に書き、p002 の受け入れの grep に `compose->pass\b`・`pass_load` を足す。

### M4. `KL_SYSTEM_SINCE_ROTATION 34` の 1 つの版に、p004 と p006 の 2 つの段で違う interface を入れている（§6.1・§6.2・§12）

- 根拠: §6.1 は版 34 に requests 7・events 5（`set_rotation` と `bind_touch`・`listen_touch`、`rotation` と `touch`）を置くが、§12 は p004 で `set_rotation`・`rotation`、p006 で残りを入れる。zedBSD の libwayland は interface の表に無い opcode の event を `EPROTO` にする（`libwayland/wire.c:897-899`）。`KL_VERSION` も 93 の 1 つに p004 の getter と p006 の `kl_system_touches_*` を入れている（§6.2）。
- 起こること: p004 の後・p006 の前の libkeiland（版 34・events 4）と p006 の後の compositor（版 34 に `touch` の event 4 を送る）が出会うと、Settings の接続が切れる。1 つの image の中では同じ build なので起こりにくいが、Linux・FreeBSD の package（WS108・WS109）で libkeiland と compositor の版がずれると起こる。公開の API の版も「93 の API」が段ごとに変わる。
- 直し方: どちらか。(a) p004 で版 34 の wire を全部（requests 7・events 5、`KL_VERSION` 93 の全 API）決めて入れ、compositor は p006 まで `bind_touch`・`listen_touch` に UNSUPPORTED を答え `touch` を送らない。(b) 結び付けに別の版（`KL_SYSTEM_SINCE_TOUCH_BIND` 35、`KL_VERSION` 94）を Q1 が割り当てる。§6.1・§6.2・§12 を揃える。

### M5. identity の UNIQ を整えていない（§5.3）

- 根拠: §5.3 は NAME の空白だけを `_` に置き換え、UNIQ（`EVIOCGUNIQ`）はそのまま使う。USB の unique_id は serial の string descriptor そのもの（`usb-hid.c:856-863`）で、空白や制御文字を含み得る。§6.4 は空白を含む identity の `touch=` の行を無効として捨てる。
- 起こること: serial に空白がある LCD は結び付けが保存できず、再起動で毎回既定に戻る（完了の条件 6 の「再起動後も保たれる」を外す）。
- 直し方: UNIQ も NAME と同じく印字できる ASCII の空白以外に限り、他は `_` にする。長さ 96 で切った時の衝突は制限として書く。host 試験（`host-identity`）に空白・制御文字・長い serial を足す。

### M6. touch screen が direct かどうかを property で決めてはいけない。§4.8・§6.3 が D11 の決定と食い違う（§5.2）

- 根拠: compositor は touch screen を「multitouch protocol B で `INPUT_PROP_POINTER` が無い」で分け、DIRECT を見ない（`input.c:883-900, 1217-1245`）。Linux・FreeBSD の backend は property を読まず空にする（`keiland-backend-evdev.h:39-44`、`libkeiland-backend/evdev/input-evdev.c:289-304`）。§5.2 は touch screen について「kernel が DIRECT を付ける」としか書かず、実装が `direct = INPUT_PROP_DIRECT` を全部の device に当てると、property の無い経路の touch screen が「回さない」側に入る。§4.8 と §6.3 は「Linux・FreeBSD では pen は全部 Tablet = 回さない」と書くが、ws.md の Q1 の決定 D11 は「property の読みを足す」。
- 直し方: §5.2 に「touch screen（MT で POINTER でない）と絶対の pointer は常に direct、property で分けるのは pen だけ」と書く。§4.8・§6.3 を D11 の決定（読みを足した後は pen も DIRECT で分ける）に合わせる。

### M7. ユーザーの touch LCD の descriptor は BUG-267 にもう有り、規則の確かめ方が変わる（§1.7・§5.2・§13）

- 根拠: [BUG-267](../../bugs/BUG-267.md) の 5330 の dump（`plan/bugs/BUG-267/touchscreen-if0.rdesc`・`touchscreen-if1.rdesc`）は、USB の外付けの touchscreen（kernel の名前 "Pen and multitouch sensor"、DP の 1920x1280 の display と一緒）の descriptor。読んだ結果、interface 1 の最初の application は **Pen（page 0x0d・usage 0x02）**、interface 0 は Touch Screen（0x0d・0x04）。kernel の parser はもう pen の application を `HID_REPORT_PEN_DISPLAY`（Pen）と `HID_REPORT_PEN_TABLET`（Digitizer）に分けて `input->pen` に持つ（`hid-report.c:2801-2823`、`include/drivers/generic/hid-report.h:36-41`、`hid-input.c:536`）。§1.7・§5.2・§13 は「descriptor は未確認（WS079 H1）」とする。WS079 の 10 インチの LCD と同じ機材かは推測（WS079 の記録は「USB-C か HDMI+USB」）。
- 起こること: 規則「同じ HID の device に Touch Screen の collection があれば DIRECT」は、この機材では pen（interface 1）と touch（interface 0）が別の `hid_input` なので当たらない（Pen の application の方で DIRECT になるので結果は正しい）。identity は USB の vendor・product・serial なので 2 つの interface で同じになり、1 つの結び付けが両方に効く（§5.3 の主張はこの機材で成り立つ）。
- 直し方: §1.7 の「pen は DIRECT も POINTER も付かない」の行に「parser は Pen と Digitizer をもう分けている」を足し、規則は `input->pen == HID_REPORT_PEN_DISPLAY` → DIRECT、`_TABLET` → POINTER を主にし、Touch Screen の条件は補いとする。`host-hid-pen-props` の fixture に BUG-267 の 2 つの descriptor を使う。§13 の契機「descriptor の確認（H1）」は「BUG-267 の機材で確認済み、別の機材が来た時」に直す。

### M8. WS141 p015 の前の Pi で回転を許すと、Guardrail に反する毎 frame の CPU の並べ替えが main に出る（§4.9・D7）

- 根拠: §4.9 は p015 の前の Pi を「動く（CPU/TFU の変換あり）と記録」に留めるとし、Settings の回転の button は Pi でも出る（§6.3 に platform の条件が無い）。Guardrail は毎 frame の経路で CPU が raster→UIF を並べ替えることを禁じる（`plan/guardrail.md:230-234`）。ws.md の D7 の決定は「Pi の**受け入れ**は p015 の後」で、p015 の前の Pi の利用者が CPU の経路を踏むことまで決めていない。
- 直し方: ユーザーに出す: (a) p015 の前の Pi では `set_rotation` を UNSUPPORTED にし（何で Pi と分かるかは libvulkan だけの規則の中で決める必要がある。例: build の構成で回転を切る）、測る時だけ試験の option で許す、(b) 規則の例外として許す。推奨は (a)。

### M9. 「この画面に触って」が pen の proximity_in でも結ぶ（§5.3）

- 根拠: §5.3 は listen の間に最初に来た「touch の開始か pen の down か proximity_in」の device を結ぶ。
- 起こること: 机の上の pen tablet に pen を置いたまま（近接の範囲の中）だと、少し動いた時の proximity_in で机の板が LCD に結ばれる。LCD の pen も、触る前の hover で結ばれる（これは意図どおりだが、dialog の文「Touch that display」と違う）。
- 直し方: pen は down（`BTN_TOUCH`）だけで結ぶ。dialog の文と合う。

### M10. 机の上の pen tablet を縦の論理に直に写すと縦横比が崩れる（§5.2、ユーザーの決定 D3）

- 根拠: 非 direct は「生 → slot の出力の論理の大きさへ scale」。横長の板を縦長の論理に全面で写す。
- 起こること: 板の上で右へ 1 cm と上へ 1 cm で cursor の動きが大きく違い、線の形が歪む。ユーザーの決定（回さない）はこの帰結まで述べていない。
- 直し方: 制限として §13 に書くか、縦横比を保つ写し（板の一部を使う）を Future Work に入れる。ユーザーの機材は direct なので p006 では作らなくてよい。

## 軽微

- L11. §6.1 の `set_rotation` の「key が無い」は NO_KEY だが、`set_shown` の not found は `ENOENT → KL_SYSTEM_RESULT_UNAVAILABLE`（`displays-shell.c:819-832`）で、libkeiland では UNAVAILABLE が ENODEV、NO_KEY は ENOENT（`system-view.c:1044-1047`）。対応表の L9 の「`set_shown` と同じ not found（ENODEV）」と §6.1 の NO_KEY が食い違う。UNAVAILABLE に揃える。
- L12. anchor は plane の原点とは限らない（`compose->output_x/output_y`、`heads.c:569-617`、wl_output の x/y も `heads.c:728-731`）。§4.4 の「anchor は原点のまま」は「anchor の place は保ち」に直す。
- L13. format の能力の確認（§4.2 の 1）に `BLIT_DST` が無い。回転した head の mirror は anchor の turn.image から head の turn.image へ `vkCmdBlitImage` する（§4.4）。
- L14. 対応表の L9 は「失敗は turn の作成で先に分かる」とするが、`turn` の pipeline（`turn.frag`、SPIR-V の compile は kernel の driver）をいつ作るかが無い。最初の record で作ると、OK を答えた後に失敗する。`turn_new` か `kwl_compose_output_prepare` で作ると書く。
- L15. §4.2 の 4 の「回転ありなら framebuffer を turn.view で作る」は、turn pass が使う swapchain の framebuffer（`compose->framebuffers[]`）も残す、と書く。
- L16. §5.4 の `struct kwl_cursor_geometry` に、§4.6 の 5 と §5.2 が言う `direct` の field が無い（QEMU の tablet は常に direct なので要らないなら §4.6 の 5 から消す）。絶対の pointer の結び付けが `absolute_slot` 1 つなのは、絶対の pointer が 2 つある時の制限として書く。
- L17. 列挙のずれ: Linux と FreeBSD の backend は evdev の code を 1 つの file `libkeiland-backend/evdev/input-evdev.c` で共有する（`libkeiland-backend-linux/Makefile.linux:34`、`libkeiland-backend-freebsd/Makefile.freebsd:29`）。§12 の `libkeiland-backend-{zedbsd,linux,freebsd}/input-*.c`、§4.8 の「3 つの backend の tree」、D11 の「2 file」「3 file」は「zedBSD の `input-zedbsd.c` と共有の `input-evdev.c` の 2 file」。`include/keiland/keiland.h` は `userland/desktop/include/keiland/keiland.h`。`scanout.c:287` は `:288`。§0 の「D7〜D11」は §9 の D7〜D12。§1.1 の「書く所 5 か所」は main.c の既定値を入れて数え直す（M1）。
- L18. listen の間の指を「`ROUTE_NONE` で up まで無視」（§10）は、「どこにも行かない」の route が `ROUTE_IGNORED`（`touch.c:111-118`）。`ROUTE_NONE` は「slot に指が無い」。
- L19. 注入の device の identity は vendor・product が 0 なので `virtual:0000:0000:NAME`（`input-inject.c:466-468, 657-660`）。注入の pen と touch は名前が違うので別の identity で、「1 つの結び付けが pen と touch に効く」は注入では確かめられない（host 試験で確かめる、と §11 に書く）。`INPUT_INJECT_KIND_PEN` と `PEN_POINTER` は同じ名前だと同じ identity になるので、名前を分ける。
- L20. p002a（§12）を main に入れる時は、turn 無しで論理の大きさだけ入れ替わる状態を出さないよう、`rotate=`・`--rotate` の 0 以外を p002b まで UNSUPPORTED にする、と書く。
- L21. §11.2 の image は `plan/ws113/tests/config-amd64-p006.mk` を include する。WS113 は incomplete で、完了の時に WS の試験は消される（AGENTS）。WS217 の `tests/` に複写する（M14 の host 試験の扱いと同じ）。
- L22. ws.md の「ユーザーの決定」の D5「p006 で先に測る」は第 1 版の段の番号（第 1 版の p006 = 確かめ）。第 2 版では p007。ws.md の Resume point は p007 と書いているので、決定の文に注記を足す。
- L23. T1 への依頼が p002〜p007 の 6 回になる。2026-10-03 の試験の方針（意味のまとまった単位でまとめて）に合わせ、p002 と p003、p004〜p006 をまとめて流せるかを §12 に書く。
- L24. 回転した出力の cursor の plane: `kwl_cursor_plane_update` は C の park を先に解いてから空の像を publish する（`cursor-plane.c:314-335`）ので、unpark の直後の 1 回、C が古い像を新しい論理の位置で出し得る。Pi だけ・1 frame。publish を park の解除の前にするか、制限に書く。
- L25. `listen_touch`・`bind_touch` は active な session のどの client からも呼べる（`set_shown` と同じ信頼の形）。listen を続けて呼ぶ client は次の touch を飲み込み続けられる。今の形に合わせるなら、そう書く。

## review-1 の指摘への対応の確認（第 2 版の対応の表を source で確かめた）

| ID | 確認 | 残り |
| --- | --- | --- |
| C1 | 解決。版の扱い・判定は source と合う。zedBSD の libwayland は proxy の版を interface の版で切る（`libwayland/proxy.c:852-855`）ので、interface の版を上げる直しは要る。main と全 agent branch の `KL_SYSTEM_MANAGER_VERSION` は 33（34 は空き） | M4（段の分け方） |
| C2 | 大筋は解決 | M1（`--width` は呼べない、既定値）、C4（switch の比較は論理同士） |
| S1 | 解決（D7 の受け入れの扱い） | M8（Pi の利用者の経路） |
| S2 | 大筋は解決 | C3（swapchain 側の pass）、M3（pass の選び方）、L13 |
| S3 | 解決。`state < 0` は plane を空にする（`cursor-plane.c:332-335`）、`kwl_cursor_plane_takes` も同じ判定で damage に cursor が入る | L24 |
| S4 | 解決 | L20・L23 |
| S5 | 解決（fact と host 試験） | C4（game mode の後の open で resize が走らない） |
| S6 | 解決。点・矩形・逆・24.8・vector の式を導き直して全部合った（下の確かめた点） | — |
| M1 | 解決。per-frame の damage は cursor の古い所と新しい所を両方含む（`damage.c:145`）ので、scene を今の frame だけ描いても古い cursor は残らない。record した frame は必ず R が submit する（cancel は record の前、`compose.c:1083-1085, 3337-3340`）ので不変条件も成り立つ | — |
| M2 | 解決 | — |
| M3 | 解決 | — |
| M4 | 大筋は解決 | M6・M7 |
| M5 | 一部 | C4（glass は `kwl_compose_open` で作られる） |
| M6 | 一部 | M2 |
| M7 | 解決 | L13 |
| M8 | 解決 | M4（`KL_VERSION` の段） |
| M9 | 解決（案 E） | — |
| M10 | anchor は解決 | S9（head） |
| M11 | 解決 | — |
| M12 | 解決（WS213 p004 は in-progress のまま、`plan/ws213/phase004/phase.md:6`） | — |
| M13 | 解決。「90 = 左辺が下」は式（論理の上辺 → 物理の右端の列）と合う | — |
| M14 | 大筋は解決 | L21・S7 の試験 |
| L1〜L8・L10 | 解決（L4 は L12 の言い方、L7 の `DISPLAYS` は `server->width` を返すことを確かめた: `heads.c:686-687`） | L12 |
| L9 | 食い違い | L11 |

## 確かめた点（source と照らして正しかった所）

- 写像の式（§2）: 点 90 `(Wp−1−y, x)` の逆は `(Y, Wp−1−X)`、270 `(y, Hp−1−x)` の逆は `(Hp−1−Y, X)`。半開の矩形 90 `X∈[Wp−y1, Wp−y0), Y∈[x0,x1)`、180・270 も各画素の像の集合と一致。24.8 は画素の式を 1/256 の単位にしたもので、誤差は 1/256 px。傾きの vector は点の写像の微分（90: 物理 `(dX,dY) = (−dy, dx)`）の逆で `(TY, −TX)`、270 は `(−TY, TX)`。
- `server->width/height` の書き手は `main.c:104-105, 419, 433`、`compose.c:1384-1385`、`output-switch.c:218-219, 230-231` だけ（grep）。head は `heads_open` の `kwl_compose_display_read(&head->width, ...)`（`heads.c:829-837`）。
- `kwl_heads_config_load` は parse と `display_mode` だけで（`heads.c:126-168`）、早めても副作用は無い。古い build は 4096 byte で切れた最後の行を、`=` が無ければ skip、知らない key でも skip する（`displays.c:583-586, 654-656`）ので、§6.4 の「古い build は新しい行だけを切る」は正しい。
- kernel: evdev は `EVIOCGUNIQ`・`EVIOCGPHYS`・`EVIOCGPROP` を持つ（`generic/input.c:2355-2385`）。USB の unique_id は serial（`usb-hid.c:856-863`）、I2C-HID は NULL（`i2c-hid.c:1286`）、注入は `BUS_VIRTUAL`。touch screen の node は DIRECT、pad は POINTER（`hid-touch.c:155, 174`）、main の device の properties は 0（`hid-input.c:300-330`）。
- zedBSD の backend だけが `EVIOCGPROP` を読む（`input-zedbsd.c:288-291`）。
- protocol: 版 34 は main・全 agent branch で未使用。set_shown の判定（`displays-shell.c:161`）と libkeiland の判定（`system.c:2290`）は manager の版。client は表に無い event の opcode を EPROTO にする（`libwayland/wire.c:897-905`）。
- game mode の `size_matches` は `server->width/height` との比較（`scanout.c:189`）、scanout の claim は `server->width/height`（`scanout.c:288`）。
- shot は test image だけの機能で（`shot.c:8-12`）、利用者向けの screenshot は無い（grep）ので、物理の絵を返しても製品に影響は無い。xdg_output は無い（grep）。
- 今の code は `compose_pass` と `pass_load` で外部 dependency が違うまま同じ pipeline を使っているので、turn の pass の dependency を変えても既存の前例の範囲（規格の厳密な互換の読みは下の未確認）。

## 確かめていない点（推測を含む）

- 外部 dependency だけが違う render pass の pipeline の互換を、zedBSD の libvulkan・venus の host の driver・Linux の libvulkan-compat がどう扱うか（既存の code も同じ形）。`UNDEFINED` からの遷移で内容を実際に捨てる driver があるか（C3 が i915・venus で見えるかどうか）。
- BUG-267 の touchscreen が WS079 の 10 インチの LCD と同じ機材か（推測では同じ: USB の touch と pen、DP の 1920x1280 の display）。その serial の文字（M5）。
- head の mode が変わった時（hotplug で違う大きさ）の既存の窓の refit の経路があるか（S9 の直しで流用できるか）。
- OSK・Wiseview・App Home の配置が `output_resized` だけで縦に合うか、lock 画面と sleep・resume・lid の経路（review-1 と同じく未確認）。
- `kwl_cursor_box_warp` の move の callback で古い回転と新しい回転を使う形が WS213 p004 の契約（in-progress）と衝突しないか。
- Settings の `menu.c` の popup と dialog が「primary 無し・result で閉じる」を今の API で書けるか（`se_dialog` の field は見たが、閉じ方は読んでいない）。
- bcm2711 の change ring と TFU（review-1 と同じ）。
