<!-- awesome-plan project=zedbsd record=ws217-p001 -->

# ws217-p001: 設計 — 画面の回転（display ごとの 0°・90°・180°・270°）と pen・touch と display の結び付け

Parent: [WS217](../ws.md)
Status: planning（第 2 版、2026-10-13 P2。第 1 版は P4、[review-1](review-1.md) の全指摘に対応。Q1 の review-2 待ち）
Disposition: normal
Primary Milestone: MG006（WS から継承）
Queue / attempts: Q1 の指示（2026-10-13、P4「第 1 版」、P2「review-1 の全指摘に対応した第 2 版」）。コードは書かない。
Purpose / goal: Settings の Display に display ごとの回転を足し、縦長で Notes を使える（ペンで書ける）ようにするための、compositor・入力・protocol・Settings・保存・試験の設計と、実装の段の分け方。ユーザーの決定（2026-10-13、ws.md）により、pen・touch と display の結び付けも含む。
Prerequisites: 無し（調べは §1）。実装の段は WS213 p004 の **scoped な出力**（park、`struct kwl_cursor_box`、I の warp の番号）と WS113（displays.conf・`kl_system_displays_*`）の main に統合済みの code を前提にする（M12: WS213 p004 は in-progress。契約が変わったら追従）。

## 第 1 版との差分の要約（2026-10-13 P2）

| 項目 | 第 1 版 | 第 2 版 |
| --- | --- | --- |
| protocol の版（C1） | displays の object の「version 20」、manager は 33 のまま | `KL_SYSTEM_SINCE_ROTATION` は manager の通し番号の次（今の最大 33 → **34 案**、Q1 が割当）、`KL_SYSTEM_MANAGER_VERSION` も 34 に。両側とも manager の版で判定 |
| 論理と物理の分離（C2） | `server->width/height` を論理にする、とだけ | 物理の mode の大きさを `compose->mode_width/height`（head は `head->mode_width/height`）に分け、`server->width/height` を書く 5 か所（§1.1 の表）を 1 つの関数 `kwl_output_size_set()` に通す |
| libkeiland の ABI（M8） | `struct kl_display` の末尾に `rotation` | struct は伸ばさず getter `kl_system_displays_rotation()`。`kl_window_output_mode` は変えない（§4.7 の案 E で不要） |
| wl_output（M9） | geometry に本当の transform、mode は物理 | **案 E**: transform は normal のまま、mode は論理（§4.7）。client は何も変わらず、`set_buffer_transform` の EPROTO の危険が消える。本当の transform は Future Work（案 D と一緒） |
| Pi 4（S1） | 「GPU だけ・1 pass」 | raster→UIF の変換（CPU か TFU）が要り、WS141 p015 の前は Guardrail（2026-10-12）に反する。Pi の受け入れは p015 の後（D7）。§4.9・§7 |
| turn の画像（S2） | usage 3 つ、layout と同期は無し | layout（`SHADER_READ_ONLY_OPTIMAL`）、2 つの render pass、外部 dependency、usage に `TRANSFER_DST`、format の能力の確認、`texelFetch`。§4.2 |
| cursor の plane（S3） | 像を回して plane に載せる | **回転した出力では plane を使わない**（合成の cursor）。p002 に入れる |
| 段の依存（S4） | p002 の T1 が Settings を使う、p004 は p003 と並行 | p002 の T1 は `--rotate` と `--file` の displays.conf、p004 は p003 の後。§12 |
| game mode（S5） | `rotated` は log のため | 正しさのための必須の fact。game mode の中の回転は `kwl_scanout_leave` の後。§4.5・§8 |
| 矩形の写像（S6） | 点の式で 4 隅 | 点（画素）と半開の矩形を別の式に。逆写像を直した。host 試験は総当たり。§2 |
| 入力（M2・M3・M4） | 受け箱に rotation を後から書く、傾き無し、全部の pen を回す | 矩形・回転・物理の大きさを 1 つの struct で warp/outputs の両方が丸ごと書く。傾きも回す。`INPUT_PROP_DIRECT` の device だけ物理→論理（画面の無い pen tablet は論理に直に）。kernel の hid-digitizer が DIRECT を付ける規則。§5 |
| pen・touch と display の結び付け | 無し（D3 で別の WS） | **新しい §5.3・§6（bind_touch・listen_touch・touch の event、`touch=` の行、Settings の card と「この画面に触って」）、段 p006** |
| 回転の適用（M5・M6・M10） | 書き換えてから作る、起動時は作ってから作り直す | 新しい資源を全部先に作ってから入れ替える。config は `compose_targets` の前に引く。回転の時に touch を cancel し grab を終える |
| 向きの約束（M13） | 内部 0..3 | 内部・protocol・file とも**時計回りの度数**（0/90/180/270）。Wayland の enum は `rotate.h` の 1 関数だけ（今は使わない） |
| 試験（M14・L6・L7） | 他 WS の tests/ を拡張 | WS217 の `tests/` に複写、config.mk、OSK・Wiseview・窓の refit・game mode・hotplug・login/lock・傾き・5320 を足す。部分 frame の証拠は `KWL DAMAGE` の物理の矩形と SHOT の部分撮影 |

## review-1 の指摘への対応

| ID | 対応 | 所在 |
| --- | --- | --- |
| C1 | 直した。`KL_SYSTEM_SINCE_ROTATION 34U`（案）、manager 34、interface の version を `KL_SYSTEM_SINCE_ROTATION`、requests 7・events 5、判定は manager の版 | §6.1 |
| C2 | 直した。`compose->mode_width/height`・`head->mode_width/height`、書き手 5 か所の表と `kwl_output_size_set()`、p002 の受け入れに書き手の表 | §1.1、§4.1、§12 |
| S1 | 直した。Pi 4 は WS141 p015 の後に限り GPU だけ。p015 の前は「動く（CPU/TFU の変換あり）と記録」（D7）。見積もりに変換の分を足した。driver 側の代案（常に TFU）も書いた | §4.9、§7、§9 D7 |
| S2 | 直した。layout・2 つの pass・dependency・usage・format の確認・`texelFetch`・layout の helper | §4.2 |
| S3 | 直した（推奨どおり）。回転した出力では cursor の plane を使わない。像の回転・hotspot の式は無し | §4.5 |
| S4 | 直した。p002 の T1 は `--rotate` と `--file`、p004 は p003 の後、plane の停止は p002 | §12 |
| S5 | 直した。`rotated` は必須の fact、`kwl_scanout_decide` の host 試験、game mode の中と出力が閉じている時の扱い | §4.5、§8、§11.1 |
| S6 | 直した。点と半開の矩形の式を分け、逆写像を直し、総当たりの host 試験。T1 には `KWL DAMAGE` の物理の矩形と SHOT の部分撮影 | §2、§11.1、§11.2 |
| M1 | 直した。scene は今の frame の damage、turn は age の union。不変条件（present が落ちた frame の後は turn を全体で描く）。clamp は論理 | §4.3 |
| M2 | 直した。`struct kwl_cursor_geometry` を warp と outputs の両方が丸ごと書く。warp の move の context に古い回転と新しい回転 | §5.4、§4.6 |
| M3 | 直した。傾きの vector も回す（式は §2）、peninject の傾きを試験に | §5.2、§11.2 |
| M4 | 直した。`INPUT_PROP_DIRECT` の device だけ物理→論理。kernel の hid-digitizer は property を付けていない（§1.7）ので p003 で規則を足す。Linux/FreeBSD の backend は property を読まない → D に出す（D11） | §5.2、§1.7、§9 |
| M5 | 直した。config の読み込みは `kwl_compose_output_open` の入口、回転は `compose_targets` の前に引く。`config_loaded` の「一度」は保ち、適用は別の関数 | §4.6 |
| M6 | 直した。新しい資源を全部先に作る。backdrop の UNTRIED の戻し。入れ替えの間の 2 倍の見積もり | §4.6、§7 |
| M7 | 直した。fit は論理、barrier は layout の helper、0° と回転時の mirror の可否の違いを明記。mirror の中の head ごとの回転は ws.md のユーザーの確認（各 display が自分の向き）で決定済み。Settings は mirror でも row に回転を出す | §4.4、§6.3 |
| M8 | 直した。getter。`kl_window_output_mode` は案 E で変えない | §6.2、§4.7 |
| M9 | 直した（案 E）。transform を告げないので pre-rotate する client は現れない。移植の package は調べていない（案 E ならば調べなくてよい）。本当の transform は Future Work | §4.7、§13 |
| M10 | 直した。回転の時に touch の cancel、pen の up/proximity_out、窓の move/resize の grab の終了 | §4.6 |
| M11 | 直した。UIF の変換、head の毎回全体、WS214 の予算と v3d の帯域を分けて書いた。texture cache は計測の項目 | §7 |
| M12 | 直した。Prerequisites を scoped な依存に | 冒頭 |
| M13 | 直した。内部も度数。D1 の絵は「panel を倒す向き」で選ぶ（決定済み）。11.3 に倒す向き | §2、§6.3、§11.3 |
| M14 | 直した。OSK・Wiseview・窓の refit、5320、切り替え・hotplug・lid、game mode の拒否、logout/login（login 画面は回らない: 製品の制限、D9）、lock、傾き、config.mk、他 WS の tests/ は複写 | §11 |
| L1 | 直した（参照を消した） | §10 |
| L2 | 直した。shown でない display の snapshot は保存した回転を当てて論理で送る。off・limited への `set_rotation` は保存して出す時に適用 | §6.1 |
| L3 | 直した。古い build の往復で `rotate=`・`touch=` が消えることを明記（受け入れる）。行の順と 4096 byte の上限。度数以外は無効の host 試験 | §6.4 |
| L4 | 直した。anchor が回った時は head を動かす | §4.4 |
| L5 | 採らない。180° だけ別の経路（blit の offset の反転）を作ると試験の対象が増える。turn pass は 4 つの向きで同じ shader | §3 |
| L6 | 直した。shot は keiland-shot。D5 は 1024x600 に `--rotate=90` で測る | §11.2、§9 D5 |
| L7 | 直した。`SHOT`・`DISPLAYS` の答えに `rotation=` | §4.2、§11.2 |
| L8 | 直した。`kwl_output_view` に `rotation` と物理の大きさ（案 E では wl_output は論理だけ使うが、Settings の snapshot と shot が物理を使う） | §4.7 |
| L9 | 直した。key が無い時は `set_shown` と同じ not found（ENODEV）。result は最初の frame を待たない（回転は modeset 無しで失敗しない。失敗は turn の作成で先に分かる） | §6.1 |
| L10 | 直した。p008（規約）は p007 の直しの後に当てる | §12 |

## 0. 要点（design-reviewer・Q1 向け）

- **どの GPU の driver も plane の回転を持たない**（§1.3）。よって **compositor が回す**（案 A）: 回転した出力では scene を論理の大きさの自分の画像（turn.image）に描き、最後に 1 pass（turn pass）で swapchain の画像（物理）へ回して写す。0° の出力は今の経路のまま。GPU だけで行う。**Pi 4 だけは** turn.image を shader で sample するために raster→UIF の変換が要り（S1）、WS141 p015 の前は CPU（256 KiB 以下の箱）か TFU が毎 frame 動く。Pi の回転の受け入れは p015 の後（D7）。
- **論理と物理を分ける**（C2）: 物理の mode の大きさは `compose->mode_width/height`（swapchain・mode・scanout・shot・driver）、論理は `server->width/height`（plane・窓・shell・gesture・wl_output・Settings の図）。両者が出会う所だけに写像を入れる。`server->width/height` を書くのは 1 つの関数。
- **入力**: 相対（mouse・touchpad）は変えない。絶対は device の種類で分ける: `INPUT_PROP_DIRECT` の device（touch screen・ペン LCD・QEMU の tablet）は「生 → **物理** → 逆回転で論理」、DIRECT でない device（机の上の pen tablet）は「生 → **論理**に直に」（回さない、ユーザーの決定）。傾きも回す。
- **pen・touch と display の結び付け**（ユーザーの決定、新）: 絶対の device は identity（bus・vendor・product・serial）で display の key に結ぶ。Settings の「Touch and pen」の card で選ぶか、display の row の「Touch to assign」で compositor を listen にして「この画面に触って」で決める。結び付けの無い device は anchor（今の挙動）。保存は displays.conf の `touch=IDENTITY KEY`。
- **client**: wl_output は transform normal のまま、mode は論理（案 E）。client は何も知らなくてよく、`set_buffer_transform` は今どおり 0 だけ受ける。
- **Settings と保存**: `kl_system_displays_v1` に `set_rotation`・`bind_touch`・`listen_touch` の request と `rotation`・`touch` の event（manager の version 34 案）。libkeiland は getter（ABI 不変）。Settings の row に倒す向きの 4 つの絵（D1）、すぐ反映（D4）。`displays.conf` に `rotate=KEY 90`・`touch=IDENTITY KEY`（version 1 のまま）。
- **game mode**: 回転した出力では直の scanout をしない（合成に落とす、D6）。**cursor の plane** も回転した出力では使わない（S3）。
- **残る問い** D7〜D11（§9）。決定済みの D1〜D6 は ws.md。

## 1. 調べた事実（source、file:line は agent/p2-ws215 a07ad4cc7 = main の tree。第 1 版の表は review-1 が main `f50beb833` で照合済み。第 2 版で読み直した行は a07ad4cc7）

### 1.1 compositor の出力の持ち方

| 事実 | 所在 |
| --- | --- |
| desktop は 1 つの plane。anchor（主の出力）が slot 0、head N は 1+N。`struct kwl_plane_rect {x,y,width,height}`、`KWL_PLANE_SLOTS 8`、`KWL_PLANE_ANCHOR 0` | `wayland/plane.h:12-43` |
| anchor の大きさは `server->width/height/refresh`、swapchain は `compose->output`（`.width/.height`）。**`server->width/height` は今、swapchain と mode を開く要求の大きさを兼ねる**（C2） | `kwl.h:1147-1150`、`compose.h:361` |
| **`server->width/height` を書く所（全部）**: `--width/--height`（`main.c:419, 433`、`size_given`）、anchor の open で display の native の大きさ（`compose.c:1384-1385`）、output switch の新しい display と失敗の戻し（`output-switch.c:218-219, 230-231`） | — |
| **`server->width/height` を mode・swapchain の大きさとして読む所**: `vkdemo_display_open_on(..., server->width, server->height, ...)`（`compose.c:307`）、`compose_refresh(server, display, server->width, server->height, ...)`（`compose.c:1389`）、switch の `old_width = server->width` と `width != old_width`（`output-switch.c:200-202, 257-259`）、game mode の `kl_backend_scanout_open(server->backend, server->width, server->height, ...)`（`scanout.c:287`）、head は `vkdemo_display_open_on(..., head->width, head->height, ...)`（`heads.c:837`） | — |
| `kwl_outputs()` が plane の矩形を作る。`kwl_output_rect()`・`kwl_output_at()` | `kwl.h:2295-2297`、`heads.c:1615-1690` |
| wl_output に送る値は `struct kwl_output_view {index,x,y,width,height,refresh}` | `kwl.h:2004-2013` |
| `struct kwl_head {open, lost, display, name, output, views[], framebuffers[], rendered[], acquired, x, y, width, height, refresh, dirty, wallpaper, showed, in_frame, image, global}` | `compose.h:235-256` |
| scale・transform・rotation の field は無い | `protocol.c:699-704` |
| anchor の大きさの変更の経路: `output_resized()`（glass の resize、窓の refit、pointer の warp と clamp を**新しい矩形と同じ warp の区間で**） | `output-switch.c:660-688` |
| mode は `compose->display_mode`。anchor の plane の位置は `compose->output_x/output_y` | `compose.h:577-579` |

### 1.2 描画と scanout の経路

| 事実 | 所在 |
| --- | --- |
| anchor の surface と swapchain は `preTransform = IDENTITY` | `userland/tests/vkdemo/display.c:133-140, 286, 317-322` |
| 主の render pass: `initialLayout = UNDEFINED`、`finalLayout = PRESENT_SRC_KHR`、外部 dependency は `COLOR_ATTACHMENT_OUTPUT → COLOR_ATTACHMENT_OUTPUT` だけ | `compose.c:1715-1745` |
| 投影の行列は無い。quad は CPU で NDC を計算して push constant に（`server->width/height`、head なら view） | `compose.c:2482-2523`、`glass.c:1905-1962`、`batch.c:156-233` |
| damage は論理で集め、`compose_region()` が age の履歴を足し、`compose->output.width/height` に clamp、70% で全体 | `compose.c:2560-2660` |
| `image_frames[image] = frame` は描く前に書かれる（M1） | `compose.c:2574` |
| scene の写し（buffer→image の戻しを含む、`TRANSFER_DST` が要る）、shot、mirror の元の barrier は `PRESENT_SRC_KHR` を前提 | `scene.c:160, 213`、`compose.c:2779`、`shot.c:298, 334`、`heads.c:1196-1219` |
| mirror: anchor の完成した swapchain の画像を `vkCmdBlitImage`（LINEAR）で `kwl_displays_fit(compose->output.width, ...)` の矩形へ。`compose->readback` が無ければ黒、log `mirror unsupported` は一度 | `heads.c:1160-1230` |
| extended の head は毎回全体を描く | `heads.c:1262-1279` |
| game mode: `size_matches` は client の buffer を `server->width/height` と比べる。backend は transform 無し。`kwl_scanout_leave(server, reason)` | `scanout.c:185-190, 287`、`kwl.h:2067`、`scanout-rules.h:48-61` |
| cursor の plane: `plane_state()` が −1 で合成の cursor。矢印と shape は copy を経ない（`plane_fixed`）。park は `kwl_cursor_plane_hold` で、次の `kwl_cursor_plane_update` で解ける。extent は `min(maxSrcExtent, 64)`（正方形とは限らない）。plane の mode は `compose->output.width/height` で照合（物理のままが正しい） | `cursor-plane.c:300-360, 395, 470-520` |
| shot は完成した swapchain の画像（物理）。`DISPLAYS` の答えに `mode=`・place の行 | `shot.c:27, 274-340, 500-575` |

### 1.3 driver と UAPI の回転の能力（第 1 版のまま、review-1 が確認）

| platform | 事実 | 所在 |
| --- | --- | --- |
| UAPI | present・plane_state・commit・overlay・cursor の struct に rotation/transform/flip の field は無い。tiling は `GPU_IMAGE_LINEAR` だけ | `include/uapi/gpu-display.h:189-424`、`gpu.h:36, 97-111` |
| i915 | `hw.rotation` は `MODE_ROTATE_0` の 2 か所だけ、scanout は linear XRGB8888 だけ、`plane_count = 1` | `display/plane.c:293, 378`、`display/internal.h:2525-2530`、`display/display.c:2761, 3377` |
| bcm2711（Pi 4） | HVS は flip の bit を読むだけ、90/270 を持たない。**texture は strict UIF で sample し、raster の画像は変わった箱が 256 KiB 以下なら CPU、大きければ TFU で変換**（S1） | `bcm2711/list.c:329-332`、`display-flip.c:1664-1708`、`native-texture.c:8, 119`、`native-storage.c:25-26, 753-808` |
| venus | cursor・commit・overlay は NULL | `venus/display.c:154-167, 803-806` |
| libvulkan | `supportedTransforms`/`currentTransform` は IDENTITY だけ、他は `VK_ERROR_INITIALIZATION_FAILED` | `wsi.c:125, 620-624`、`wsi-display.c:584-585`、`wsi-swapchain.c:1213-1215` |
| Linux・FreeBSD | libvulkan-compat の KMS は legacy、IDENTITY | `libvulkan-compat/kms.c:412, 431` |

### 1.4 入力の経路

| 事実 | 所在 |
| --- | --- |
| device の分類: pen tablet（`BTN_TOOL_PEN`・`ABS_PRESSURE`・両軸）、touch pad（MT protocol B ＋ `INPUT_PROP_POINTER`）、touch screen（MT protocol B）、keyboard、絶対の pointer（`ABS_X/Y`）、相対の pointer。**`INPUT_PROP_DIRECT` は読んでいない** | `input.c:131-200, 880-900, 1210-1245` |
| `struct kwl_input_device {fd, live, attached, closing, pointer, keyboard, absolute, tablet, touch, touchpad, reading, abs_*, ...}`。`KWL_INPUT_MAX 32`、scan は 2 秒ごと（`KWL_INPUT_SCAN_MS 2000`） | `kwl.h:96-106, 382-422` |
| 絶対の device はどれも anchor に写る: QEMU の tablet は I thread の `kwl_cursor_box_place()`（`box->outputs[ANCHOR]` の幅と高さ）、touch screen は main の `read_report()`（`server->width/height`）、pen は main の `place_pointer()`（同じ） | `cursor-box.c:131-159`、`touch.c:964-969`、`tablet.c:1006-1028` |
| 受け箱 `struct kwl_cursor_box` は `outputs[KWL_PLANE_SLOTS]`・`output_count` を持つ。`kwl_cursor_box_warp()` は矩形と位置を 1 区間で、`kwl_cursor_box_outputs()` は矩形だけを書く。`kwl_input_sync()` が毎 pass `kwl_cursor_box_outputs()` を呼ぶ | `cursor-box.h:112-144`、`cursor-box.c:170-225`、`input.c:485-508` |
| `kwl_pointer_warp_to(server, x, y)` は anchor に固定（`place.output = KWL_PLANE_ANCHOR`） | `input.c:647-666, 1750-1765` |
| tablet の device は name・vendor・product（USB の時）を持ち、client に `TABLET_NAME`・`TABLET_ID`・`TABLET_PATH` で告げる。傾きは device の軸のまま client へ | `tablet.c:120-160, 539-549, 1318-1328`、`tablet.c:592-598, 713` |
| touch screen は `struct touch_screen {input, axis_x, axis_y, slot, contacts[], motion_device, msc...}`（name・identity は持たない）。wl_touch.cancel の経路がある | `touch.c:203-213`、`touch.c:50-68, 1832` |
| surface の座標は plane の位置 − surface の位置（出力の項は無い） | `seat.c:593-594`、`touch.c:2288-2289`、`tablet.c:1149-1150` |
| 端・角の gesture は `server->width/height`（anchor）で判定 | `corner.c:626-640`、`home.c:429`、`touch.c:1020-1021` |

### 1.5 client と protocol

| 事実 | 所在 |
| --- | --- |
| wl_output.geometry は transform 0（normal）、physical size 0。mode は `view.width/height`。scale は 1 | `protocol.c:651-705` |
| `set_buffer_transform` は 0 以外、`set_buffer_scale` は 1 以外を EPROTO | `protocol.c:1036-1047` |
| fullscreen の configure と bounds は `server->width/height` | `protocol.c:1729-1731, 1838-1839` |
| libkeiland は mode の幅・高さ・refresh だけ `kl_window_output_mode()`（「first screen's current mode」）で返す。Settings の About と Display の plain の表示が使う | `libkeiland/ui/window.c:1010-1015, 2799-2871`、`settings/page-display.c:395-426` |
| xserver は wl_output を見ない。GTK4 は upstream のまま | `xserver/wayland.c:1184-1189` |
| Notes は窓の大きさから page を両軸で fit。toolbar は折り返さない | `notes/geometry.c:343-376`、`notes/ui.c:270-276` |

### 1.6 Settings と保存

| 事実 | 所在 |
| --- | --- |
| `KL_SYSTEM_SINCE_*` は**全部 manager の版の通し番号**。今の最大は `KL_SYSTEM_SINCE_PHONE_CALLS 33U` = `KL_SYSTEM_MANAGER_VERSION 33U`。displays の object は manager の版で作られ（interface の version は `KL_SYSTEM_SINCE_SHOWN`、requests 4・events 3）、compositor は `object->version >= KL_SYSTEM_SINCE_SHOWN`、libkeiland は `manager_version < KL_SYSTEM_SINCE_SHOWN` で判定 | `kl-system-protocol.h:324, 366-422, 686-702, 974-985`、`system-protocol.c:506-528`、`displays-shell.c:161-168`、`system.c:2275-2300` |
| `kl_system_displays_v1`: request `destroy(0)`・`apply(1)`・`set_brightness(2)`・`set_shown(3)`、event `output(0)`・`done(1)`・`result(2)`。result の値 `KL_SYSTEM_RESULT_OK/DENIED/UNSUPPORTED/BUSY/INVALID/UNAVAILABLE/FAILED/NOT_SAVED/NO_KEY` | `kl-system-protocol.h:103-134, 534-551, 944-954` |
| snapshot: anchor は `server->width/height`、head は `head->width/height`、shown でない display は `kwl_compose_display_read()` の物理の mode | `displays-shell.c:420-470` |
| 公開 API: `struct kl_display {key,label,x,y,width,height,refresh_mhz,flags,brightness}`（`kl_system_displays_get` は `memcpy` で写す。libkeiland は共有 library）、`KL_VERSION 92`（master: 次は 93） | `include/keiland/keiland.h:49, 1982-2042`、`system.c:2150-2163` |
| Settings の Display の頁: `struct se_display {displays[], count, mode, draft[], ..., request, light_request, shown_request, message}`、row（`display_rows`）・card（`display_card`）・mode・Apply/Revert・light。dialog は `se_dialog_*`（title・body・primary・fields、list は無い）、popup の menu は `menu.c` | `settings/page-display.c:1-78`、`settings.h:583-605, 787-820, 1669-1676` |
| `displays.conf`（`version=1`、`mode=`、`anchor=`、`brightness=`、`place=KEY X Y`、`off=KEY`）。知らない key は skip。読みは 4096 byte（`HEADS_TEXT_MAX`）。`KWL_DISPLAYS_KEY 64`、`KWL_DISPLAYS_PLACES 16`。path は `$HOME/.config/keiland/displays.conf`（greeter は別の process・別の HOME: `handoff.c:9-19`） | `displays.h:56-60, 78-106`、`heads.c:57-61, 126-168, 1461-1487` |
| `kwl_heads_config_load` は一生に一度（`config_loaded`）、`compose_targets`・`kwl_scene_open` の後に呼ばれる | `heads.c:136-140`、`compose.c:440-472` |

### 1.7 入力の device の identity と property（新、結び付けのため）

| 事実 | 所在 |
| --- | --- |
| backend は `EVIOCGNAME`・`EVIOCGID`（bustype・vendor・product・version）・`EVIOCGBIT`・`EVIOCGPROP` を読む。**`EVIOCGUNIQ`（serial）は読んでいない**。Linux・FreeBSD の backend は property を読まず空 | `libkeiland-backend/keiland-backend-evdev.h:36-70`、`libkeiland-backend-zedbsd/input-zedbsd.c:120-175, 270-300` |
| kernel の evdev は `EVIOCGUNIQ`・`EVIOCGPHYS`・`EVIOCGPROP` を持つ。unique_id は USB の serial（無ければ空） | `src/drivers/generic/input.c:66, 2355-2385`、`src/drivers/usb/usb-hid.c:856-863, 1363` |
| HID の pen と touch は **1 つの identity（name は別、unique_id・id は同じ）で 2 つの evdev の node**として登録される。touch の node は `touch_description.properties` を持ち、pen（main の device）の `info.properties` は 0（**pen は DIRECT も POINTER も付かない**） | `src/drivers/generic/hid-input.c:300-340`、`hid-touch.c:155, 174` |
| HID の application: Digitizer `0x0d0001`、Pen `0x0d0002`、Touch Screen `0x0d0004`、Touch Pad `0x0d0005`。pen の collection は `layout->pen` に記録される | `src/drivers/generic/hid-report.c:65-74, 1446, 1494` |
| 注入の device（`/dev/input-inject`）: `struct input_inject_setup {magic, kind, x_max, y_max, report_contacts, reserved}`、`INPUT_INJECT_KIND_PEN 1`。pen は property 無し | `include/uapi/input-inject.h:87, 114-125`、`src/drivers/generic/input-inject.c:640-680` |
| ユーザーの pen の機材: 10 インチの touch LCD（USB-C か HDMI+USB）と AES pen。descriptor は未確認（WS079 H1） | `plan/ws079/ws.md:29-34, 46` |

## 2. 用語と向きの約束

- **物理（physical）**: panel の画素の並び。`compose->mode_width/height`（= swapchain `compose->output.width/height`）、`head->mode_width/height`。swapchain・viewport・scanout・shot・driver・cursor の plane の mode の照合。
- **論理（logical）**: 利用者が見る向きの desktop の画素。`server->width/height`、`head->width/height`、plane の矩形、窓・shell・gesture・wl_output・Settings の図。90°・270° では幅と高さが入れ替わる。
- **回転の値**: 内部（`compose->rotation`・`head->rotation`）・protocol・displays.conf・log の全部で**時計回りの度数** `0`・`90`・`180`・`270`（`unsigned`。定数 `KWL_ROTATE_0/90/180/270` = 0,90,180,270、公開は `KL_DISPLAY_ROTATE_*` 同値）。他の値は INVALID。「90°」は**絵を panel の上で時計回りに回す**ことで、**panel を反時計回りに倒した**（左辺が下に来た）利用者に合う。Settings の絵は倒す向きで描く（D1、§6.3）。Wayland の enum（反時計回り）への変換 `kwl_rotate_to_wayland()` は `rotate.h` に置くが、案 E（§4.7）では今は呼ばない。
- **写像**（物理の幅 Wp・高さ Hp、論理の幅 Wl・高さ Hl。90/270 では Wl=Hp・Hl=Wp）。純粋な module `rotate.c`/`rotate.h`:
  - 点（画素）論理 (x,y) → 物理 `kwl_rotate_point`: 0: (x, y)、90: (Wp−1−y, x)、180: (Wp−1−x, Hp−1−y)、270: (y, Hp−1−x)。
  - 点の逆 物理 (X,Y) → 論理 `kwl_rotate_point_back`: 90: (Y, Wp−1−X)、180: (Wp−1−X, Hp−1−Y)、270: (Hp−1−Y, X)。（S6: 第 1 版の「90 と 270 を入れ替えた式」は Wp≠Hp で誤り）
  - **半開の矩形** 論理 [x0,x1)×[y0,y1) → 物理 `kwl_rotate_rect`: 90: X∈[Wp−y1, Wp−y0)、Y∈[x0, x1)。180: X∈[Wp−x1, Wp−x0)、Y∈[Hp−y1, Hp−y0)。270: X∈[y0, y1)、Y∈[Hp−x1, Hp−x0)。逆 `kwl_rotate_rect_back` も同じ形。
  - 固定小数（24.8）の点 `kwl_rotate_fixed`: 物理の 24.8 の値 (X,Y)（0 ≤ X < Wp·256）→ 論理: 90: (Y, Wp·256−1−X) の形（画素の式と同じ対応を 256 倍の単位で）。入力に使う。
  - vector（傾き）`kwl_rotate_vector`: 論理の向きの vector (vx,vy) は物理の vector から 逆回転: 物理 (TX,TY) → 論理: 90: (TY, −TX)、180: (−TX, −TY)、270: (−TY, TX)。（pen の tilt は device の物理の軸。利用者の見る向きに合わせる）
  - 大きさ `kwl_rotate_size`、`kwl_rotate_swaps(rotation)`（90/270 で 1）。
  - host 試験（§11.1）は「矩形の像 = その矩形の各画素の点の像の集合」を 4 つの向き × 非正方形で総当たり、往復（back(forward(p)) == p）、境界（0・端−1・端）。

## 3. 案と選択

| 案 | 内容 | 採否と理由 |
| --- | --- | --- |
| **A. 論理の画像 ＋ turn pass（採用）** | 回転した出力では scene を論理の大きさの自分の `VkImage`（turn.image）に描き、最後に 1 pass で swapchain へ回して写す（fragment shader が `texelFetch` で読む）。0° は今の経路 | 変更が compose.c の target と最後の pass、damage の写像、mirror の元、cursor の plane に閉じる。shader・batch・glass の計算に触れない。全 platform で同じ。費用は回転した出力だけ 1 pass（Pi 4 は ＋UIF の変換、§4.9） |
| B. NDC の関門で quad を回す | — | 不採用（第 1 版のとおり: `panel.vert` の `pixel` と frag の box・角・blur・影が出力の画素の向きに依存し、全 shader を直すことになる） |
| C. hardware の plane の回転 | — | 不採用、Future Work（D2 決定済み）。i915 は Y-tiled の scanout、HVS は 90/270 無し |
| D. client に pre-rotate させる | — | 不採用（今は）。pre-rotate する client は無く、compositor が解く経路も無い |
| E. wl_output は transform normal・mode は論理（採用、§4.7） | 本当の transform を告げる代わりに「論理の大きさの normal な出力」を見せる | 採用: 規格の字義（mode は transform の前）からは外れるが、client から見て矛盾が無く、pre-rotate する client が現れない（M9）、libkeiland・GTK4・xserver に変更が要らない。本当の transform は D を入れる時に一緒に（Future Work） |
| 180° だけ blit の offset の反転（L5） | — | 不採用: 経路が増える割に得る物が無い。4 つの向きで同じ shader |

## 4. compositor の設計

### 4.1 状態と所有（C2）

- **物理の mode**: `compose->mode_width/mode_height`（anchor）、`head->mode_width/mode_height`（head）。swapchain を開く・mode の refresh を引く・game mode の claim・切り替えの戻し・shot・cursor の plane の mode の照合は**これ**を読む。
- **論理**: `server->width/height`、`head->width/height`。plane・窓・shell・gesture・wl_output・Settings の snapshot・damage・scene・backdrop・layers・glass の画像。
- **回転**: `compose->rotation`、`head->rotation`（度数）。所有は M（main）、変更は park の中だけ（§4.6）。
- **`server->width/height` を書く唯一の関数** `kwl_output_size_set(server, mode_width, mode_height, rotation)`（output-switch.c）: `compose->mode_* = mode_*`、`compose->rotation = rotation`、`server->width/height = kwl_rotate_size(mode, rotation)`。呼び手は §1.1 の書き手 5 か所の置き換え: `main.c` の `--width/--height`（`size_given` の時 mode に、回転は後で config から）、`compose.c:1384`（native の大きさ）、`output-switch.c:218, 230`（新しい display と戻し。戻しは古い mode と古い回転）。head は `kwl_head_size_set(head, mode_width, mode_height, rotation)`。p002 の受け入れに「`server->width =`・`server->height =` の grep が `kwl_output_size_set` の中の 2 行だけ」を入れる。
- **turn**: `struct kwl_turn { VkImage image; VkDeviceMemory memory; VkImageView view; VkFramebuffer framebuffer; VkDescriptorSet set; uint32_t width, height; unsigned rotation; unsigned whole; }` を anchor（`compose->turn`）と head（`head->turn`）に 1 つ。回転 0 では `image == VK_NULL_HANDLE`。framebuffer は 1 つ（swapchain のように巡らない）。寿命は出力の open/close と回転の変更。持ち主は M、破棄は `kwl_compose_forget` の後に R の step 1（WS213 4.7）。

### 4.2 描く経路（anchor、回転 ≠ 0）（S2）

1. **turn.image の作り方**: format は `compose->format`、tiling OPTIMAL、usage `COLOR_ATTACHMENT | SAMPLED | TRANSFER_SRC | TRANSFER_DST`（scene の戻しが buffer→image の copy、mirror の元、shot と scene の写しの元）。作る前に `vkGetPhysicalDeviceFormatProperties` の `optimalTilingFeatures` に `SAMPLED_IMAGE`・`COLOR_ATTACHMENT`・`TRANSFER_SRC`・`TRANSFER_DST`・`BLIT_SRC`（mirror）があることを確かめ、無ければ UNSUPPORTED（§8）。
2. **layout**: turn.image は pass の外では常に `SHADER_READ_ONLY_OPTIMAL`。turn 用の render pass を 2 つ作る（既存の `compose_pass`・`load pass` と同じ attachment の format と sample count で pipeline は互換のまま）: `turn_clear_pass`（`initialLayout UNDEFINED`→`finalLayout SHADER_READ_ONLY_OPTIMAL`、loadOp CLEAR）、`turn_load_pass`（`SHADER_READ_ONLY_OPTIMAL`→同、loadOp LOAD）。外部 dependency: 入口は `srcStage = FRAGMENT_SHADER | TRANSFER`、`srcAccess = SHADER_READ | TRANSFER_READ | TRANSFER_WRITE` → `dstStage = COLOR_ATTACHMENT_OUTPUT`、`dstAccess = COLOR_ATTACHMENT_READ | WRITE`（前の frame の turn pass の読みと、scene の写し・戻し・shot・mirror の transfer の後）。出口は `COLOR_ATTACHMENT_OUTPUT / COLOR_ATTACHMENT_WRITE` → `FRAGMENT_SHADER | TRANSFER / SHADER_READ | TRANSFER_READ`。frame は同じ queue に順に submit されるので、この dependency で frame N の turn pass（読み）と frame N+1 の scene pass（書き）の WAR が閉じる。
3. **layout の helper** `compose_target_layout(compose, slot)`（`PRESENT_SRC_KHR` か `SHADER_READ_ONLY_OPTIMAL`）と `compose_target_image(compose, slot)`・`compose_target_size(compose, slot, &w, &h)`（論理）を足し、`scene.c:160, 213`・`compose.c:2779`・`backdrop.c:326-327`・`shot.c:298, 334`・`heads.c:1196-1219` の barrier の old/new layout と写しの元をこれから取る。0° では今の値が返るので挙動不変。
4. `compose_targets`: 回転ありなら framebuffer を `turn.view`（論理の大きさ）で作る。pass の `renderArea`・viewport・scissor・`pass_area`・`scissor_now`・`compose->clips` は論理（`compose.c:2600-2607, 2637, 2796-2822` を `compose_target_size()` に）。
5. scene・backdrop・layers・glass の host image は論理の大きさ（`compose.c:455-459`、`backdrop.c:71, 132`、`layer-cache.c:101-102`、`glass.c:766, 843, 1159`）。
6. **turn pass**: scene の pass（cursor の合成を含む、論理）の後、swapchain の画像（`initialLayout UNDEFINED`→`PRESENT_SRC_KHR`、既存の `compose_pass` と同じ）を attachment にした pass で、全画面の quad（既存の `quad.vert` と NDC の全画面の矩形）を描き、新しい fragment shader `turn.frag` が push constant（rotation、Wp、Hp）から `gl_FragCoord`（物理）を `kwl_rotate_point_back` と同じ式で論理に戻して `texelFetch(turn, ivec2(x, y), 0)` する（sampler の補間無し、画素を混ぜない）。描く矩形は**物理の damage**（§4.3）に scissor で限る。新しい pipeline `turn`（layout: 既存の quad の push constant の後ろに turn.frag の constants、descriptor set は turn.image 1 つ）。
7. R の record の列: `compose_record` の最後に turn pass。`kwl_shot_record` は turn の後（物理の絵）。`SHOT`・`DISPLAYS` の答えに `rotation=N`（L7）を足し、試験の道具が論理の PNG に戻せる。
8. 0° の出力は 1〜7 を通らない。

### 4.3 damage と buffer age（M1・S6）

- damage は今どおり論理で集める。
- **scene の pass は今の frame の damage だけ**を描く（`compose->history[slot]`・`history_whole[slot]` は既にある。turn.image は frame をまたいで残るので union は要らない）。scene の写しと戻しの rects も今の frame の分。clamp は論理の target の大きさ（`compose_target_size`）。70% の判定も論理。
- **turn pass は `compose_region()` の union**（swapchain の画像の age）を `kwl_rotate_rect` で物理に写した矩形。帳簿（`image_frames`）は今どおり swapchain の画像に対して使う。
- 不変条件: 「anchor の damage を記録した frame は必ず turn.image に描く」。present が落ちた・frame を捨てた時（`image_frames` は描く前に進む、`compose.c:2574`）は `turn.whole = 1` を立て、次の turn pass は全体を描く（age の帳簿を信じない）。`compose_region` が 0（全体）を返した時も turn は全体。
- `compose->clips`（`kwl_compose_clip_visible/draw`）は論理のまま。
- log: `KWL DAMAGE` の行に turn の物理の矩形 `turn=x,y,w,h ...` を足す（T1 が部分の frame を照合する、§11.2）。

### 4.4 mirror と extended の head（M7・L4）

- **mirror**: 元を「anchor の完成した swapchain の画像」から「anchor の scene の画像」（回転 0 なら swapchain の画像、回転ありなら `turn.image`）に変え、head の**論理**の矩形に `kwl_displays_fit(Wl_anchor, Hl_anchor, Wl_head, Hl_head)` で fit して blit（head が回転ありなら head の `turn.image` へ、その後 head の turn pass）。各 display は自分の回転に対して正立（ユーザーの確認）。barrier の layout は §4.2 の helper。回転ありの anchor の `turn.image` は常に TRANSFER_SRC なので、`compose->readback` が無い platform でも回転時だけ mirror が映る（0° は今どおり黒と `mirror unsupported`）。この非対称は log に `KWL OUTPUT mirror source=turn` で残す。
- **extended**: head の論理の矩形は回転で幅と高さが入れ替わる。`heads_record_extended` は head の framebuffer（回転ありなら head の turn）に毎回全体を描き、head の turn pass も毎回全体（§7）。`server->view_width/height` は論理。
- **配置の再検査**: `kwl_displays_validate` は論理の矩形。回転で配置が無効になったら、**回ったのが head ならその head だけ**、**回ったのが anchor なら anchor は原点のままで各 head を**既存の fallback（`kwl_displays_place_right`）で置き直し、snapshot を送る（Settings の図が追う。drag の draft は STALE で取り直し）。保存の `place=` はその結果。

### 4.5 直の scanout・overlay・cursor の plane（S3・S5）

- **game mode の直の scanout**: `kwl_scanout_facts` に `rotated` を足し、`kwl_scanout_decide` は `rotated` なら直の scanout をしない（合成に落とす、D6。180° も同じ）。`size_matches` は論理同士の比較になり、回転した出力でも真になり得る（S5）ので、`rotated` は**正しさのための必須の fact**。理由の log は `direct=0 reason=rotated`。host 試験は `kwl_scanout_decide` を 4 つの向きで（§11.1）。
- game mode の中に `set_rotation` が届いた時と、出力が閉じている・lost の時は §4.6 の 0。
- **WS214 の overlay の plane**（p005・p006 は未投入）: 回転した出力では窓を plane に載せない（`rotation != 0` → fallback）。p005 の設計に 1 行足す（foreign Phase への連絡は Q1）。
- **cursor の plane**: `plane_state()` は `compose->rotation != 0` で −1（合成の cursor）。像・hotspot の回転・位置の写像・C の変更は無し。plane を使うのは Pi 4 だけで、Pi の回転は D7 の後なので失う物は無い。plane の回転は Future Work（§13）。

### 4.6 回転の変更（実行中）（M5・M6・M10・M2）

`kwl_output_rotate(server, slot, rotation)`（output-switch.c）:

0. 変更が無ければ 0。game mode なら先に `kwl_scanout_leave(server, KWL_SCANOUT_ROTATED)`（新しい reason）し、次の pass の `enter_window_mode` → `kwl_compose_output_open` で開く時に 2 以降が走る（`config` の回転を先に書いておく）。出力が閉じている・lost の時も config だけ書き（`saved` の答え）、開く時に適用（`applied` は 1、回転は開いた時に効く）。
1. anchor の時: **新しい資源を全部先に作る**（park の外、M が作る。GPU の object の作成は park の外でよい: 使われていない object）: `turn_new`（§4.2、無理なら UNSUPPORTED）、framebuffer、scene・backdrop（`BACKDROP_UNTRIED` に戻す）・layers・壁紙と blur の host image を新しい論理の大きさで。どれかが失敗したら新しい物だけ捨てて UNSUPPORTED（`KWL OUTPUT rotate refused errno=`）。何も変わっていない。
2. `kwl_compose_park`（R が止まる）と `kwl_cursor_plane_hold`（C。回転した出力では plane を使わないので、unpark の後の `kwl_cursor_plane_update` が −1 で plane を空にする）。
3. 入力の途中の状態を終える（M10）: 指が触れていれば wl_touch.cancel の既存の経路で全部の contact を cancel、pen が下りていれば up と proximity_out を送り次の report で入り直す、窓の move/resize の grab は終える（glass の既存の終了）。pointer の button は保つ。
4. `kwl_output_size_set(server, compose->mode_width, compose->mode_height, rotation)`。swapchain・display の mode は**触らない**（modeset 無し）。古い turn・framebuffer・scene・backdrop・layers・壁紙を新しい物と入れ替え、古い物は `kwl_compose_forget` の後に R の step 1 で破棄。
5. `output_resized(server)`（既存: 窓の refit、`kwl_arrange_output_resized`、pointer の warp と clamp）。warp の move の context に**古い回転・新しい回転・物理の大きさ**を渡し、「同じ物理の点」（論理の古い位置 → 物理 → 新しい論理）を warp の中で計算する。warp は新しい `struct kwl_cursor_geometry`（§5.4: 矩形・回転・物理の大きさ・direct の既定）を同じ区間で書く。I が新しい矩形と古い回転を見ることは無い（M2）。
6. `kwl_compose_unpark`。`kwl_outputs_changed`（wl_output の mode を論理で再送、fullscreen の窓は configure が新しい大きさで届く）、`kwl_input_bindings_resync`（§5.3: 結び付けの slot の引き直し）、`kwl_displays_tell`（Settings の snapshot）、`heads_save`（displays.conf）。
7. head の時: 1・2・4（`kwl_head_size_set`）、配置の再検査（§4.4）、6。
8. park の失敗は既存（`KWL FAILED site=park`、`_exit`）。
9. log: `KWL OUTPUT rotate key=… rotation=… width=… height=… mode=WxH`（試験が読む）。

**起動時**（M5）: config の読み込み（`kwl_heads_config_load`、一生に一度のまま）を `kwl_compose_output_open` の入口へ移し、`compose_output_open_parked` の swapchain の後・`compose_targets` の**前**で `kwl_displays_rotation(&compose->config, key)` を引いて `kwl_output_size_set(..., rotation)` と turn の作成を行う（targets・scene は最初から論理の大きさで作る）。anchor の切り替え（`kwl_output_switch`）と hotplug の再 open も同じ関数を通るので、新しい display の key の `rotate=` がそのまま効く。`--rotate=N`（試験用の起動 option、p002）は config より優先。`--width/--height` が与えられた時も物理＝その大きさ、論理はその回転。

### 4.7 wl_output と client（案 E、M8・M9・L8）

- `output_events`（protocol.c:651）: geometry の transform は **0（normal）のまま**、mode の幅・高さは**論理**（`view.width/height`）、x/y は論理の plane。client は「論理の大きさの normal な出力」を見る。
- `kwl_output_view` に `rotation`・`mode_width`・`mode_height` を足す（Settings の snapshot と shot の `DISPLAYS` が使う。wl_output は使わない）。
- libkeiland は変えない（`kl_window_output_mode()` は論理を返すことになる。About の「Display 1080x1920」は利用者の見る向き）。fullscreen の configure・bounds は論理のまま。xserver・GTK4・libegl・Vulkan の WSI は変更無し。`set_buffer_transform` は 0 だけ（不変）、`preferred_buffer_transform` は送らない。
- 選ばなかった形（本当の transform を告げ、mode を物理に）: 規格に忠実だが、この tree では誰も transform を使わず、pre-rotate する client が現れると compositor が解けない（EPROTO、M9）。移植の package（Qt6・SDL・mpv）が transform を見るかは調べていない（案 E なら調べなくてよい）。案 D を入れる時に一緒に戻す（§13）。

### 4.8 Linux・FreeBSD

backend は変えない（compositor の経路 A は Vulkan の標準 API だけ、WS131 の境界に触れない）。`kl_backend_input_uniq` の追加（§5.3）は 3 つの backend の tree に同じ形で（evdev の `EVIOCGUNIQ` は 3 OS にある）。Linux・FreeBSD の backend は property を読まないので pen は全部「DIRECT でない」= 回さない・論理に直に（D11）。

### 4.9 Pi 4（S1）

- bcm2711 は shader が sample する画像を strict UIF で持ち、raster の画像（GPU が raster で描いた turn.image）は sample の前に変換する: 変わった箱が 256 KiB 以下なら **CPU**、大きければ **TFU**（`native-storage.c:25-26, 753-808`）。毎 frame の経路で CPU が tiling を並べ替えるのは Guardrail（2026-10-12）に反し、TFU でも全体をもう 1 回読み書きする。
- 正しい前提は **WS141 p015**（GPU の描画の出力を UIF で持つ）で、glass と同じ（Guardrail の補い）。p015 は planning（第 3 版）。
- 扱い（D7、推奨）: p002〜p006 は amd64・QEMU で進め、Pi 4 の回転の受け入れ（ws.md の完了の条件 4・5 の Pi の分）は p015 の後にする。p015 の前の Pi は「動く（CPU/TFU の変換あり）と記録」に留め、計測（§11.4）は変換の費用も含めて測る。
- driver 側の代案（S1-b、不採用）: bcm2711 の規則を「render target でもあり sample もされる画像は箱の大きさに関わらず TFU」に変えれば CPU は触らない（規則の字義は満たす）。しかし change ring（`bcm2711_v3d_memory_changes`）が GPU の描いた箱を知るか未確認で、知らなければ毎 frame 全体の TFU（1024x600 で 2.4 MB の読み書きがもう 1 回）。p015 がこれを根本から無くすので、別の経路は作らない。

## 5. 入力の設計

### 5.1 相対（mouse・touchpad）

変更無し。plane は利用者の見る向きなので、机の上で右へ動かせば画面の右へ動く。touchpad の端の gesture は pad の座標（不変）。

### 5.2 絶対の device の写像（M3・M4）

- device の **direct** の印: 分類（`kwl_input_probe`）で `INPUT_PROP_DIRECT` を読み `kwl_input_device.direct` に持つ。QEMU の tablet（`ABS_X/Y` の絶対の pointer）は画面全体に対応する装置なので property に関わらず **direct 扱い**（host の mouse の位置に cursor が来る）。touch screen は kernel が DIRECT を付ける（`hid-touch.c:155`、注入の touch も同じ description）。
- **写像の規則**（`kwl_output_absolute(server, slot, direct, raw_x, min_x, max_x, raw_y, min_y, max_y, &x_fixed, &y_fixed)`、heads.c。24.8 固定小数、結果は plane の座標）:
  - direct: 生 → slot の出力の**物理**の大きさ（`mode_*`）へ scale（24.8）→ `kwl_rotate_fixed` で論理へ → 出力の plane の位置 (x,y) を足す。
  - direct でない（机の上の pen tablet）: 生 → slot の出力の**論理**の大きさへ scale → plane の位置を足す。**回さない**（ユーザーの決定。tablet の板を回す必要は無い）。
- 3 つの経路の置き換え: I thread の `kwl_cursor_box_place()`（受け箱の geometry の `physical[slot]`・`rotation`・`direct` を使う、§5.4）、main の `touch.c:964-969`（`read_report`）、`tablet.c:1014-1015`（`place_pointer`）は `kwl_output_absolute()` を呼ぶ。slot は device の結び付け（§5.3、無ければ anchor）。
- **傾き**（M3）: pen の `ABS_TILT_X/Y` は device の物理の軸。direct の device では `kwl_rotate_vector` で論理の向きに回してから client へ（`tablet.c:592-598, 713` の送る所）。direct でない device は回さない。
- **kernel の pen の property**（M4、p003、driver の変更。UAPI は不変）: `hid-input.c` が pen（main の device）を登録する時に `info.properties` を決める: HID の pen の application が **Pen（`0x0d0002`）**、または同じ HID の device に **Touch Screen（`0x0d0004`）の collection がある**（LCD の pen と touch は 1 つの device、§1.7）なら `INPUT_PROP_DIRECT`、application が **Digitizer（`0x0d0001`）**だけなら `INPUT_PROP_POINTER`。これは HID の usage の意味（Digitizer = 板、Pen = 画面の上の pen）に従う一般の規則で、Linux の hid-input も同じ対応をとる（知識として。code は読まず写さない、§14）。ユーザーの LCD の descriptor（WS079 H1、未確認）が届いたら確かめ、合わなければ Settings の結び付けの card に「画面の上で使う／机の上で使う」の切り替え（`touch=` の行に印）を足す（§13 の候補）。
- **注入の pen**（試験）: `/dev/input-inject` の pen は今 property 無し。**`INPUT_INJECT_KIND_PEN` は DIRECT を付ける**（LCD の pen を模す。既存の試験は property を見ないので壊れない）に変え、机の上の板は新しい kind **`INPUT_INJECT_KIND_PEN_POINTER`（案、UAPI の追加 = `include/uapi/input-inject.h` の定数 1 つ。Q1 とユーザーの承認）**で `INPUT_PROP_POINTER`。peninject に `--pointer` の option。
- 画面の端の判定（keyboard の zone、edge の group）は論理のまま。
- 受け入れ: host 試験（§11.1）で 4 つの向き × direct/非 direct × 境界、傾きの向き。

### 5.3 pen・touch と display の結び付け（新、ユーザーの決定 D3）

- **対象**: 絶対の device の全部 — touch screen（`touch`）、pen（`tablet`）、絶対の pointer（QEMU の tablet、`absolute`）。相対の device は対象外。
- **identity**（`kwl_input_identity()`、純粋な関数、host 試験）: `BUS:VVVV:PPPP:UNIQ` の文字列（bus は `usb`・`bluetooth`・`i2c`・`virtual`・`pci`、他は `bus%u`。vendor・product は 4 桁の hex。UNIQ は `EVIOCGUNIQ` の serial、無ければ空）。vendor と product が共に 0 の時は `BUS:0000:0000:NAME`（NAME は空白を `_` に）。identity は空白を含まない（`displays.conf` の行で最初の語になる）。長さは `KWL_INPUT_IDENTITY_MAX 96`。LCD の pen と touch は同じ identity（§1.7）なので、**1 つの結び付けが pen と touch の両方に効く**。同じ機種を 2 台、serial 無しで繋ぐと区別できず同じ display に結ばれる（制限として記録。`EVIOCGPHYS` の path は抜き差しで変わるので使わない）。
- **backend の追加**: `int kl_backend_input_uniq(int descriptor, char *uniq, size_t size)`（`EVIOCGUNIQ`。3 つの backend に同じ形。`keiland-backend-evdev.h` の宣言）。`struct kwl_input_device` に `identity[]`・`direct`・`bound`（slot、`KWL_PLANE_SLOTS` = 結び付け無し）を足す。touch.c・tablet.c は `input->bound` を slot に使う。
- **保存**: `displays.conf` の `touch=IDENTITY KEY`（§6.4）。`struct kwl_display_config` に `touches[KWL_DISPLAYS_TOUCHES 8] {identity, key}`。
- **引き直し** `kwl_input_bindings_resync(server)`（input.c）: device の attach の時、出力の変化（`kwl_outputs_changed`、hotplug・切り替え・shown/off・mirror）の後、config の変更の後に、各 device の identity → key → 今 shown な display の slot（anchor か head）を引く。key の display が無い・shown でない・mirror の時は **anchor**（既定）。device の抜き差し（2 秒の scan）でも同じ。display の抜き差しで `touch=` は消さない（`place=` と同じ方針）。
- **既定**: 結び付けが無い device は anchor（今の挙動と同じ。自動の推測は後）。
- **pointer の warp**: `kwl_pointer_warp_to(server, slot, x, y)` に slot を足す（plane の座標。anchor 固定を外す）。cursor は head の上に出る（`pointer_output`）。
- **「この画面に触って」（listen）**: `listen_touch(request, key)` が届くと compositor は `displays_listen {object, request, key, until_ms}` を立てる（30 秒、`KWL_DISPLAYS_LISTEN_MS`）。立っている間に最初に来た **touch の contact の開始**（`contact_begin`）か **pen の down（`BTN_TOUCH`）か proximity_in**（`tablet.c` の `apply_report`）の device を、その key に結ぶ（config に書き、保存、`kwl_input_bindings_resync`、`kwl_displays_tell`、result OK）。その touch・pen の down は**飲み込む**（shell・client に渡さない。contact は up まで無視、pen は up まで無視）。mouse の click は受けない。30 秒で UNAVAILABLE、`listen_touch(request, "")` は立っているものを UNAVAILABLE で答え自分は OK、2 つ目の listen は BUSY、client が切れたら解除、active な session でなければ DENIED、key が shown でなければ INVALID。log `KWL INPUT bound identity=… key=… slot=N`。
- **Settings の選択**: §6.3。

### 5.4 受け箱の geometry（M2）

```
struct kwl_cursor_geometry {
	struct kwl_plane_rect outputs[KWL_PLANE_SLOTS];   /* 論理 */
	unsigned output_count;
	struct { uint32_t width, height; unsigned rotation; } physical[KWL_PLANE_SLOTS];
	unsigned absolute_slot;    /* 絶対の pointer（QEMU の tablet）の結び付け、direct 扱い */
};
```

- `kwl_cursor_box_warp(box, geometry, move, context, motion)` と `kwl_cursor_box_outputs(box, geometry)` の両方が **これを丸ごと** 1 区間で書く（今の矩形だけの `outputs[]` を置き換える）。M は `kwl_outputs()` の拡張 `kwl_output_geometry(server, &geometry)` で作る。
- `kwl_cursor_box_place()` は `geometry.absolute_slot` の出力へ §5.2 の direct の規則で写す。
- I は park しない、受け箱は 1 区間で更新、warp の番号で古い motion は捨てられる（WS213 の不変条件を保つ）。

## 6. protocol・libkeiland・Settings・保存

### 6.1 `kl_system_displays_v1` の拡張（manager の version `KL_SYSTEM_SINCE_ROTATION`、**34 案**。C1）

| 種類 | opcode | 名前と引数 | 意味 |
| --- | --- | --- | --- |
| request | 4 | `set_rotation(uint request, string key, uint rotation)` | rotation は度数 0/90/180/270（他は INVALID）。すぐ適用（Apply 不要）。result: OK（`saved` は別）、DENIED（active な session でない）、NO_KEY（key が繋がっていない。`set_shown` の not found と同じ、L9）、INVALID（度数でない）、UNSUPPORTED（turn の作成に失敗。何も変わらない）。off・limited の display は保存だけして出す時に適用（applied 1）。result は最初の frame を待たない（modeset が無い） |
| request | 5 | `bind_touch(uint request, string identity, string key)` | 絶対の device を display に結ぶ。key `""` は既定（結び付けを消す）。result: OK、DENIED、INVALID（identity が今の device に無い、または key が繋がっていない）。`saved` |
| request | 6 | `listen_touch(uint request, string key)` | 「この画面に触って」: key の display に次に触れた device を結ぶ（§5.3）。`""` は取り消し。result: OK（結んだ）、UNAVAILABLE（30 秒で来なかった、取り消された）、BUSY（別の listen が立っている）、INVALID（key が shown でない）、DENIED |
| event | 3 | `rotation(string key, uint rotation)` | snapshot で `output` の後・`done` の前に、繋がっている display ごとに（度数）。shown でない display も保存した回転を当てた値（L2: その display の `output` の width/height も回転を当てた論理で送る） |
| event | 4 | `touch(string identity, string label, uint kind, string key)` | snapshot で、絶対の device（identity ごとに 1 つ。pen と touch の node は 1 つに合わせ kind を OR）ごとに。kind は `KL_TOUCH_DIRECT 0x1`・`KL_TOUCH_PEN 0x2`・`KL_TOUCH_FINGERS 0x4`・`KL_TOUCH_POINTER 0x8`。key は結び付け（`""` = 既定）。label は device の name（無ければ identity） |

- event 3・4 は `object->version >= KL_SYSTEM_SINCE_ROTATION` の object にだけ送る。既存の `output` の signature は変えない。
- `kl-system-protocol.h`: `KL_SYSTEM_SINCE_ROTATION 34U`（**番号は Q1 が割り当て**。今の最大 33 の次）、`KL_SYSTEM_MANAGER_VERSION 34U`。`system-protocol.c`: `kl_system_displays_v1_interface` の version を `KL_SYSTEM_SINCE_ROTATION`、requests 7（`"set_rotation" "usu"`、`"bind_touch" "uss"`、`"listen_touch" "us"`）、events 5（`"rotation" "su"`、`"touch" "ssus"`）。compositor（`displays-shell.c:161-168` の形）と libkeiland（`system.c:2289` の形）は **manager の版**で判定。device の抜き差し・結び付けの変化も snapshot（全 object へ）で伝える（`KL_SYSTEM_CHANGED_DISPLAYS`）。

### 6.2 libkeiland の公開 API（`KL_VERSION` の次の番号、**93 案**、Q1 が割当。M8）

- `struct kl_display` は**変えない**（ABI。`kl_system_displays_get` の `memcpy` の大きさが変わらない）。
- `#define KL_DISPLAY_ROTATE_0 0U`・`_90 90U`・`_180 180U`・`_270 270U`。
- `int kl_system_displays_rotation(const struct kl_system *system, const char *key, unsigned *rotation);` — 最後の snapshot の値。ENODEV（key が無い）、ENOTSUP（manager が 34 未満: Settings は回転の UI を出さない）。
- `int kl_system_displays_set_rotation(struct kl_system *system, const char *key, unsigned rotation, uint32_t *request);` — ENOTSUP（34 未満）、EINVAL。result は `kl_system_take_result` で 0・EPERM・ENODEV・EINVAL・ENOTSUP（既存の対応）。
- `struct kl_touch_device { char identity[KL_TOUCH_IDENTITY_MAX 96]; char label[KL_DISPLAY_LABEL_MAX]; unsigned kind; char display[KL_DISPLAY_KEY_MAX]; }`（新しい struct）、`KL_TOUCH_DEVICES_MAX 8`、`KL_TOUCH_*` の kind。
- `size_t kl_system_touches_get(const struct kl_system *system, struct kl_touch_device *devices, size_t capacity);`
- `int kl_system_touches_bind(struct kl_system *system, const char *identity, const char *key, uint32_t *request);`（key NULL か `""` で既定）
- `int kl_system_touches_listen(struct kl_system *system, const char *key, uint32_t *request);`（key NULL か `""` で取り消し）。result は 0・ENODEV（UNAVAILABLE: 来なかった）・EBUSY・EINVAL・EPERM。
- 変化は `KL_SYSTEM_CHANGED_DISPLAYS`。`kl_window_output_mode()` は変えない（案 E）。

### 6.3 Settings の Display の頁（`page-display.c`）

- **回転**（D1・D4）: 出力の row（`display_rows`）に「Rotation」の 4 つの絵の button（倒す向き: 横置き／左辺を下に（90）／逆さ（180）／右辺を下に（270）。絵は display の card の形の小さな icon と上辺の印。文字は tooltip 無しなので絵の下に `0°`〜`270°` の小さな文字を添える）。選択中を強調。押すとすぐ `kl_system_displays_set_rotation`（draft ではない。Apply/Revert は配置と mode のまま）。request 中は row の button を無効（`display->request` の既存の形、`rotation_request` を足す）。mirror でも row に出す（各 display が自分の向き、M7）。1 台でも使える。manager が 34 未満（ENOTSUP）なら出さない。
- 配置の図（`display_card`）は snapshot の論理の `width/height` で描くので自動で縦長。card の中に上辺の向きの小さな三角。
- **Touch and pen**（新しい card、絶対の device が 1 つ以上ある時だけ）: device ごとに 1 行（label、kind の語「Touch screen」「Pen」「Pen and touch」「Tablet」、今の display の label か「Main display (default)」）。行を押すと `menu.c` の popup に display の label の一覧と「Default」を出し、選ぶと `kl_system_touches_bind`。
- **「この画面に触って」**: 各 display の row（Touch and pen の card がある時）に「Touch to assign」の button。押すと `kl_system_touches_listen(key)` と `se_dialog_open`（title「Touch <label>」、body「Touch that display with a finger or the pen. Pen tablets on the desk: touch with the pen.」、primary 無し、Cancel で `listen("")`）。result で閉じる: 0 → 「<device> now follows <label>」の message、ENODEV → 「Nothing touched it」、EBUSY → 「Another assignment is waiting」。
- 結果の文字: 保存できなかった（`saved == 0`）は既存の形。UNSUPPORTED は「This display cannot be rotated」。
- Linux/FreeBSD の build でも同じ UI（pen は全部「Tablet」= 回さない、D11）。

### 6.4 保存（displays.conf、L3）

- 行 `rotate=KEY N`（N は度数。key に空白があり得るので最後の語が値）。0 は書かない。
- 行 `touch=IDENTITY KEY`（identity は空白を含まないので最初の語、残りが key）。既定（anchor）は書かない。
- `version=1` のまま。古い build は知らない行を skip するが、**古い build が保存すると `rotate=`・`touch=` は消える**（往復で失われる。受け入れる: 一時的な抜き差しより稀で、新しい build で選び直せば戻る）。
- 行の順: `version`・`mode`・`anchor`・`brightness`・`place`・`off`（既存）の後に `rotate`、最後に `touch`。読みの上限 `HEADS_TEXT_MAX` を 8192 に上げる（16 places ＋ 16 off ＋ 16 rotate ＋ 8 touch の最長で約 5 KB）。古い build の 4096 byte の読みは新しい行だけを切る。
- `struct kwl_display_config` に `rotations[KWL_DISPLAYS_PLACES] {key, rotation}`・`rotation_count`、`touches[KWL_DISPLAYS_TOUCHES] {identity, key}`・`touch_count`。`kwl_displays_parse/format` に parse と出力、`kwl_displays_rotation(config, key)`・`kwl_displays_set_rotation()`・`kwl_displays_touch(config, identity)`・`kwl_displays_set_touch()`。度数以外（`rotate=KEY 1`）・identity に空白・上限超えは無効（行を捨てて log `KWL DISPLAYS config`、残りは読む）。
- 保存と適用は別（`applied`・`saved`）。切断した display の `rotate=`・`touch=` は保持。
- greeter（login 画面）は別の process・別の HOME で利用者の displays.conf を読まないので、**login 画面は回らない**（製品の制限、D9）。lock 画面は session の compositor なので回ったまま。

## 7. 速さと費用（M6・M11・S1）

- 回転 0 の出力: 変更無し（helper の分岐だけ）。
- 回転あり（i915・venus）: 1 frame に turn pass 1 つ（age の union の分の画素を 1 回読み 1 回書く）。全画面 1920x1080 で 8.3 MB の読み ＋ 8.3 MB の書き、60 fps で約 1 GB/s。普通の frame は damage の分だけ。5330 では無視できる。
- **Pi 4**（p015 の前）: turn pass の前に raster→UIF の変換: 箱が 256 KiB 以下なら CPU（規則違反、Pi の CPU 負荷）、大きければ TFU で全体をもう 1 回読み書き。1024x600 の全画面で turn 2.4+2.4 MB ＋ TFU 2.4+2.4 MB ≈ 0.6 GB/s（60 fps）。p015 の後は turn だけ（0.3 GB/s）。v3d の描画の DRAM の帯域と、WS214 の HVS の scanout の予算（1.5 GB/s 仮）は**別物**として書く。線形の画像を 90° で sample する時の texture cache の当たり（v3d の TMU）は未確認 → p007 の計測の項目。
- extended の回転した head は毎回全画面の turn pass（head は毎回全体を描く、§4.4）。
- **memory**（入れ替えの間は 2 倍、M6）: 1080p で turn 8.3 MB、scene 8.3 MB、壁紙と blur の host image 16.6 MB、layer の絵は層数 × 8.3 MB、backdrop 1/8。入れ替えの瞬間は約 2 倍（4 GB の前提、§master-design-policy 10）。turn の作成の失敗は UNSUPPORTED（§8）。
- 計測: `KWL PERF pass` の行に turn の時間を足す。Pi 4 で回転あり/無しの App Home の開閉・窓の drag の frame の時間、underrun の計数（p007、p015 の後）。
- GPU だけで行う（CPU は画素に触れない。cursor の plane の像の回転は無し、S3）。

## 8. 失敗と回復、並行性

| 場面 | 扱い |
| --- | --- |
| turn の画像・framebuffer・scene・layers の memory が取れない、format の能力が無い | 新しい物だけ捨てて UNSUPPORTED。何も変わらない（M6）。log `KWL OUTPUT rotate refused errno=` |
| park の時間切れ | 既存（`KWL FAILED site=park`、`_exit`） |
| displays.conf が読めない・値が不正 | その行を捨てて 0・既定（`place=` と同じ）。log `KWL DISPLAYS config` |
| game mode の中の `set_rotation`（S5） | config を書き `kwl_scanout_leave(ROTATED)`、次の open で適用。answer は applied 1 |
| 出力が閉じている・lost の時の `set_rotation` | config だけ、開く時に適用 |
| 回転中に hotplug・output switch | 回転の変更は park の中で原子的。切り替え後の open で新しい key の `rotate=` を適用（§4.6） |
| present が落ちた frame | `turn.whole = 1`、次の turn は全体（M1） |
| 回転の瞬間に指・pen・grab（M10） | touch の cancel、pen の up と proximity_out、grab の終了（§4.6 の 3） |
| listen の最中に display が抜けた・session が lock | listen を解除して UNAVAILABLE |
| 同じ identity の device が 2 つ | 両方同じ display に結ぶ（制限、§5.3） |
| fullscreen の client が古い大きさの buffer を出し続ける | configure で新しい論理の大きさを伝える。届くまで論理の中に clip（既存） |
| 試験の shot | 物理の絵。`SHOT`/`DISPLAYS` の `rotation=` で道具が論理に戻せる |
| thread | rotation・論理の大きさ・turn は M が park の中で変える。受け箱の geometry は 1 区間で書き I が読む。C は plane を使わない。R は record を再生するだけ。lock の追加は無し |

## 9. 決めてほしい点（残る問い、推奨つき。D1〜D6 は ws.md で決定済み）

| ID | 問い | 推奨 |
| --- | --- | --- |
| D7 | Pi 4 の回転（S1）: WS141 p015 の前の Pi は「動く（CPU/TFU の変換あり）と記録」に留め、Pi の受け入れは p015 の後にするか。または driver の規則を「sample される render target は常に TFU」に変える（S1-b） | **p015 の後**。p002〜p006 は amd64・QEMU で進める。S1-b は作らない |
| D8 | 回転の値の protocol と file の表し方: 度数（0/90/180/270）に統一（M13） | **度数**（`rotate=KEY 90`、request も度数。取り違えが型で分からない 0..3 を持たない） |
| D9 | login 画面（greeter）は利用者の displays.conf を読まないので回らない。製品の制限として記録するか、greeter に伝える仕組み（sessiond 経由で最後の session の向き）を Future Work にするか | **制限として記録**し、Future Work に「greeter の向き」 |
| D10 | wl_output の案 E（transform normal・mode は論理）でよいか。規格の字義（mode は物理、transform で告げる）からは外れる | **案 E**。pre-rotate する client が無く、compositor が解けない今は矛盾の無い形。案 D と一緒に戻す |
| D11 | Linux・FreeBSD の backend は `EVIOCGPROP` を読まないので pen は全部「回さない（論理に直に）」になる。この WS で Linux・FreeBSD の backend に property の読みを足すか（`EVIOCGPROP` は両 OS にある、backend の 2 file に数行） | **足す**（p003 の中、同じ形で 3 file）。Linux の build で確かめるのは build だけ（Guardrail の例外の QEMU guest は使わない） |
| D12 | 注入の pen の UAPI の追加 `INPUT_INJECT_KIND_PEN_POINTER`（`include/uapi/input-inject.h` の定数 1 つ、既存の kind は不変）と、`INPUT_INJECT_KIND_PEN` が DIRECT を付けること | **承認を求める**（UAPI の追加）。無ければ机の上の板の経路は host 試験だけ |

## 10. 敵対的レビュー（自分で、第 2 版）

| 懸念 | 評価と対処 |
| --- | --- |
| `compose->output.width/height`・`server->width` を読む所の取りこぼし | p002 の受け入れに `grep -n 'output\.width\|output\.height\|mode_width\|server->width\|server->height' userland/desktop/wayland/*.c` の全件を「物理／論理が正しい」と注記付きで表にすること（読み手も書き手も） |
| turn.image の dependency が driver で守られない（review-1 の未確認） | 同じ queue の順の submit と pass の dependency は Vulkan の規格の範囲。zedBSD の libvulkan の wire が subpass の外部 dependency を落とすなら、turn pass の前に明示の `vkCmdPipelineBarrier` を 1 つ足す（p002 で venus と i915 の両方で縦線・ちらつきを見る） |
| `texelFetch` の座標が 1 px ずれる | `gl_FragCoord` は画素の中心（+0.5）。`floor` してから式に入れる。host 試験では式を C で同じに書き（`rotate.c` を shader の oracle に）、T1 の部分 frame の照合で見る |
| scene を今の frame の damage だけで描き、turn が union を写す時、scene に無い古い画素を写す | turn.image は frame をまたいで残るので、union の矩形の画素は過去の frame で描かれている。不変条件（present が落ちたら全体）で守る（M1） |
| 回転の最中に I が report を処理する | geometry を 1 区間で書く（M2）。warp の番号で古い motion は捨てられる |
| listen の間に来た touch を飲み込むと shell の gesture の状態が狂う | contact は `ROUTE_NONE` で up まで無視（既存の「どこにも行かない」経路、`touch.c:1832`）。pen は up まで無視 |
| 2 つの LCD の pen・touch が同じ identity | serial 無しの同じ機種だけ。制限として記録。Settings は同じ identity を 1 行にし「×2」を添える |
| hid-digitizer の DIRECT の規則がユーザーの LCD に合わない | descriptor が届いたら確かめる（H1）。合わなければ Settings の切り替え（§13）。当面は注入の pen で経路を確かめる |
| `HEADS_TEXT_MAX` を上げると古い build が切れた行を読む | 新しい行を最後に書くので、古い build は `touch=`・`rotate=` の途中で切れるだけ。切れた行は知らない key として skip される（`place=` が先） |
| mirror の 0° と回転時で mirror の可否が違う | readback の無い platform は実機に無い（i915・venus は readback あり）。log に `mirror source=turn` |
| anchor の回転で head の配置が動く（L4） | 既存の fallback。Settings の draft は STALE |
| Notes の toolbar が 600 px に収まらない | D5（p007 で 1024x600 に `--rotate=90` で測る） |
| i915 の `MODE_ROTATE_*`（Linux の定数）は使わない | 新しい code は Zlib。§14 |
| HAL・UAPI | HAL は不変。UAPI は D12 の定数 1 つだけ（案）。driver の変更は hid-input の property の規則（UAPI は不変） |

## 11. 試験の計画（M14・L6・L7・S6）

### 11.1 host（実装の担当、`plan/ws217/tests/`）

- `host-rotate.{c,sh}`: `rotate.c` の点・半開の矩形・固定小数・vector・大きさ、4 つの向き × 非正方形 × 境界、「矩形の像 = 各画素の点の像の集合」の総当たり、往復、ASan/UBSan。
- `host-displays-conf.{c,sh}`: `plan/ws113/tests/host-displays.c` を**複写**して拡張（他 WS の plan は触らない、M14）: `rotate=`・`touch=` の parse と format、知らない行の skip、0・既定は書かない、度数以外・空白のある identity・上限超えは無効、行の順、4096 byte で切れた text を古い parse が読む（`place=` が残る）。
- `host-absolute.{c,sh}`: `kwl_output_absolute()` の 4 つの向き × direct/非 direct × head の位置 × 境界（24.8）、傾きの vector。`plan/ws213/tests/cursor-due-host-test.c` の fake を**複写**して `kwl_cursor_box_place` の geometry（回転・物理・absolute_slot）と warp/outputs の丸ごとの書き。
- `host-scanout-rules.{c,sh}`: `kwl_scanout_decide` の `rotated` × 4 つの向き（180° を含む）。
- `host-identity.{c,sh}`: `kwl_input_identity()`（bus・vendor・product・uniq・name の組、空白の置換、長さ）。結び付けの引き直し（key → slot、shown でない時の anchor）。
- `host-hid-pen-props.{c,sh}`: hid-input の property の規則（Pen・Digitizer・Touch Screen の有無の 4 組）を descriptor の fixture で（WS079 の host 試験の形を複写）。
- `host-kl-displays.{c,sh}`: `plan/tools/settings/host-kl-settings.c` の形で `set_rotation`・`bind_touch`・`listen_touch` の marshal、34 未満の ENOTSUP、`rotation`・`touch` の event の受け。
- build: amd64（`$(BUILD)/dynamic/zdesktop`・`$(BUILD)/bin/settings` など named target）と rpi4 の compositor、Linux・FreeBSD の Makefile の Settings と backend、warning 0。`plan/tools/keiland-os-boundary/check.sh`。kernel の host 試験（hid）。

### 11.2 T1（QEMU、virtio-gpu/Venus、1 つの image）

- image: `plan/ws217/tests/config-amd64.mk`（`plan/ws113/tests/config-amd64-p006.mk` を include、`CONFIG_INPUT_TEST_INJECT=y`（touchinject・peninject）、`ZEDBSD_TEST_SCREEN_CAPTURE=y`、Notes・Settings・wltest）。2 台目の出力は WS113 p007 の image の形。
- 撮影は keiland-shot（QMP の screendump は zedBSD では使わない、L6）。shot は物理の絵。
- AAT の scenario を `tests/scenarios/desktop/displays/rotate.md`・`rotate-input.md`・`touch-bind.md`、`tests/scenarios/apps/notes/portrait.md` に足す:
  1. **p002**: `--rotate=90`（1280x800 の virtio）で起動 → `KWL OUTPUT rotate … rotation=90 width=800 height=1280 mode=1280x800`、shot は 1280x800 で system bar が物理の右端に縦。`--file $HOME/.config/keiland/displays.conf=plan/ws217/tests/displays-rotate.conf`（`rotate=KEY 90`）でも同じ（起動の最初の frame から縦長、`KWL DISPLAYS config`）。180・270・0。
  2. **部分 frame**（S6）: 窓を少し動かして `KWL DAMAGE … turn=` の物理の矩形が `kwl_rotate_rect` の式と合う（scenario が式で照合）。SHOT に「全体を描かない」形（`SHOT partial`、test image だけ）を足し、部分の turn の後の絵に古い列が残らない（縦線の無い shot）。
  3. **p003**: 注入の touch（物理の座標）で App Home の tile を押す → `KWL TOUCH` の論理の座標が写像と合う。注入の pen（DIRECT）で物理の点に書き、`KWL TABLET` の論理の座標、傾きの向き（peninject の tilt）。`INPUT_INJECT_KIND_PEN_POINTER`（D12）で論理に直に写る。
  4. **p004**: Settings → Display → 90° の絵を押す → `KWL OUTPUT rotate`、`ZSETTINGS DISPLAY rotation`、shot。180・270・0 と戻す。session の login し直しで `rotate=` が残る。
  5. **p005**: 2 台目の出力で head の 90°（`KWL OUTPUT rotate key=<head>`）、extended の配置の再検査、mirror の正立（head の shot で絵が正立、黒い帯）。
  6. **p006**: 「Touch to assign」→ listen → 注入の touch → `KWL INPUT bound identity=… key=…`、Settings の card の表示。2 台目の出力に結んだ注入の touch の `KWL TOUCH` が head の plane の矩形に入る。抜き差し（inject の close/open）で結び付けが戻る。
  7. **回帰と周辺**: 0° の既存の smoke suite（shot の比較）。回転した出力で game mode の app（wltest の fullscreen）が `KWL SCANOUT direct=0 reason=rotated`、180° でも。回転したまま output switch（`KWL OUTPUT switch`）と hotplug（QMP で 2 台目を抜き差し）で `rotate=` が key ごとに効く。lock → unlock で回ったまま。logout → login 画面は回らない（D9 の記録）、login で回る。スクリーンキーボード（osk の open-close を縦で）、Wiseview、窓の refit（最大化・fullscreen・snap が縦の大きさ）。
  8. **D5**: 1024x600 に `--rotate=90` で Notes の toolbar が 600 px に収まるか（shot）。

### 11.3 実機（5330・5320、ユーザー／T1 の SSH）

- 5330・5320 の内蔵 panel（eDP）で 90°（panel を反時計回りに倒す向きの絵を押す）→ 縦長の desktop、mouse・touchpad の向き、Notes。写真（ユーザー）。
- 5330 の touch screen（内蔵）で縦の touch。
- ペン LCD を HDMI の 2 台目として縦置き（**左辺を下に**: 90 の絵）、「Touch to assign」で LCD の pen を LCD に結ぶ → pen で Notes に書く（WS079 L3 の機材。ユーザー）。descriptor の DIRECT の確認（H1）。
- 実機の証拠は QEMU と分けて記録。

### 11.4 Pi 4（T1・Q1 経由、D7 により WS141 p015 の後）

- 1024x600 の LCD で 90°。`KWL PERF pass` の turn の時間、App Home の開閉と窓の drag の frame の時間（回転あり/無し）、underrun、texture cache（90° の sample の時間）。p015 の前に試す時は「CPU/TFU の変換あり」と明記して記録だけ。

## 12. 実装の段（Phase）と受け入れ（S4・L10）

| Phase | 内容 | 受け入れ | 依存 |
| --- | --- | --- | --- |
| p002 | compositor の中核（anchor）: `rotate.c`、`mode_width/height` と `kwl_output_size_set()`（書き手 5 か所）、turn（画像・2 つの pass・layout の helper・`turn.frag`・pipeline）、`compose_target_size/layout/image`、damage（scene は今の frame、turn は union、`turn.whole`、`KWL DAMAGE turn=`）、scene/backdrop/layers/glass の大きさ、`kwl_output_rotate`（資源を先に作る、park、cancel、同じ物理の点、geometry の struct の M 側）、config の `rotate=`（parse/format/適用は open の入口）、wl_output の論理（案 E）、`kwl_output_view` の拡張、scanout の `rotated` fact と `KWL_SCANOUT_ROTATED`、cursor の plane の停止、`--rotate=N`、`KWL OUTPUT rotate`、`SHOT`/`DISPLAYS` の `rotation=`、`SHOT partial` | host 11.1 の rotate・displays-conf・scanout-rules PASS、amd64・rpi4 の build warning 0、grep の表（§10）、T1 11.2 の 1・2・7 の回帰と game mode（test-wait） | WS213 p004 の scoped な出力（main 済み） |
| p003 | 入力（anchor）: `struct kwl_cursor_geometry`（warp/outputs の丸ごと）、`kwl_cursor_box_place` の direct の写像、`kwl_output_absolute()`、touch・tablet の置き換え、`direct` の分類（`INPUT_PROP_DIRECT`）、傾き、kernel の hid-input の property の規則、注入の pen の DIRECT と `PEN_POINTER`（D12）、Linux・FreeBSD の backend の `EVIOCGPROP`（D11） | host 11.1 の absolute・hid-pen-props PASS、kernel の host 試験、T1 11.2 の 3 | p002 |
| p004 | protocol・libkeiland・Settings の回転: version 34（`set_rotation`・`rotation`）、getter/setter、`KL_VERSION 93`（Q1 が番号）、`page-display.c` の 4 つの絵と card の印、shown でない display の論理の snapshot、結果の文字 | host 11.1 の kl-displays、Settings の named build（amd64・Linux・FreeBSD）、T1 11.2 の 4 | p002、**p003**（S4: Settings から回せる状態を入力の前に main に出さない） |
| p005 | head の回転: `head->mode_*`・`kwl_head_size_set`、head の turn、extended と mirror（元を scene の画像に、論理の fit）、配置の再検査（L4）、head の `rotate=` と snapshot | T1 11.2 の 5（2 台目の virtio）、host の配置の再検査 | p002、p004 |
| p006 | pen・touch と display の結び付け: `kl_backend_input_uniq`（3 backend）、identity、`touch=`、`bound` と `kwl_input_bindings_resync`、`kwl_pointer_warp_to(slot)`、listen、protocol（`bind_touch`・`listen_touch`・`touch`）、libkeiland の `kl_system_touches_*`、Settings の card・menu・「Touch to assign」の dialog | host 11.1 の identity・displays-conf・kl-displays、T1 11.2 の 6 | p003、p005（head に結ぶため）。Q1 の判断で p006a（kernel/backend/compositor/config/log）と p006b（protocol/libkeiland/Settings）に割ってよい |
| p007 | 確かめ: T1 の scenario の全部（11.2 の 7・8 を含む）、5330・5320（ユーザー）、D5 の Notes の幅（要れば WS079 へ）、Pi 4 は D7 | 11.2〜11.3 の証拠、ws.md の完了の条件 1〜5 の判定（Pi 4 は「動く／動かない所」と p015 待ちの記録でよい） | p002〜p006 |
| p008 | 規約の全文の見直し（`plan/coding-style.md`、`style-check.py`、boundary の checker）。p007 の直しの後に当てる | 違反 0、build warning 0 | p007（L10） |

- 1 Queue は 1 Phase。p002 は大きいので、Q1 の判断で p002a（`rotate.c`・`mode_*`・`kwl_output_size_set`・config・wl_output・`--rotate`・scanout の fact・cursor の plane の停止。turn 無しで「論理の大きさだけ入れ替わる」所まで。単独では T1 に出さない）と p002b（turn・damage・scene/backdrop/layers・`SHOT partial`）に割ってよい。
- 変更する file の見込み: `wayland/rotate.{c,h}`（新）、`compose.{c,h}`、`backdrop.c`、`scene.c`、`layer-cache.c`、`glass.c`、`heads.c`、`displays.{c,h}`、`displays-shell.c`、`output-switch.c`、`protocol.c`、`scanout.c`・`scanout-rules.{c,h}`、`cursor-box.{c,h}`、`cursor-plane.c`、`touch.c`、`tablet.c`、`input.c`、`input-thread.c`、`kwl.h`、`main.c`、`shot.c`、`shaders/turn.frag` と `shaders.h`、`Makefile` 3 つ、`libkeiland-backend/keiland-backend-evdev.h`、`libkeiland-backend-{zedbsd,linux,freebsd}/input-*.c`、`libkeiland/system/{kl-system-protocol.h,system-protocol.c,system.c}`、`include/keiland/keiland.h`、`settings/page-display.c`・`settings.h`、kernel `src/drivers/generic/hid-input.c`・`input-inject.c`、`include/uapi/input-inject.h`（D12）、`userland/tests/peninject/main.c`。HAL・GPU の UAPI・GPU の driver・libvulkan は変えない。

## 13. 選ばなかった事と Future Work（Q1 が future-work.md へ）

- hardware の plane の回転（案 C、D2）: i915 の Y-tiled scanout と UAPI の transform。契機: i915 で turn pass の費用が問題になった時。
- `set_buffer_transform` の 0 以外の受け入れ（案 D）と、wl_output の本当の transform（案 E の撤回）。契機: pre-rotate する client の移植。
- cursor の plane の回転（Pi 4）。契機: Pi 4 で回転した出力の cursor の遅れが問題になった時（WS214 の C の持ち主の移しと一緒）。
- greeter（login 画面）の向き（D9）。契機: 常に縦置きの機械。
- 結び付けの自動の推測（内蔵の touch → 内蔵の panel、1 台の LCD の pen → その LCD）と、Settings の「画面の上で使う／机の上で使う」の切り替え（hid の規則が合わない device のため）。契機: ユーザーの LCD の descriptor の確認（H1）。
- 加速度 sensor による自動の回転（driver が無い）。
- Notes の toolbar の折り返し（D5、WS079）。
- Pi 4 の S1-b（常に TFU）: p015 で不要になる見込み。

## 14. ライセンスと転記

- 新しい code は全て Zlib（`Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib`）。この設計で参照したのは Wayland の `wl_output.transform` の意味（protocol の XML、MIT）、Vulkan の `VkSurfaceTransformFlagBitsKHR` と render pass の dependency（仕様）、HID の Usage Tables（USB-IF の公開の仕様: Digitizer・Pen・Touch Screen の application の意味）だけで、code の転記は無い。
- hid-input の property の規則（Pen → DIRECT、Digitizer → POINTER）は Linux の hid-input が同じ対応をとることを**知識として**知っているが、Linux の source は読まず写さない。zedBSD の規則は HID の usage の意味から自分で書く（§5.2）。
- i915 の `MODE_ROTATE_*`・`PLANE_CTL_ROTATE_*`（Linux の GPL の header から写した定数、出典は `intel/plane.h:77-80`）はこの設計では使わない。
- weston・mutter・sway・libinput の回転・結び付けの実装は読んでいない（設計は自分の tree の事実から）。

## Event

- 2026-10-13 / ws217-p001-design-v1: P4 が第 1 版を書いた（source は agent/p4 28e5fe929）。
- 2026-10-13 / ws217-p001-review-1: design-reviewer の敵対的レビュー（致命的 2・重大 6・中 14・軽微 10、[review-1.md](review-1.md)、main f50beb833 で照合）。
- 2026-10-13 / ws217-p001-design-v2: P2 が第 2 版を書いた（review-1 の全指摘に対応、対応の表は冒頭。ws.md のユーザーの決定 D1〜D6 と「回転は display ごと」「pen・touch と display の結び付けを含める」を設計に入れた。source は a07ad4cc7。新しい残る問い D7〜D12）。コードは書いていない。Q1 の review-2 待ち。
