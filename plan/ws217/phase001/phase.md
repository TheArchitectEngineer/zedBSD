<!-- awesome-plan project=zedbsd record=ws217-p001 -->

# ws217-p001: 設計 — 画面の回転（display ごとの 0°・90°・180°・270°）と pen・touch と display の結び付け

Parent: [WS217](../ws.md)
Status: planning（第 3 版、2026-10-13 P2。第 2 版は P2、[review-2](review-2.md) の全指摘に対応し、ws.md の「ユーザーの決定（2026-10-13、scanout の回転）」を入れた。Q1 の review-3 待ち）
Disposition: normal
Primary Milestone: MG006（WS から継承）
Queue / attempts: Q1 の指示（2026-10-13、P4「第 1 版」、P2「review-1 の全指摘に対応した第 2 版」、P2「review-2 の全指摘と scanout の回転の決定を入れた第 3 版」）。コードは書かない。
Purpose / goal: Settings の Display に display ごとの回転を足し、縦長で Notes を使える（ペンで書ける）ようにするための、kernel の UAPI と driver・libvulkan・compositor・入力・protocol・Settings・保存・試験の設計と、実装の段の分け方。ユーザーの決定（2026-10-13、ws.md）により、pen・touch と display の結び付けも含む。
Prerequisites: 無し（調べは §1）。実装の段は WS213 p004 の **scoped な出力**（park、`struct kwl_cursor_box`、I の warp の番号）と WS113（displays.conf・`kl_system_displays_*`）の main に統合済みの code を前提にする（WS213 p004 は in-progress、`plan/ws213/phase004/phase.md:6`。契約が変わったら追従）。Pi 4 の段（p011）は WS214 p003a/p003b（`display-flip.c` の作り直し、実装済み・Pi と T1 待ち）の main への統合を前提にする。

## 第 2 版との差分の要約（2026-10-13 P2）

| 項目 | 第 2 版 | 第 3 版 |
| --- | --- | --- |
| **180° の経路**（ユーザーの決定、ws.md「scanout の回転」） | 4 つの向きとも compositor の turn pass | **180° は表示の hardware が回す**: UAPI に plane の回転の印、i915 は `PLANE_CTL_ROTATE_180`、libvulkan は `VkDisplaySurfaceCreateInfoKHR.transform`・`supportedTransforms`・`preTransform` で ROTATE_180 を受ける、compositor は surface を transform つきで開き直す。Pi 4 は HVS の左右・上下の反転の組。§5 |
| **90°・270° の経路** | 同上 | compositor の turn pass のまま（Intel は Tiger Lake も含め全部。Y-tiled の scanout はしない、Future Work）。Pi 4 は WS141 p015 の後、それまで UNSUPPORTED（M8） |
| **Venus（QEMU）** | 回転できる | 回転は**無効表示**（driver が 180 の印を出さないので何も出さない。試験のためだけに `--rotate-policy=any`、D13） |
| **回転の能力の告知**（新） | 無し | display ごとの選べる角度の mask と無効の理由を protocol の event `rotations` で Settings へ。Settings は選べない絵を無効にし理由を書く。§4.10・§7.1・§7.3 |
| turn pass の swapchain 側の pass（C3） | `compose_pass`（CLEAR）に scissor | 全体の時だけ CLEAR、部分の時は `pass_load`（LOAD、renderArea は物理の damage の箱）。§4.2 |
| 起動時・open の経路の回転（C4） | `compose_output_open_parked` の `compose_targets` の前 | `compose_display` の中（display の名前が決まった直後）で config を読んで当て、glass・scene・layers を最初から論理の大きさで。open の全経路で論理の大きさを前後で比べ、違えば `output_resized`。§4.6 |
| `--width/--height`（M1） | `kwl_output_size_set()` を呼ぶ | 頼まれた大きさは `server->size_width/size_height`、既定 320x240 もそこに。`server->width/height` を書くのは `kwl_output_size_set()` の中の 2 行だけ。§4.1 |
| glass・backdrop の入れ替え（M2） | 先に作って `output_resized` | `output_resized` を「glass を作り直すか」で分け、回転では先に作った壁紙・blur を入れ替える。backdrop は先に作らず、古い target を R の破棄に回して UNTRIED。§4.6 |
| turn.image の pass の選び方（M3） | 無し | `compose_target_pass(compose, slot, load)`。3 か所と head の pass をこれから取る。§4.2 |
| protocol の版の分け方（M4） | 版 34 を p004 と p006 で分けて入れる | **(a)** p004 で版 34 の wire（requests 7・events 6）と `KL_VERSION` の次の API を全部入れ、compositor は p006 まで `bind_touch`・`listen_touch` に UNSUPPORTED、`touch` の event は送らない。§7.1 |
| head に結んだ touch（S7） | 端・zone の判定は anchor | contact が結び付けの slot と出力の論理の矩形を持つ。clamp はその矩形。**edge・corner・keyboard の zone の gesture は anchor だけ**。§6.3 |
| mirror の中の結び付け（S8、Q1 が推奨を採用） | anchor に写す | head の物理 → head の回転の逆 → head の論理 → fit の逆写像 → anchor の論理。§6.2 |
| head の回転（S9） | 1・2・4・再検査・6 | その head に結んだ device の cancel、head の上の窓の refit、置き直しの前後で `heads_windows_follow`。§4.4・§4.6 |
| identity の UNIQ（M5） | そのまま | 印字できる ASCII の空白以外に限り他は `_`。§6.4 |
| direct の決め方（M6・D11） | property で | touch screen と絶対の pointer は常に direct、pen だけ property（Linux・FreeBSD も読みを足した後は同じ）。§6.1 |
| LCD の descriptor（M7） | 未確認（H1） | BUG-267 の dump で確認済み。規則は `input->pen == HID_REPORT_PEN_DISPLAY` → DIRECT、`_TABLET` → POINTER。§1.7・§6.1 |
| listen の結び方（M9） | down か proximity_in | pen は **down だけ**。§6.5 |
| 机の上の板の縦横比（M10） | 無し | 制限として記録、Future Work。§14 |
| 段（新しい ID、既存の ID は保つ） | p002〜p008 | p002（compositor の turn）、**p009**（scanout の 180°: UAPI・i915・libvulkan）、**p010**（compositor の scanout の 180° と能力の policy）、p003（入力）、p004（protocol・Settings、能力の告知を含む）、p005（head）、**p011**（Pi 4: HVS の反転と turn の停止）、p006（結び付け）、p007（確かめ）、p008（規約）。§13 |
| 軽微 L11〜L25 | — | 全部直した（対応の表） |

## review-2 の指摘への対応

| ID | 対応 | 所在 |
| --- | --- | --- |
| C3 | 直した。turn pass は `turn.whole` か `compose_region()` が全体の時だけ CLEAR の pass、部分は `pass_load`（LOAD、renderArea = 物理の damage の箱、scissor は矩形ごと）。`SHOT partial` の判定に「damage の外に背景色の帯が無い」 | §4.2 の 6、§12.2 の 2 |
| C4 | 直した。(a) 回転は `compose_display` の中（`compose.c:1358` の display_name の後）で config を読んで当て、glass・scene・layers は最初から論理。(b) open の全経路で論理の大きさを前後で比べ、違えば `output_resized` 相当を必ず走らせる。`output-switch.c:257-259` の比較は論理同士。(c) T1 に `KWL GLASS` の壁紙の大きさと「回転したまま壁紙を変える」 | §4.6、§1.1、§12.2 の 1 |
| S7 | 直した。contact は結び付けの slot と出力の論理の矩形を持つ。clamp はその矩形。edge・corner・keyboard の zone は anchor だけ（head では出さない、と決めた）。p006 の file に `touch.c`・`corner.c`・`edge.c`。T1 に「head の上の drag で窓が動く」「head の縁の swipe は出ない」 | §6.3、§13、§12.2 の 6 |
| S8 | 直した（Q1 が推奨を採用）。mirror の head に結んだ direct の device は fit の逆写像で anchor の論理へ。T1 に mirror の 1 件 | §6.2、§12.2 の 6 |
| S9 | 直した。head の回転にも cancel・refit・`heads_windows_follow`。T1 に head の最大化の窓と anchor の回転で動いた head の窓 | §4.4、§4.6 の 7、§12.2 の 5 |
| M1 | 直した。`server->size_width/size_height`（既定 320x240 もここ）、`compose_display` で native か頼まれた大きさを mode にして `kwl_output_size_set()`。grep に `server.width =` | §4.1、§1.1 |
| M2 | 直した。`output_resized(server, glass)`: 回転の park の経路は先に作った壁紙・blur を入れ替えて glass の作り直しを飛ばす。backdrop は target を R の破棄に回して UNTRIED（`kwl_backdrop_destroy` は呼ばない。`pass_load` は残る） | §4.6 の 1・4・5 |
| M3 | 直した。`compose_target_pass(compose, slot, load)`。`compose.c:2789-2791, 2876`・`backdrop.c:127`・`heads.c:1262` をこれから取る。受け入れの grep に `compose->pass\b`・`pass_load` | §4.2 の 3、§13 |
| M4 | 直した（a）。版 34 の wire は p004 で全部、compositor は p006 まで UNSUPPORTED。`KL_VERSION` の次の番号も p004 で全 API | §7.1、§7.2、§13 |
| M5 | 直した。UNIQ も印字できる ASCII の空白以外、他は `_`。96 で切った衝突は制限。host-identity に空白・制御文字・長い serial | §6.4 |
| M6 | 直した。touch screen（MT で POINTER でない）と絶対の pointer は常に direct、pen だけ property。§4.8・§7.3 を D11 に合わせた | §6.1、§4.8、§7.3 |
| M7 | 直した。BUG-267 の dump（if1 = Pen、if0 = Touch Screen）、parser の `HID_REPORT_PEN_DISPLAY/_TABLET`（`hid-report.c:2801-2823`、`hid-report.h:36-41`）を主の規則に、Touch Screen の collection は補い。fixture は BUG-267 の 2 つの rdesc。§14 の契機を直した | §1.7、§6.1、§12.1、§14 |
| M8 | 直した（Q1 が推奨 (a) を採用）。rpi4 の build は turn を止め（`KWL_TURN_DISABLED`）、Pi では 90/270 が UNSUPPORTED（Settings は理由つきで無効表示）。測る時だけ `--rotate-policy=any` | §4.9、§4.10 |
| M9 | 直した。pen は down（`BTN_TOUCH`）だけ。dialog の文のまま | §6.5 |
| M10 | 直した。制限として §14 と Future Work | §14 |
| L11 | 直した。key が無い時は UNAVAILABLE（libkeiland は ENODEV）、`set_shown` と同じ | §7.1 |
| L12 | 直した。「anchor の place は保ち」 | §4.4 |
| L13 | 直した。`BLIT_DST` を足した | §4.2 の 1 |
| L14 | 直した。turn の pipeline は `turn_new` で作る（`kwl_compose_output_prepare` の経路。失敗は OK の前に分かる） | §4.2 の 1 |
| L15 | 直した。swapchain の framebuffer は残す | §4.2 の 4 |
| L16 | 直した。`direct` の field を消した（QEMU の tablet は常に direct）。`absolute_slot` が 1 つなのは制限 | §6.6、§14 |
| L17 | 直した。backend は `input-zedbsd.c` と共有の `input-evdev.c` の 2 file、`userland/desktop/include/keiland/keiland.h`、`scanout.c:288`、D7〜D12、書き手の数 | §1、§13 |
| L18 | 直した。`ROUTE_IGNORED` | §11 |
| L19 | 直した。注入の identity は `virtual:0000:0000:NAME`。pen と pen-pointer の名前を分ける。1 つの結び付けが pen と touch に効くことは host 試験で | §6.4、§12.1 |
| L20 | 直した。p002a の間は `rotate=`・`--rotate` の 0 以外を UNSUPPORTED | §13 |
| L21 | 直した。config.mk は WS217 の `tests/` に複写 | §12.2 |
| L22 | 直した。ws.md の D5 の文に注記（第 2 版以降は p007） | ws.md |
| L23 | 直した。T1 の依頼を 5 つの束に | §13 |
| L24 | 直した。`kwl_cursor_plane_update` の publish を C の park の解除の前に（p002 の 1 行）。Pi だけ・1 frame の制限は無くなる | §4.5 |
| L25 | 直した。`set_shown` と同じ信頼の形（active な session のどの client も）。続けて呼ぶ client が touch を飲み続けられることを記録 | §6.5 |

## 0. 要点（design-reviewer・Q1 向け）

- **180° は表示の hardware が回す**（ユーザーの決定）。UAPI に plane の回転の印（`GPU_DISPLAY_ROTATE_180` の能力、present と plane_state の `ROTATE_180`）、i915 は既にある `i915_skl_plane_ctl_rotate()`（`plane.c:778-803`）へ `hw.rotation = MODE_ROTATE_180` を通すだけ（linear のまま 180 が可: 同じ tree の MIT の参照 `skl_universal_plane.c:1423-1444` は linear で断るのを REFLECT_X と 90/270 だけにしている。Intel の公開の PRM で確かめる段を p009 に置く）。libvulkan は display の `supportedTransforms` に ROTATE_180 を足し、surface の `transform` と swapchain の `preTransform` に受ける。compositor は surface を transform つきで開き直す（swapchain の作り直し、modeset 無し。論理の大きさは変わらない）。Pi 4 は HVS の位置の word の 2 つの反転 bit（tree にある値 `0x80008000`、`list.c:331`）で 180°。
- **90°・270° は compositor の turn pass**（第 2 版の案 A のまま、C3・M3 を直した）: 論理の大きさの turn.image に描き、1 pass で swapchain へ回す。Intel は全部（Tiger Lake も）。Pi 4 は WS141 p015 の後、それまで rpi4 の build で止める（UNSUPPORTED）。
- **能力の policy**（§4.10）: display ごとに `hw180`（libvulkan が告げる ROTATE_180）と `turn_allowed`（build と `hw180`）から選べる角度の mask を作り、`rotations` の event で Settings へ。hardware が 180 を回せない display（Venus、古い driver、Linux・FreeBSD の KMS）は**回転を出さない**（ユーザー「Venus では回転できなくていい、無効表示」）。試験のためだけに `--rotate-policy=any`。
- **論理と物理を分ける**（C2・C4・M1）: 物理は `compose->mode_width/height`、論理は `server->width/height`（書き手は `kwl_output_size_set()` の 2 行だけ）。回転は `compose_display` で config から当て、open の全経路で論理の大きさを前後で比べる。
- **入力**: 相対は不変。touch screen と絶対の pointer は常に direct（物理 → 逆回転 → 論理）、pen は `INPUT_PROP_DIRECT` で分け、机の上の板は論理に直に。head に結んだ指は head の矩形で clamp、mirror は fit の逆写像で anchor へ。
- **pen・touch と display の結び付け**: identity（bus・vendor・product・serial、空白を `_` に）で display の key に結ぶ。Settings の card か「この画面に触って」（pen は down だけ）。`touch=IDENTITY KEY`。
- **client**: wl_output は transform normal のまま、mode は論理（案 E）。
- **残る問い** D13〜D17（§10）。決定済み D1〜D12 は ws.md。

## 1. 調べた事実（source、file:line は agent/p2-ws215 = main 217936c4c の tree。review-2 が main `5f096bb42` で照合した表は保ち、第 3 版で読み直した行だけ更新）

### 1.1 compositor の出力の持ち方

| 事実 | 所在 |
| --- | --- |
| desktop は 1 つの plane。anchor（主の出力）が slot 0、head N は 1+N。`struct kwl_plane_rect {x,y,width,height}`、`KWL_PLANE_SLOTS 8`、`KWL_PLANE_ANCHOR 0` | `wayland/plane.h:12-43` |
| anchor の大きさは `server->width/height/refresh`、swapchain は `compose->output`（`.width/.height`）。`server->width/height` は今、swapchain と mode を開く要求の大きさを兼ねる（C2） | `kwl.h:1147-1150` |
| **`server->width/height` を書く所（全部、6 か所）**: 既定 320x240（`main.c:104-105`）、`--width/--height`（`main.c:419, 433`、`size_given`。`compose` はまだ無い: M1）、anchor の open で display の native の大きさ（`compose.c:1381-1385`）、output switch の新しい display と失敗の戻し（`output-switch.c:218-219, 230-231`） | — |
| **`server->width/height` を mode・swapchain の大きさとして読む所**: `vkdemo_display_open_on(..., server->width, server->height, ...)`（`compose.c:307`）、`compose_refresh(...)`（`compose.c:1389`）、game mode の `kl_backend_scanout_open(server->backend, server->width, server->height, ...)`（`scanout.c:288`）、head は `vkdemo_display_open_on(..., head->width, head->height, ...)`（`heads.c:837`）。switch の `width != old_width`（`output-switch.c:257-259`）は**論理同士の比較**（C4）で、この表には入れない | — |
| `kwl_compose_open` は `compose_display`（`compose.c:114`）の後に `kwl_glass_open`（`compose.c:158-165`）を呼ぶ。glass は壁紙と blur の host image を `server->width/height` で作る（`glass.c:766, 1159`）。`kwl_glass_resize` は quiesce と `vkDeviceWaitIdle` の後に古い画像を**先に捨てて**作り直す（`glass.c:565-597`）。呼び手は `output_resized` だけ（`output-switch.c:666-670`）。壁紙の loader は大きさの一致で写す（`glass.c:730-731`）、`wallpaper_draw` は今の `server->width/height` で行を書く（`glass.c:843`）。uv は `server->width/height` を基準（`glass.c:2761-2764`） | — |
| `output_resized()`（glass の作り直し、窓の refit、pointer の warp と clamp）。switch は `width != old_width` の時だけ呼ぶ | `output-switch.c:658-688, 257-259` |
| `kwl_outputs()` が plane の矩形を作る。wl_output に送る値は `struct kwl_output_view {index,x,y,width,height,refresh}` | `kwl.h:2004-2013, 2295-2297`、`heads.c:1615-1690` |
| `struct kwl_head {open, lost, display, name, output, views[], framebuffers[], rendered[], acquired, x, y, width, height, refresh, dirty, wallpaper, showed, in_frame, image, global}`。head の大きさは `heads_open` の `kwl_compose_display_read(&head->width, ...)`（`heads.c:829-837`） | `compose.h:235-256` |
| anchor の plane の位置は `compose->output_x/output_y`（原点とは限らない、`heads.c:569-617`、wl_output の x/y も `heads.c:728-731`）。apply の経路は `heads_windows_follow` で head の上の窓と pointer を動かす（`heads.c:617-619`） | — |
| mode は `compose->display_mode` | `compose.h:577-579` |

### 1.2 描画と scanout の経路

| 事実 | 所在 |
| --- | --- |
| anchor の surface は `vkdemo_display_open_on(instance, physical, display, width, height, &output)`（`transform = IDENTITY` 固定、`display.c:133`）、swapchain は `preTransform = IDENTITY`（`display.c:322`）。capabilities に IDENTITY が無い display は断る（`display.c:285-287`、compositor も `compose.c:1351-1352`）。compositor は surface を `kwl_compose_output_prepare`（`compose.c:307`）、swapchain を `compose_output_open_parked`（`compose.c:380-430`、readback の可否で usage を落とす）で作る。head も同じ helper（`heads.c:837, 859`） | `userland/tests/vkdemo/display.c:98-145, 270-330` |
| 主の render pass `compose->pass`: `loadOp CLEAR`、`initialLayout UNDEFINED`→`finalLayout PRESENT_SRC_KHR`、外部 dependency は `COLOR_ATTACHMENT_OUTPUT → COLOR_ATTACHMENT_OUTPUT`。`pass_load`: `LOAD`、`PRESENT_SRC_KHR`→同（backdrop が一度だけ作り、`kwl_backdrop_destroy` が壊す） | `compose.c:1715-1750`、`backdrop.c:305-335, 180-184` |
| frame の始めは `compose->pass`、部分 frame（`region != NULL`）か再開は `pass_load`、`renderArea` は damage の箱、`pass_area`・`scissor_now`（M3 の 3 か所: `compose.c:2789-2791, 2876`、`backdrop.c:127`。head は `compose->pass` で毎回全体 `heads.c:1262-1279`） | `compose.c:2780-2830, 2868-2882` |
| backdrop の target は M の record の最初の使用で作られる（`backdrop.c:68-80`、`output.width/height / BACKDROP_SCALE`）。`BACKDROP_UNTRIED/READY/FAILED` | `backdrop.c:60-135` |
| 投影の行列は無い。quad は CPU で NDC を計算して push constant に | `compose.c:2482-2523`、`glass.c:1905-1962`、`batch.c:156-233` |
| damage は論理で集め、`compose_region()` が age の履歴を足し、`compose->output.width/height` に clamp、70% で全体。`image_frames[image] = frame` は描く前に書かれる | `compose.c:2560-2660, 2574` |
| scene の写し（buffer→image の戻しを含む）、shot、mirror の元の barrier は `PRESENT_SRC_KHR` を前提 | `scene.c:160, 213`、`compose.c:2779`、`shot.c:298, 334`、`heads.c:1196-1219` |
| mirror: anchor の完成した swapchain の画像を `vkCmdBlitImage`（LINEAR）で `kwl_displays_fit(compose->output.width, ...)` の矩形へ。`compose->readback` が無ければ黒 | `heads.c:1160-1230` |
| game mode: `size_matches` は client の buffer を `server->width/height` と比べる（`scanout.c:189`）。backend の claim は `kl_backend_scanout_open(backend, width, height, &scanout)`（transform 無し）。`kwl_scanout_leave(server, reason)` | `scanout.c:185-190, 288`、`keiland-backend-display.h:84`、`scanout-rules.h:48-61` |
| cursor の plane: `plane_state()` が −1 で合成の cursor（`state < 0` は plane を空にする、`cursor-plane.c:332-335`）。`kwl_cursor_plane_update` は C の park を先に解いてから publish する（`cursor-plane.c:314-335`、L24） | `cursor-plane.c:300-360, 395, 470-520` |
| shot は完成した swapchain の画像。test image だけの機能（`shot.c:8-12`）。`DISPLAYS` の答えは `server->width` を返す（`heads.c:686-687`） | `shot.c:27, 274-340, 500-575` |

### 1.3 driver と UAPI の回転の能力（第 3 版で読み直した）

| platform | 事実 | 所在 |
| --- | --- | --- |
| UAPI | `gpu_display_info.flags` の bit は 1〜2048 まで使用（`CONNECTED`〜`HAS_OVERLAYS`）、`reserved` が 1 つ。`gpu_display_present.flags` は `PRESENT_FIFO 1`・`PRESENT_BLOB 2`。`gpu_display_plane_state.flags` は `PLANE_SHOWN 1`・`PLANE_IMAGE 2`。rotation/transform の field は無い。核（gpu.c）は present の flags を `FIFO` か `FIFO|BLOB` に限り（`gpu.c:3530-3531`）、plane の flags を `SHOWN|IMAGE` に限る（`gpu.c:4057`） | `include/uapi/gpu-display.h:19-70, 189-205, 319-345`、`src/drivers/gpu/gpu.c:3525-3540, 4050-4060` |
| i915 | `hw.rotation = MODE_ROTATE_0` の 2 か所（`drv_i915_plane_emit` の `plane.c:293`、`drv_i915_lcd_ms_plane_prepare` の `plane.c:378`）。**`i915_skl_plane_ctl_rotate()` は既にあり、`MODE_ROTATE_180` → `PLANE_CTL_ROTATE_180`**（`plane.c:778-803`）、`i915_skl_plane_ctl()` が `rotation & MODE_ROTATE_MASK` で呼ぶ（`plane.c:884-930`）。PLANE_CTL は flip のたびに計算し直して書く（`modeset.c:2750` が `ms->fb_*` で `plane_prepare`、`plane.c:302-306` は noarm → arm）。`fb_*` は `head->cfg`（`head.c:876-881`）と `ms->fb_*`（`modeset.c:3085-3097`）が持つ。scanout は linear XRGB8888 だけ（`internal.h:2525-2530` の「rotation 0」の注記、`plane.c:333-346`）。`plane_count = 1`、`HAS_CURSOR` 無し（`display.c:2761, 3377`）。present の op は `drv_i915_present_display_present`（`present.c:337`、`request->flags` の BLOB だけ見る `present.c:422, 440`）。display version は `drv_i915_lcd_display_ver()`（`modeset.c:283-286`） | — |
| i915 の参照（MIT） | tree の中の Linux の i915 の参照 `plan/ws031/linux-parity/linux-reference/ubu-i915-src/display/skl_universal_plane.c`（SPDX MIT、WS031 の固定の参照環境）: `rotation & REFLECT_X && modifier == LINEAR` → 断る（1431-1435）、90/270 は Y/Yf tiling が要る（1437-1442）、**180 は linear で断られない**。`DISPLAY_VER >= 13` は `ROTATE_0 | ROTATE_180`、それ以前は 90/270 も（2402-2407）、`>= 11` は `REFLECT_X` も（2409-2410）。Q1 が示した v6.17 の `skl_universal_plane.c:2959-2964` と同じ内容 | — |
| bcm2711（Pi 4） | HVS の plane の位置の word（unscaled は word 1）の `0x80008000` を readout が「flipped」と読み（`list.c:329-332`）、console の探索は flipped の plane を飛ばす（`list.c:263-267`）。flip の側は位置の word を `words[1] = 0`（frame、`display-flip.c:1698`）、`(y << 16) | x`（cursor `2382`、overlay `2803, 2835`）で書き、反転の bit は立てない。display_query の flags は `CONNECTED | ACTIVE | FIFO | HAS_CURSOR | HAS_OVERLAYS`（`display-device.c:698`）。present の flags の検査は `display-device.c:915, 1124`。texture は strict UIF で sample し、raster の画像は変わった箱が 256 KiB 以下なら CPU、大きければ TFU で変換（S1） | `bcm2711/list.c`、`display-flip.c`、`display-device.c`、`native-texture.c:8, 119`、`native-storage.c:25-26, 753-808` |
| venus | cursor・commit・overlay は NULL。display_query の flags は `CONNECTED/ACTIVE/POWER_CONTROL/REFRESH_COUNTER`、`plane_count 1` | `venus/display.c:154-167, 795-815` |
| libvulkan | display の property は `supportedTransforms = IDENTITY`（`wsi.c:125`）、surface の作成は `create_info->transform != IDENTITY` を `VK_ERROR_INITIALIZATION_FAILED`（`wsi.c:620-624`）、`surface->display_info = *create_info`（`wsi.c:662`）。surface の capabilities は `supportedTransforms = currentTransform = IDENTITY`（`wsi-display.c:584-585`）、swapchain は `preTransform != IDENTITY` を断る（`wsi-swapchain.c:1213-1215`）。kernel の flags は `vulkan_wsi_output.flags` に別の番号で写す（`VULKAN_WSI_OUTPUT_*` 1〜32、`wsi-internal.h:20-27`、`wsi-display.c:296-372`）。present は `gpu_display_present.flags = FIFO | BLOB`（`wsi-display.c:1412`、cursor と overlay は自分の present `1395-1397`）、commit の plane の state は `display_overlay_state()`（`wsi-display.c:3595-3625`、`flags = SHOWN | IMAGE`） | — |
| Linux・FreeBSD | libvulkan-compat の KMS は legacy（`SETCRTC`・`PAGE_FLIP`）、IDENTITY。DRM の plane の rotation の property は使っていない | `libvulkan-compat/kms.c:405-435` |

### 1.4 入力の経路

| 事実 | 所在 |
| --- | --- |
| device の分類: pen tablet（`BTN_TOOL_PEN`・`ABS_PRESSURE`・両軸）、touch pad（MT protocol B ＋ `INPUT_PROP_POINTER`）、touch screen（MT protocol B で POINTER でない）、keyboard、絶対の pointer（`ABS_X/Y`）、相対の pointer。**`INPUT_PROP_DIRECT` は読んでいない** | `input.c:131-200, 880-900, 1210-1245` |
| `struct kwl_input_device {fd, live, attached, closing, pointer, keyboard, absolute, tablet, touch, touchpad, reading, abs_*, ...}`。`KWL_INPUT_MAX 32`、scan は 2 秒ごと | `kwl.h:96-106, 382-422` |
| 絶対の device はどれも anchor に写る: QEMU の tablet は I thread の `kwl_cursor_box_place()`、touch screen は main の `read_report()`、pen は main の `place_pointer()` | `cursor-box.c:131-159`、`touch.c:964-969`、`tablet.c:1006-1028` |
| 受け箱 `struct kwl_cursor_box` は `outputs[KWL_PLANE_SLOTS]`・`output_count`。`kwl_cursor_box_warp()` は矩形と位置を 1 区間で、`kwl_cursor_box_outputs()` は矩形だけ。`kwl_input_sync()` が毎 pass 呼ぶ | `cursor-box.h:112-144`、`cursor-box.c:170-225`、`input.c:485-508` |
| `kwl_pointer_warp_to(server, x, y)` は anchor に固定 | `input.c:647-666, 1750-1765` |
| **touch.c は指の位置を anchor の大きさで判定・制限する**（S7）: keyboard の zone（`touch.c:1019-1021`）、edge の group（`touch.c:1608, 1613, 1758-1761, 1848`）、shell へ渡す点の clamp（`touch.c:2082-2085`）。角の gesture も anchor（`corner.c:632-635`）。「どこにも行かない」の route は `ROUTE_IGNORED`（`touch.c:111-118`、L18） | — |
| tablet の device は name・vendor・product（USB の時）を持ち、client に `TABLET_NAME`・`TABLET_ID`・`TABLET_PATH` で告げる。傾きは device の軸のまま client へ | `tablet.c:120-160, 539-549, 1318-1328`、`tablet.c:592-598, 713` |
| surface の座標は plane の位置 − surface の位置（出力の項は無い） | `seat.c:593-594`、`touch.c:2288-2289`、`tablet.c:1149-1150` |

### 1.5 client と protocol

| 事実 | 所在 |
| --- | --- |
| wl_output.geometry は transform 0、physical size 0。mode は `view.width/height`。scale は 1 | `protocol.c:651-705` |
| `set_buffer_transform` は 0 以外、`set_buffer_scale` は 1 以外を EPROTO。zedBSD の libwayland は interface の表に無い opcode の event を EPROTO にする（`libwayland/wire.c:897-905`）、proxy の版を interface の版で切る（`libwayland/proxy.c:852-855`） | `protocol.c:1036-1047` |
| fullscreen の configure と bounds は `server->width/height` | `protocol.c:1729-1731, 1838-1839` |
| libkeiland は `kl_window_output_mode()`（「first screen's current mode」）。Settings の About と Display の plain の表示が使う | `libkeiland/ui/window.c:1010-1015, 2799-2871`、`settings/page-display.c:395-426` |
| xserver は wl_output を見ない。xdg_output は無い | `xserver/wayland.c:1184-1189` |
| Notes は窓の大きさから page を両軸で fit。toolbar は折り返さない | `notes/geometry.c:343-376`、`notes/ui.c:270-276` |

### 1.6 Settings と保存

| 事実 | 所在 |
| --- | --- |
| `KL_SYSTEM_SINCE_*` は manager の版の通し番号。今の最大は `KL_SYSTEM_SINCE_PHONE_CALLS 33U` = `KL_SYSTEM_MANAGER_VERSION 33U`（main と全 agent branch で 34 は空き、review-2）。displays の object は manager の版で作られ、compositor は `object->version >= KL_SYSTEM_SINCE_SHOWN`、libkeiland は `manager_version < KL_SYSTEM_SINCE_SHOWN` で判定 | `kl-system-protocol.h:324, 366-422, 686-702, 974-985`、`system-protocol.c:506-528`、`displays-shell.c:161-168`、`system.c:2275-2300` |
| `kl_system_displays_v1`: request `destroy(0)`・`apply(1)`・`set_brightness(2)`・`set_shown(3)`、event `output(0)`・`done(1)`・`result(2)`。`set_shown` の not found は `ENOENT → KL_SYSTEM_RESULT_UNAVAILABLE`（`displays-shell.c:819-832`）、libkeiland は UNAVAILABLE → ENODEV、NO_KEY → ENOENT（`system-view.c:1044-1047`） | `kl-system-protocol.h:103-134, 534-551, 944-954` |
| snapshot: anchor は `server->width/height`、head は `head->width/height`、shown でない display は `kwl_compose_display_read()` の物理の mode | `displays-shell.c:420-470` |
| 公開 API: `struct kl_display {key,label,x,y,width,height,refresh_mhz,flags,brightness}`（`memcpy` で写す）、`KL_VERSION 92`（次は Q1 が割当） | `userland/desktop/include/keiland/keiland.h:49, 1982-2042`、`system.c:2150-2163` |
| Settings の Display の頁: `struct se_display {displays[], count, mode, draft[], ..., request, light_request, shown_request, message}`、row・card・mode・Apply/Revert・light。dialog は `se_dialog_*`、popup の menu は `menu.c` | `settings/page-display.c:1-78`、`settings.h:583-605, 787-820, 1669-1676` |
| `displays.conf`（`version=1`、`mode=`、`anchor=`、`brightness=`、`place=KEY X Y`、`off=KEY`）。知らない key は skip、`=` の無い切れた行も skip（`displays.c:583-586, 654-656`）。読みは 4096 byte（`HEADS_TEXT_MAX`）。path は `$HOME/.config/keiland/displays.conf`（greeter は別の process・別の HOME） | `displays.h:56-60, 78-106`、`heads.c:57-61, 126-168, 1461-1487`、`handoff.c:9-19` |
| `kwl_heads_config_load` は一生に一度（`config_loaded`）、parse と `display_mode` だけ（副作用無し、早めてよい） | `heads.c:126-168`、`compose.c:440-472` |

### 1.7 入力の device の identity と property（結び付けのため）

| 事実 | 所在 |
| --- | --- |
| zedBSD の backend は `EVIOCGNAME`・`EVIOCGID`・`EVIOCGBIT`・`EVIOCGPROP` を読む（`input-zedbsd.c:288-291`）。**`EVIOCGUNIQ` は読んでいない**。Linux・FreeBSD は共有の 1 file `libkeiland-backend/evdev/input-evdev.c`（`Makefile.linux:34`、`Makefile.freebsd:29`）で property を読まず空 | `keiland-backend-evdev.h:36-70`、`input-zedbsd.c:120-175, 270-300`、`input-evdev.c:289-304` |
| kernel の evdev は `EVIOCGUNIQ`・`EVIOCGPHYS`・`EVIOCGPROP` を持つ。USB の unique_id は serial（空白・制御文字を含み得る: M5）、I2C-HID は NULL、注入は `BUS_VIRTUAL` で vendor・product 0 | `generic/input.c:66, 2355-2385`、`usb-hid.c:856-863, 1363`、`i2c-hid.c:1286`、`input-inject.c:466-468, 657-660` |
| HID の pen と touch は 1 つの identity で 2 つの evdev の node。touch の node は `touch_description.properties`（screen は DIRECT、pad は POINTER、`hid-touch.c:155, 174`）、pen の `info.properties` は 0 | `hid-input.c:300-340` |
| **parser は pen の collection を分けている**: Pen（`0x0d0002`）→ `HID_REPORT_PEN_DISPLAY`、Digitizer（`0x0d0001`）→ `HID_REPORT_PEN_TABLET`、無し → `_NONE`（`hid-report.c:2801-2823`、`hid-report.h:36-41`）、`input->pen` に持つ（`hid-input.c:536`） | — |
| **ユーザーの touch LCD の descriptor は確認済み**（M7）: [BUG-267](../../bugs/BUG-267.md) の 5330 の dump `plan/bugs/BUG-267/touchscreen-if0.rdesc`・`touchscreen-if1.rdesc`（kernel の名前 "Pen and multitouch sensor"）: interface 1 は Pen（0x0d・0x02）、interface 0 は Touch Screen（0x0d・0x04）。別の `hid_input` だが identity（USB の vendor・product・serial）は同じ | — |
| 注入の device: `struct input_inject_setup {magic, kind, x_max, y_max, report_contacts, reserved}`、`INPUT_INJECT_KIND_PEN 1`、pen は property 無し | `include/uapi/input-inject.h:87, 114-125`、`input-inject.c:640-680` |

## 2. 用語と向きの約束

- **物理（physical）**: panel の画素の並び。`compose->mode_width/height`（= swapchain `compose->output.width/height`）、`head->mode_width/height`。swapchain・viewport・scanout・shot・driver・cursor の plane の mode の照合。
- **論理（logical）**: 利用者が見る向きの desktop の画素。`server->width/height`、`head->width/height`、plane の矩形、窓・shell・gesture・wl_output・Settings の図。90°・270° では幅と高さが入れ替わる。
- **回転の値 `rotation`**: 内部（`compose->rotation`・`head->rotation`）・protocol・displays.conf・log の全部で**時計回りの度数** `0`・`90`・`180`・`270`（`unsigned`、定数 `KWL_ROTATE_0/90/180/270`、公開は `KL_DISPLAY_ROTATE_*` 同値）。「90°」は絵を panel の上で時計回りに回すことで、panel を反時計回りに倒した（左辺が下）利用者に合う。Wayland の enum への変換 `kwl_rotate_to_wayland()` は `rotate.h` に置くが案 E では呼ばない。
- **scanout の回転 `scanout_rotation`**（新）: 表示の hardware が画像に当てる回転。この設計では `0` か `180`。画像の画素 (x, y) を出力の (Wp−1−x, Hp−1−y) に出す。
- **turn の回転 `turn_rotation`**（新）: compositor の turn pass が当てる回転（`0`・`90`・`270`。`--rotate-policy=any` の時だけ `180` もあり得る）。
- **不変条件**: `rotation` は `scanout_rotation` と `turn_rotation` の合成で、どちらか一方だけが 0 でない（`kwl_rotate_plan`、§4.10）。論理と物理の写像は `rotation` だけで決まり、写像の関数は scanout か turn かを知らない。
- **写像**（物理の幅 Wp・高さ Hp。90/270 では Wl=Hp・Hl=Wp）。純粋な module `rotate.c`/`rotate.h`（review-2 が式を確かめた）:
  - 点 論理 (x,y) → 物理 `kwl_rotate_point`: 0: (x, y)、90: (Wp−1−y, x)、180: (Wp−1−x, Hp−1−y)、270: (y, Hp−1−x)。逆 `kwl_rotate_point_back`: 90: (Y, Wp−1−X)、180: (Wp−1−X, Hp−1−Y)、270: (Hp−1−Y, X)。
  - 半開の矩形 `kwl_rotate_rect`: 90: X∈[Wp−y1, Wp−y0)、Y∈[x0, x1)。180: X∈[Wp−x1, Wp−x0)、Y∈[Hp−y1, Hp−y0)。270: X∈[y0, y1)、Y∈[Hp−x1, Hp−x0)。逆 `kwl_rotate_rect_back` も同じ形。
  - 固定小数（24.8）の点 `kwl_rotate_fixed`（画素の式を 1/256 の単位で）。vector（傾き）`kwl_rotate_vector`: 物理 (TX,TY) → 論理: 90: (TY, −TX)、180: (−TX, −TY)、270: (−TY, TX)。大きさ `kwl_rotate_size`、`kwl_rotate_swaps(rotation)`。
  - host 試験（§12.1）は 4 つの向き × 非正方形で総当たり、往復、境界。

## 3. 案と選択

| 案 | 内容 | 採否と理由 |
| --- | --- | --- |
| **A'. 180° は scanout、90°・270° は turn pass（採用）** | 180° は surface の transform（hardware の plane の回転）、90°・270° は論理の大きさの turn.image に描いて 1 pass で swapchain へ回す。0° は今の経路 | ユーザーの決定。180° は追加の pass も画像も無く（Pi 4 でも UIF の変換が要らない）、i915 の port に `PLANE_CTL_ROTATE_180` の計算が既にある。90/270 を hardware でやると Intel は Y-tiled の scanout（display version 13 以降は不可）、HVS は無い |
| A. 4 つの向きとも turn pass（第 2 版） | — | 180° に限り hardware の方が安い。turn の機構は残す（90/270、試験の policy の 180） |
| B. NDC の関門で quad を回す | — | 不採用（全 shader の画素の向きの依存を直すことになる） |
| C. hardware の plane の 90/270 | — | 不採用、Future Work（Y-tiled の scanout） |
| D. client に pre-rotate させる | — | 不採用（pre-rotate する client は無い） |
| E. wl_output は transform normal・mode は論理（採用、§4.7） | — | 採用（第 2 版のとおり、D10） |
| 回転の能力の gate を「hardware の 180 の有無」に置く（採用、§4.10） | hardware が 180 を回せない display は回転を一切出さない | Venus を「virtual」と見分ける libvulkan の口を作らずに済む（Venus・Linux/FreeBSD の KMS・古い driver は driver が印を出さないだけ）。代案「libvulkan に virtual の印を足す」は Vulkan の標準の面を越える口が要り不採用。帰結（Linux・FreeBSD では回転が無効）は §14 に制限として記録、D14 |
| 180° の変更を swapchain の作り直し無しに（present の flag を毎 frame 変える） | — | 不採用。Vulkan の `preTransform` は swapchain の性質。作り直しは output switch の reopen と同じ経路（`KWL OUTPUT reopen`）で、180° の変更は稀 |

## 4. compositor の設計

### 4.1 状態と所有（C2・M1）

- **物理の mode**: `compose->mode_width/mode_height`（anchor）、`head->mode_width/mode_height`（head）。swapchain・mode の refresh・game mode の claim・切り替えの戻し・shot・cursor の plane の照合はこれを読む。
- **論理**: `server->width/height`、`head->width/height`。plane・窓・shell・gesture・wl_output・Settings の snapshot・damage・scene・backdrop・layers・glass の画像。
- **頼まれた大きさ**（M1）: `server->size_width/size_height`（既定 320x240、`--width/--height` が書く、`size_given`）。`main.c:104-105, 419, 433` はこれを書く。
- **回転**: `compose->rotation`・`compose->scanout_rotation`・`compose->turn_rotation`、head も同じ 3 つ（度数）。所有は M、変更は §4.6 の経路だけ。
- **能力**: `compose->hw180`（この display の surface の `supportedTransforms` に ROTATE_180 がある）、`compose->rotations`（選べる角度の mask）、`compose->rotate_why`（§4.10）。head も同じ。
- **`server->width/height` を書く唯一の関数** `kwl_output_size_set(server, mode_width, mode_height, rotation)`（output-switch.c）: `compose->mode_* = mode_*`、`compose->rotation = rotation`、`server->width/height = kwl_rotate_size(mode, rotation)`。呼び手: `compose_display`（native の大きさ、または `size_given` なら `size_*`、回転は config から: §4.6「起動時と open」）、`output-switch.c:218, 230`（新しい display と戻し。戻しは古い mode と古い回転）。head は `kwl_head_size_set(head, mode_width, mode_height, rotation)`。p002 の受け入れ: `grep -n 'server->width =\|server->height =\|server\.width =\|server\.height =' userland/desktop/wayland/*.c` が `kwl_output_size_set` の中の 2 行だけ。
- **turn**: `struct kwl_turn { VkImage image; VkDeviceMemory memory; VkImageView view; VkFramebuffer framebuffer; VkDescriptorSet set; VkPipeline pipeline; uint32_t width, height; unsigned rotation; unsigned whole; }` を anchor（`compose->turn`）と head（`head->turn`）に 1 つ。`turn_rotation == 0` では `image == VK_NULL_HANDLE`。framebuffer は 1 つ。寿命は出力の open/close と回転の変更。持ち主は M、破棄は `kwl_compose_forget` の後に R の step 1（WS213 4.7）。

### 4.2 turn の経路（anchor、`turn_rotation ≠ 0`）（S2・C3・M3）

1. **turn.image と pipeline の作り方** `turn_new(compose, slot)`: format は `compose->format`、tiling OPTIMAL、usage `COLOR_ATTACHMENT | SAMPLED | TRANSFER_SRC | TRANSFER_DST`。作る前に `vkGetPhysicalDeviceFormatProperties` の `optimalTilingFeatures` に `SAMPLED_IMAGE`・`COLOR_ATTACHMENT`・`TRANSFER_SRC`・`TRANSFER_DST`・`BLIT_SRC`・`BLIT_DST`（L13: mirror は turn.image から head の turn.image へ blit）があることを確かめ、無ければ UNSUPPORTED。**turn の pipeline（`turn.frag`、SPIR-V の compile は kernel の driver）もここで作る**（L14）: `kwl_compose_output_prepare` の経路（surface と pipelines を作る所）で、OK を答える前に失敗が分かる。
2. **layout**: turn.image は pass の外では常に `SHADER_READ_ONLY_OPTIMAL`。turn 用の render pass を 2 つ: `turn_clear_pass`（`UNDEFINED`→`SHADER_READ_ONLY_OPTIMAL`、CLEAR）、`turn_load_pass`（`SHADER_READ_ONLY_OPTIMAL`→同、LOAD）。attachment の format と sample count は既存と同じ（pipeline は互換）。外部 dependency: 入口 `srcStage = FRAGMENT_SHADER | TRANSFER`、`srcAccess = SHADER_READ | TRANSFER_READ | TRANSFER_WRITE` → `dstStage = COLOR_ATTACHMENT_OUTPUT`、`dstAccess = COLOR_ATTACHMENT_READ | WRITE`。出口はその逆。同じ queue に順に submit されるので frame N の turn pass（読み）と N+1 の scene pass（書き）の WAR が閉じる。
3. **helper**（M3）: `compose_target_pass(compose, slot, load)`（turn なら `turn_clear_pass`／`turn_load_pass`、無ければ `compose->pass`／`pass_load`。`pass_load` はこの helper が無ければ作る: backdrop の `backdrop.c:305-335` の作成をここへ移し、`kwl_backdrop_destroy` は `pass_load` を壊さない）、`compose_target_layout(compose, slot)`（`PRESENT_SRC_KHR` か `SHADER_READ_ONLY_OPTIMAL`）、`compose_target_image(compose, slot)`、`compose_target_size(compose, slot, &w, &h)`（論理）。使う所: pass の 3 か所 `compose.c:2789-2791, 2876`・`backdrop.c:127` と head の `heads.c:1262`、barrier と写しの元 `scene.c:160, 213`・`compose.c:2779`・`shot.c:298, 334`・`heads.c:1196-1219`。0° では今の値が返る。
4. `compose_targets`: turn ありなら framebuffer を `turn.view`（論理）で作り、**swapchain の framebuffer（`compose->framebuffers[]`）も残す**（turn pass が使う、L15）。pass の `renderArea`・viewport・scissor・`pass_area`・`scissor_now`・`compose->clips` は論理（`compose_target_size()`）。
5. scene・backdrop・layers・glass の host image は論理の大きさ。
6. **turn pass**（C3）: scene の pass（cursor の合成を含む）の後、swapchain の画像を attachment にした pass で全画面の quad を描き、`turn.frag` が push constant（rotation、Wp、Hp）から `gl_FragCoord`（物理、`floor` してから）を `kwl_rotate_point_back` と同じ式で論理に戻し `texelFetch(turn, ivec2(x, y), 0)`。**pass の選び方**: `turn.whole` か `compose_region()` が 0（全体）の時は `compose->pass`（CLEAR、`UNDEFINED`→`PRESENT_SRC_KHR`、renderArea 全体）、部分の時は `pass_load`（LOAD、`PRESENT_SRC_KHR`→同、**renderArea は物理の damage の箱**、scissor は矩形ごと）。acquire の semaphore の待ちは両方の pass の外部 dependency（`COLOR_ATTACHMENT_OUTPUT`）で足りる。pipeline `turn`（layout: 既存の quad の push constant の後ろに turn.frag の constants、descriptor set は turn.image 1 つ）。
7. R の record の列: `compose_record` の最後に turn pass。`kwl_shot_record` は turn の後。`SHOT`・`DISPLAYS` の答えに `rotation=N scanout=S turn=T`（L7）。
8. `turn_rotation == 0` の出力は 1〜7 を通らない（180° の scanout の出力も通らない: 今の経路のまま、§4.11）。

### 4.3 damage と buffer age（M1 of review-1・S6）

- damage は論理で集める。**scene の pass は今の frame の damage だけ**（`compose->history[slot]`・`history_whole[slot]`）。clamp は `compose_target_size`。70% も論理。
- **turn pass は `compose_region()` の union**（swapchain の画像の age）を `kwl_rotate_rect` で物理に写した矩形。帳簿（`image_frames`）は swapchain の画像に対して。
- 不変条件: 「anchor の damage を記録した frame は必ず turn.image に描く」。present が落ちた・frame を捨てた時は `turn.whole = 1`。`compose_region` が全体を返した時も turn は全体。
- `KWL DAMAGE` の行に `turn=x,y,w,h ...`（物理）を足す。

### 4.4 mirror と extended の head（M7 of review-1・L4・L12・S9）

- **mirror**: 元を「anchor の scene の画像」（turn ありなら `turn.image`、無ければ swapchain の画像）に変え、head の論理の矩形に `kwl_displays_fit(Wl_anchor, Hl_anchor, Wl_head, Hl_head)` で fit して blit（head が turn ありなら head の `turn.image` へ、その後 head の turn pass。head が 180° の scanout なら swapchain の画像へそのまま: hardware が回す）。各 display は自分の回転に対して正立。barrier の layout は §4.2 の helper。回転時だけ `compose->readback` 無しでも mirror が映る非対称は log `KWL OUTPUT mirror source=turn`。
- **extended**: head の論理の矩形は回転で入れ替わる。`heads_record_extended` は head の framebuffer（turn ありなら turn）に毎回全体、head の turn pass も毎回全体。
- **配置の再検査**: `kwl_displays_validate` は論理の矩形。無効になったら、回ったのが head ならその head だけ、anchor なら **anchor の place は保ち**（L12）各 head を `kwl_displays_place_right` で置き直し、**置き直しの前後の矩形で `heads_windows_follow`**（S9: 届かない窓を残さない）、snapshot を送る（draft は STALE）。保存の `place=` はその結果。

### 4.5 直の scanout・overlay・cursor の plane（S3・S5・L24）

- **game mode**: `kwl_scanout_facts` に `rotated`（`rotation != 0`、180° の scanout も含む）を足し、`rotated` なら合成に落とす（D6）。backend の claim に transform の口を足して 180° を直で出すのは Future Work（D17）。理由の log `direct=0 reason=rotated`。
- game mode の中に `set_rotation` が届いた時と、出力が閉じている・lost の時は §4.6 の 0。
- **WS214 の overlay の plane**: `rotation != 0`（180° の scanout も）では窓を plane に載せない（p005 の設計に 1 行、連絡は Q1）。
- **cursor の plane**: `plane_state()` は `rotation != 0` で −1。L24: `kwl_cursor_plane_update` の空の像の publish を C の park の解除の**前**に（`cursor-plane.c:314-335` の順を入れ替える、p002 の 1 行）。plane を使うのは Pi 4 だけ。

### 4.6 回転の変更（実行中、起動時、open の全経路）（M5・M6・M10・M2 of review-1、C4・M1・M2・S9）

`kwl_output_rotate(server, slot, rotation)`（output-switch.c）:

0. 変更が無ければ 0。mask に無い角度は UNSUPPORTED（何も変えない）。game mode なら config を書いて `kwl_scanout_leave(server, KWL_SCANOUT_ROTATED)`、次の pass の `enter_window_mode` → `kwl_compose_output_open` が config から当てる。出力が閉じている・lost の時も config だけ（`saved`、applied 1）。
1. `plan = kwl_rotate_plan(rotation, hw180, turn_allowed, policy)`（§4.10）。**scanout の回転が変わる時**（0 ↔ 180）: config を書き、`kwl_compose_output_close` → `output_prepared = 0` → `kwl_compose_output_open`（output switch の reopen と同じ経路。open が config から plan を作り、surface を新しい transform で作り直す。§4.11）。open が論理の大きさを前後で比べ、違えば 5。以下 2〜6 は通らない（open の中で済む）。log は `KWL OUTPUT rotate ... reopen=1`。
   **turn の回転だけが変わる時**（0 ↔ 90/270、90 ↔ 270）: anchor は**新しい資源を全部先に作る**（park の外、M が作る）: `turn_new`（無理なら UNSUPPORTED）、framebuffer、scene・layers の画像、**壁紙と blur の host image**（`wallpaper_create` の形で新しい論理の大きさに描く。M2）。どれかが失敗したら新しい物だけ捨てて UNSUPPORTED（`KWL OUTPUT rotate refused errno=`）。backdrop は先に作らない（M2）。
2. `kwl_compose_park`（R が止まる）と `kwl_cursor_plane_hold`。
3. 入力の途中の状態を終える: 指は wl_touch.cancel の既存の経路で全部の contact を cancel（head の時は**その head に結んだ device の contact だけ**、S9）、pen が下りていれば up と proximity_out、窓の move/resize の grab は終える。pointer の button は保つ。
4. `kwl_output_size_set(server, compose->mode_width, compose->mode_height, rotation)`、`compose->turn_rotation` を更新。swapchain・mode は触らない。古い turn・framebuffer・scene・layers・壁紙・blur を新しい物と入れ替え、古い物は `kwl_compose_forget` の後に R の step 1 で破棄。**backdrop は古い 2 つの target を同じ破棄の列に回し（pass は残す）、`BACKDROP_UNTRIED` に戻す**（次の record で新しい大きさ。失敗は今どおり `BACKDROP_FAILED`）。`kwl_backdrop_destroy` は呼ばない（`pass_load` を壊すため、M2）。
5. `output_resized(server, glass_rebuild = 0)`（M2: `output_resized` を「glass を作り直すか」の引数で分ける。回転の park の経路は 1 で入れ替えたので飛ばし、open・switch の経路は今どおり `kwl_glass_resize`）: 窓の refit、`kwl_arrange_output_resized`、pointer の warp と clamp。warp の move の context に古い回転・新しい回転・物理の大きさを渡し「同じ物理の点」を warp の中で計算、`struct kwl_cursor_geometry`（§6.6）を同じ区間で書く。
6. `kwl_compose_unpark`。`kwl_outputs_changed`、`kwl_input_bindings_resync`、`kwl_displays_tell`、`heads_save`。
7. head の時: 1（head の surface の作り直しは `heads_open` の経路、または head の turn の資源）・2・3（その head の device）・4（`kwl_head_size_set`）、**head の上の窓の refit**（最大化・fullscreen・snap を新しい論理で。`heads_windows_follow` と `kwl_glass_output_resized` の head 版。実装は head の mode が変わる既存の経路が無いことを確かめた上で足す）、配置の再検査（§4.4、`heads_windows_follow`）、6。
8. park の失敗は既存。
9. log: `KWL OUTPUT rotate key=… rotation=… scanout=… turn=… width=… height=… mode=WxH reopen=0|1`。

**起動時と open の全経路**（C4・M1）:

- `compose_display`（`compose.c:1358` の display_name の直後）で `kwl_heads_config_load`（parse だけ、一生に一度のまま）を呼び、`kwl_displays_rotation(&compose->config, key)` を引き、`--rotate=N`（試験、config より優先）があればそれを使い、`kwl_rotate_plan` で plan を作り、`kwl_output_size_set(native か size_*, rotation)` を呼ぶ。以後の `kwl_glass_open`・scene・layers は最初から論理の大きさ（壁紙の loader の大きさの一致も論理と合う）。
- `kwl_compose_output_open`（`compose_output_open_parked` を含む全経路: 起動、game mode の後の `enter_window_mode`、output switch、hotplug の reopen）は入口で「この display の key の config（と `--rotate`）」から plan を作り直し、**open の前の論理の大きさと後の論理の大きさを比べ、違えば `output_resized(server, 1)` と `kwl_outputs_changed`・`kwl_input_bindings_resync`・`kwl_displays_tell` を必ず走らせる**。`output-switch.c:257-259` の `width != old_width` はこの比較に置き換わる（論理同士。同じ mode で回転だけ違う display への switch でも走る）。surface は plan の `scanout_rotation` の transform で作る（§4.11）、turn の資源は `turn_rotation` で作る（`compose_targets` の前）。
- `--width/--height` が与えられた時も物理＝その大きさ、論理はその回転。

### 4.7 wl_output と client（案 E、決定 D10）

- `output_events`: transform は 0 のまま、mode の幅・高さは論理、x/y は論理の plane。`kwl_output_view` に `rotation`・`mode_width`・`mode_height` を足す（Settings の snapshot と shot の `DISPLAYS` が使う）。libkeiland・xserver・GTK4・libegl・Vulkan の WSI は変更無し。`set_buffer_transform` は 0 だけ。本当の transform は Future Work。

### 4.8 Linux・FreeBSD

- compositor の経路は Vulkan の標準 API だけ。libvulkan-compat の KMS は IDENTITY のままなので **`hw180` が無く、回転は無効表示**（§4.10 の gate。DRM の plane の rotation の property を使うのは Future Work、§14）。
- `kl_backend_input_uniq` と `EVIOCGPROP` の読み（D11）は zedBSD の `input-zedbsd.c` と共有の `input-evdev.c` の 2 file。読みを足した後は Linux・FreeBSD でも pen を DIRECT で分ける（M6）。

### 4.9 Pi 4（S1・M8・D7）

- **180°**: HVS の反転の組で hardware が回す（§5.4、p011）。turn も UIF の変換も要らない。
- **90°・270°**: bcm2711 は shader が sample する画像を strict UIF で持ち、raster の turn.image は sample の前に CPU（256 KiB 以下）か TFU で変換する。毎 frame の CPU の並べ替えは Guardrail（2026-10-12）に反する。正しい前提は WS141 p015（planning、第 3 版）。**p015 の前は rpi4 の build で turn を止める**（`KWL_TURN_DISABLED`、§4.10）: Settings は 90/270 を理由つきで無効表示、`set_rotation(90)` は UNSUPPORTED。測る時（§12.4）だけ `--rotate-policy=any` で許し「CPU/TFU の変換あり」と記録する。p015 が turn を UIF で描けるようにしたら build の印を外す（p015 の受け入れの 1 項目として Q1 が連絡）。
- driver 側の代案（常に TFU）は作らない（第 2 版の S1-b のまま）。

### 4.10 回転の能力の policy（新）

純粋な関数（`rotate.c`、host 試験）:

- 入力: `hw180`（libvulkan の `VkDisplayPropertiesKHR.supportedTransforms` と surface の capabilities に `VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR` がある）、`turn_build`（build の定数: amd64 は 1、rpi4 の build は 0 = `KWL_TURN_DISABLED`。p015 の後に rpi4 も 1）、`policy`（`KWL_ROTATE_POLICY_DEFAULT` か `_ANY`、`--rotate-policy=any` の試験の option、log `KWL OUTPUT rotate policy=any`）。
- `turn_allowed = turn_build && hw180`（policy ANY なら `turn_build` だけ）。**hardware が 180 を回せない display は回転を一切出さない**: Venus（driver が印を出さない）、Linux・FreeBSD の KMS、p009 の前の i915、p011 の前の bcm2711。理由: 「回せる panel」の印を driver に出させるのが、virtual の display を見分ける最も小さい口（§3）。
- `rotations` の mask: `KL_DISPLAY_ROTATION_0 1` は常に、`_180 4` は `hw180`、`_90 2`・`_270 8` は `turn_allowed`。policy ANY では 4 つ全部（180 は hw180 なら scanout、無ければ turn）。
- `why`（無効の理由の bit、Settings の文に使う）: `KL_DISPLAY_ROTATE_WHY_SCANOUT 1`（hw180 が無い: 「This display cannot be rotated」）、`KL_DISPLAY_ROTATE_WHY_TURN 2`（hw180 はあるが `turn_build` が 0: 「90° and 270° are not available on this device yet」）。
- `kwl_rotate_plan(rotation, hw180, turn_allowed, policy, &scanout, &turn)`: 0 → (0,0)。180 → hw180 なら (180,0)、policy ANY なら (0,180)、他は UNSUPPORTED。90/270 → turn_allowed なら (0,rotation)、他は UNSUPPORTED。
- display ごとに `compose->rotations/rotate_why`（anchor）・`head->rotations/rotate_why` を open の時に計算し、snapshot の `rotations` の event で Settings へ（§7.1）。shown でない display は `vkGetPhysicalDeviceDisplayPropertiesKHR` の値から `hw180` だけで計算。

### 4.11 180° の scanout（compositor 側、p010）

- surface の作成: `vkdemo_display_open_on()` に `VkSurfaceTransformFlagBitsKHR transform` の引数を足す（既存の呼び手 vkdemo・cursor-plane は IDENTITY）。`kwl_compose_output_prepare` は `compose->scanout_rotation == 180` なら `VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR` を渡す。swapchain は `vkdemo_display_create_swapchain*` が `preTransform = capabilities.currentTransform` を渡す（今は IDENTITY 固定 `display.c:322`）。`imageExtent` は mode のまま（180 は幅と高さを入れ替えない）。
- 描画は 0° と同じ経路（turn 無し、damage・age・部分 frame・scene の写し・shot・mirror の元は全部そのまま）。**swapchain の画像は論理の絵**で、hardware が出す時に回す。`SHOT` の答えは `rotation=180 scanout=180 turn=0`（PNG は論理の絵: 試験の道具は回さない）。
- 変更は reopen（§4.6 の 1）。`compose->hw180` が無い display で 180 を頼まれたら UNSUPPORTED（policy ANY なら turn）。
- head も同じ（`heads_open` の surface に head の transform）。mirror の head の blit の先は swapchain の画像。
- Pi 4 の cursor の plane・WS214 の overlay は §4.5 のとおり使わない（hardware が回すのは primary だけ）。

## 5. scanout の回転（kernel の UAPI・driver・libvulkan、p009・p011）

### 5.1 UAPI（`include/uapi/gpu-display.h`、追加だけ。Q1 がユーザーに報告、D16）

| 追加 | 意味 |
| --- | --- |
| `#define GPU_DISPLAY_ROTATE_180 4096U`（`gpu_display_info.flags`） | この出力の primary の plane は画像を 180° 回して出せる（present と commit の `ROTATE_180` を受ける）。cursor・overlay の plane は回さない |
| `#define GPU_DISPLAY_PRESENT_ROTATE_180 4U`（`gpu_display_present.flags`） | この frame を 180° 回して出す: 画像の (x, y) を出力の (W−1−x, H−1−y) に。`FIFO` と組み合わせる（`FIFO|ROTATE`、`FIFO|BLOB|ROTATE`）。出力に `GPU_DISPLAY_ROTATE_180` が無ければ EOPNOTSUPP（何も変わらない）。mode は変わらない（modeset 無し）。plane の性質なので frame ごとに変えてよい |
| `#define GPU_DISPLAY_PLANE_ROTATE_180 4U`（`gpu_display_plane_state.flags`） | commit の primary（plane 0）だけ。overlay に付けると EINVAL。SHOWN と一緒に。意味は present と同じ |

- 核（`gpu.c:3530-3531`）は present の flags の組に `ROTATE_180` を足す（`FIFO`、`FIFO|BLOB`、それぞれ `|ROTATE_180`）。`gpu_commit_plane_check`（`gpu.c:4057`）は `SHOWN|IMAGE|ROTATE_180` を許し、`plane != 0` で `ROTATE_180` なら EINVAL。driver が能力を出していない時の EOPNOTSUPP は driver が答える（核は `gpu_display_info` を覚えていない）。
- `GPU_ABI_VERSION`・struct の大きさは変えない。古い userland は bit を立てないので挙動不変。90/270 の bit は作らない（どの driver も持たない。将来は `ROTATE_90/270` と extent の入れ替えの約束を一緒に足す、§14）。
- cursor の plane（`GPU_DISPLAY_CURSOR_MOVE` の x/y）は出力の座標のまま（回さない）。利用者は回転した出力で cursor の plane を使わない（§4.5）。

### 5.2 i915（p009）

- 能力: `display_query` の 2 か所（`display.c:2750, 3360`）に `GPU_DISPLAY_ROTATE_180` を足す。条件: display version 9 以上（port の対象: Skylake 以降の universal plane。`drv_i915_lcd_display_ver()`）。5320（Tiger Lake、12）・5330（Alder Lake-P、13）とも参照の `supported_rotations` に 180 がある（§1.3）。
- 経路: `gpu_display_present.flags` の `ROTATE_180`（`present.c:337` 以降で読む）と commit の primary の `PLANE_ROTATE_180` → `head->cfg.fb_rotation`・`ms->fb_rotation`（`MODE_ROTATE_0` か `MODE_ROTATE_180`、`head.c:876-881`・`modeset.c:3085-3097` の fb の欄に 1 つ足す）→ `drv_i915_lcd_ms_plane_prepare(..., rotation)`・`drv_i915_plane_emit(..., rotation)` の引数 → `plane_state->hw.rotation`（`plane.c:293, 378` の `MODE_ROTATE_0` を置き換える）→ 既存の `i915_skl_plane_ctl()` → `PLANE_CTL_ROTATE_180`。PLANE_CTL は flip ごとに計算して書く（`modeset.c:2750`、noarm → arm）ので、frame の間で回転を変えても modeset は無い。`i915_skl_plane_stride_mult` は `rotation_90_or_270` だけ見る（180 は linear の stride のまま）。surface の offset（`PLANE_OFFSET`/`PLANE_SURF`）は 180 で変えない（参照は 180 を `drm_rect_rotate` で src の矩形に写すだけで、全画面の src は同じ矩形になる。実装の時に port の `skl_check_main_surface` 相当を読んで確かめ、違えば段を止めて報告）。
- **Intel の公開資料で確かめる段**（ユーザー「linear のままで 180 が可かを Intel の公開資料で確かめる段」）: p009 の最初に、Intel の公開の PRM（Programmer's Reference Manual、display engine の volume の `PLANE_CTL` の "Plane Rotation" の field）で「180° の回転は linear（tiling 無し）の surface で可、90°/270° は Y tiling が要る」を確かめ、volume・頁・引用の 1 行を p009 の phase.md に書く。tree の MIT の参照（§1.3: linear で断るのは REFLECT_X と 90/270 だけ、`skl_universal_plane.c:1431-1444`）と Linux の 180 の実装の歴史（2014 年の「Add 180 degree primary plane rotation support」の patch、公開の mailing list）はこれと矛盾しない。PRM が違うことを言えば p009 を止めて Q1 に報告する（turn pass の 180 に戻す案を添えて）。
- 受け入れ: 5330 と 5320 の実機で `GPU_DISPLAY_PRESENT_ROTATE_180` の frame が逆さに出る（kernel の log `i915: plane rotate=180` と利用者の目／写真、§12.3）。i915 の host の試験は無い（実機だけ）。passthrough は Guardrail「iGPU の passthrough は i915 の driver の改善の Phase だけ」の範囲（p009 は i915 の driver の Phase）。
- `internal.h:2525-2530` の「rotation 0」の注記を直す。PSR・FBC・DMC との相互作用は未確認（実機で見る）。

### 5.3 libvulkan と vkdemo（p009）

- `wsi-display.c:296-372`: `GPU_DISPLAY_ROTATE_180` → `VULKAN_WSI_OUTPUT_ROTATE_180 64U`（`wsi-internal.h`）。
- `wsi.c:125`: `property->supportedTransforms = IDENTITY | (ROTATE_180 if the flag)`。
- `wsi.c:620-624`: `create_info->transform` が display の `supportedTransforms` に無ければ `VK_ERROR_INITIALIZATION_FAILED`（IDENTITY は常に可）。`surface->display_info.transform` に残る（既に `*create_info` を写している）。cursor・overlay の surface は IDENTITY だけ。
- `wsi-display.c:584-585`: `supportedTransforms` = display の集合、`currentTransform` = surface の transform。
- `wsi-swapchain.c:1213-1215`: `info->preTransform != capabilities.currentTransform` を断る（IDENTITY の surface は今と同じ）。**約束**: 「surface の transform = scanout が画像に当てる回転。application は論理の絵を描き、`preTransform` に surface の transform を渡す（= 追加の transform を頼まない）」。Vulkan の spec の `preTransform`（「presentation の前に content に当てた transform。`currentTransform` と違えば presentation engine が transform する」）のうち「同じなら engine は何もしない」の読みに合わせ、scanout の回転は `VkDisplaySurfaceCreateInfoKHR.transform`（「scanout の一部として画像に当てる transform」）が担う。使い手は tree の中（compositor・vkdemo）だけなので、この約束を `libvulkan/README.md` に書く。
- present（`wsi-display.c:1412, 1850`）と commit の primary（`display_overlay_state()` の plane 0）: surface の transform が ROTATE_180 なら `ROTATE_180` の flag。cursor・overlay の present には付けない。
- 90/270 は受けない（`VK_ERROR_INITIALIZATION_FAILED` のまま。extent の入れ替えの約束と一緒に Future Work）。
- vkdemo: `vkdemo_display_open_on()` に `transform` の引数、`vkdemo_display_create_swapchain*` は `preTransform = capabilities.currentTransform`。vkdemo 本体は IDENTITY。
- libvulkan-compat（Linux・FreeBSD）: 変更無し（IDENTITY だけ）。
- host 試験: libvulkan の wsi の host 試験の形があれば（`userland/desktop/libvulkan/tools` を実装の時に読む）、flag の写し・transform の受け・断り・present の flag を足す。無ければ compositor の `--rotate-policy` の log と実機で。

### 5.4 bcm2711（p011、Pi 4 の 180°）

- 能力: `display-device.c:698` の flags に `GPU_DISPLAY_ROTATE_180`。
- 経路: present（`display-device.c:915, 1124` の flags の検査に `ROTATE_180` を足す）と commit の primary の `PLANE_ROTATE_180` → `struct bcm2711_flip_primary` に `rotate180` → `encode_plane`／`prepare_frame` の位置の word（`words[1]`、今は 0）に **2 つの反転の bit `0x80008000`**（左右と上下の反転の組 = 180°）。値は tree の `list.c:331` が「flipped」と読む値（zedBSD の code にある値を使う。WS217 は Guardrail の GPL の参照の規則の対象外なので、Linux の vc4 は読まない）。
- **未確認の点**（Pi の実機で確かめる、D15）: (a) 上下の反転の時に pointer を最後の行に置く必要があるか（HVS が下から読むか）、(b) 2 bit のどちらが左右・上下か（180 では両方立てるので要らない）、(c) unscaled の plane で反転が効くか。確かめ方: p011 の最初に `exercise_display`（`display-device.c:234-381`）の checker を非対称の模様（左上だけの印）に変えて 180 の flag で出し、利用者の目／写真で向きを見る。(a) が要れば pointer を `physical + (height−1)×pitch` に（これも tree の算数で書ける）。どうしても vc4 を読まないと分からなければ Q1 がユーザーに「WS217 p011 に WS141 の規則を広げるか」を聞く（D15）。
- console の plane・cursor・overlay は反転しない（回転した出力では cursor の plane も overlay も使わない、§4.5。console は不透明な全画面の primary の下で list に入らない）。readout の console の探索（`list.c:263-267`）は flipped の plane を飛ばすので、frame の plane を console と取り違えない。
- `list.c` の decode の `flipped` は `rotate180` の報告（`bcm2711_flip` の report）に写し、host 試験が list の word を見る。
- 依存: WS214 p003a/p003b（`display-flip.c` の作り直し）の main への統合の後（同じ file を 2 つの WS で触らない）。
- 帯域: 反転は読みの順の違いだけで予算（`read_bandwidth`）は変えない（未確認なら Pi の `hvs-load` の実測で）。

### 5.5 Venus・Linux・FreeBSD

- venus の driver は `ROTATE_180` を出さない（変更無し）→ libvulkan は IDENTITY だけ → compositor は回転を無効表示（§4.10）。QEMU の試験は `--rotate-policy=any` で turn の経路だけ（D13）。
- Linux・FreeBSD は §4.8。

## 6. 入力の設計

### 6.1 絶対の device の写像と direct の決め方（M3・M4 of review-1、M6・M7）

- **direct の印**（`kwl_input_device.direct`）: **touch screen（MT protocol B で POINTER でない）と絶対の pointer（QEMU の tablet）は常に direct**（property を見ない。Linux・FreeBSD の property の無い経路でも同じ）。**pen だけ** `INPUT_PROP_DIRECT` で分ける（`kwl_input_probe` で読む）。
- **写像の規則**（`kwl_output_absolute(server, slot, direct, raw_x, min_x, max_x, raw_y, min_y, max_y, &x_fixed, &y_fixed)`、heads.c、24.8、結果は plane の座標）: direct は 生 → slot の出力の物理（`mode_*`）→ `kwl_rotate_fixed` で論理 → plane の位置。非 direct（机の上の板）は 生 → slot の出力の論理 → plane の位置（回さない、ユーザーの決定。縦横比は崩れる: §14）。slot の出力が mirror の head なら §6.2。
- 置き換え: `kwl_cursor_box_place()`（I、受け箱の geometry）、`touch.c:964-969`、`tablet.c:1014-1015`。slot は device の結び付け（§6.4、無ければ anchor）。
- **傾き**: direct の pen は `kwl_rotate_vector` で論理の向きに（`tablet.c:592-598, 713`）。非 direct は回さない。
- **kernel の pen の property**（p003、UAPI 不変）: `hid-input.c` が pen の device を登録する時、`input->pen == HID_REPORT_PEN_DISPLAY`（Pen の collection）なら `INPUT_PROP_DIRECT`、`_TABLET`（Digitizer だけ）なら `INPUT_PROP_POINTER`。補い: 同じ `hid_input` に Touch Screen の collection があれば DIRECT（ユーザーの LCD では pen と touch が別の interface = 別の `hid_input` なので補いは当たらず、主の規則で DIRECT になる）。fixture は BUG-267 の 2 つの rdesc（if1 Pen → DIRECT、if0 Touch Screen の node → DIRECT は既存）と、Digitizer だけの合成の descriptor（→ POINTER）。
- **注入の pen**: `INPUT_INJECT_KIND_PEN` は DIRECT を付ける。机の上の板は `INPUT_INJECT_KIND_PEN_POINTER`（D12、Q1 が可）で POINTER。peninject に `--pointer`。**名前を分ける**（L19: `inject-pen` と `inject-pen-pointer`）。

### 6.2 mirror の中の結び付け（S8、推奨を採用）

mirror の head に結んだ direct の device: 生 → head の物理 → head の回転の逆 → head の論理 → **fit の矩形の逆写像**（`kwl_displays_fit` の矩形の外（黒い帯）は端に clamp）→ anchor の論理 → plane。`kwl_output_absolute` に mirror の分岐（head の論理と fit の矩形を geometry に持つ）。非 direct は head の論理 → fit の逆写像。

### 6.3 head に結んだ touch（S7）

- contact は始まった時の **結び付けの slot と、その出力の論理の矩形**を持つ（`struct touch_contact` に `slot`・`rect`）。shell へ渡す点の clamp（`touch.c:2082-2085`）・drag の clamp はその矩形。
- **edge の group・corner の gesture・keyboard の zone は anchor だけ**（slot が anchor でない contact はこれらの判定に入らない。head の縁の swipe は出ない）。理由: shell の panel・App Home・OSK は anchor の上にあり、head の上で出す物が無い。将来 head の上の shell が要れば矩形を引数にする形に広げられる（判定の関数は `server->width/height` の代わりに矩形を取るように直しておく）。
- `corner.c:632-635`・`touch.c:1019-1021, 1608, 1613, 1758-1761, 1848` を直す。p006 の file に `touch.c`・`corner.c`・`edge.c`。

### 6.4 pen・touch と display の結び付け（D3）

- **対象**: 絶対の device の全部（touch screen・pen・絶対の pointer）。
- **identity**（`kwl_input_identity()`、純粋、host 試験）: `BUS:VVVV:PPPP:UNIQ`（bus は `usb`・`bluetooth`・`i2c`・`virtual`・`pci`、他は `bus%u`。UNIQ は `EVIOCGUNIQ`、無ければ空）。vendor と product が共に 0 なら `BUS:0000:0000:NAME`（注入は `virtual:0000:0000:inject-pen` など）。**NAME も UNIQ も印字できる ASCII の空白以外に限り、他は `_`**（M5）。長さ `KWL_INPUT_IDENTITY_MAX 96`（切った時の衝突は制限）。LCD の pen と touch は同じ identity（§1.7）なので 1 つの結び付けが両方に効く（注入は名前が違うので別: host 試験で確かめる、L19）。同じ機種を 2 台 serial 無しは区別できない（制限）。
- **backend**: `int kl_backend_input_uniq(int descriptor, char *uniq, size_t size)`（`EVIOCGUNIQ`。`input-zedbsd.c` と `input-evdev.c` の 2 file、`keiland-backend-evdev.h` の宣言）。`struct kwl_input_device` に `identity[]`・`direct`・`bound`。
- **保存**: `displays.conf` の `touch=IDENTITY KEY`（§7.4）。
- **引き直し** `kwl_input_bindings_resync(server)`: attach、出力の変化、config の変更の後に identity → key → shown な display の slot。key の display が無い・shown でない時は anchor。mirror の head は §6.2。display の抜き差しで `touch=` は消さない。
- **既定**: anchor（今の挙動）。
- **pointer の warp**: `kwl_pointer_warp_to(server, slot, x, y)`。

### 6.5 「この画面に触って」（listen）（M9・L18・L25）

- `listen_touch(request, key)` → `displays_listen {object, request, key, until_ms}`（30 秒）。立っている間に最初に来た **touch の contact の開始**か **pen の down（`BTN_TOUCH`）**（proximity_in では結ばない: 机の上の板の pen が近くにあるだけで結ばれない。dialog の文「Touch that display」と合う）の device をその key に結ぶ（config、保存、resync、tell、result OK）。その touch・pen は飲み込む（contact は `ROUTE_IGNORED` で up まで、pen は up まで）。mouse は受けない。30 秒で UNAVAILABLE、`listen_touch(request, "")` は取り消し、2 つ目は BUSY、client が切れたら解除、active な session でなければ DENIED、key が shown でなければ INVALID。
- 信頼の形は `set_shown` と同じ（active な session のどの client も呼べる）。listen を続けて呼ぶ client は次の touch を飲み続けられる（記録。利用者の session の中の client なので、今の形の範囲）。
- log `KWL INPUT bound identity=… key=… slot=N`。

### 6.6 受け箱の geometry（M2 of review-1、L16）

```
struct kwl_cursor_geometry {
	struct kwl_plane_rect outputs[KWL_PLANE_SLOTS];   /* 論理 */
	unsigned output_count;
	struct { uint32_t width, height; unsigned rotation; } physical[KWL_PLANE_SLOTS];
	struct kwl_plane_rect mirror_fit[KWL_PLANE_SLOTS];  /* mirror の head の fit の矩形（§6.2）、他は空 */
	unsigned absolute_slot;    /* 絶対の pointer（QEMU の tablet、常に direct）の結び付け */
};
```

- `kwl_cursor_box_warp(box, geometry, move, context, motion)` と `kwl_cursor_box_outputs(box, geometry)` の両方がこれを丸ごと 1 区間で書く。M は `kwl_output_geometry(server, &geometry)` で作る。
- 絶対の pointer が 2 つある時は 1 つの `absolute_slot`（制限、§14）。
- I は park しない、warp の番号で古い motion は捨てられる。

## 7. protocol・libkeiland・Settings・保存

### 7.1 `kl_system_displays_v1` の拡張（manager の version `KL_SYSTEM_SINCE_ROTATION` = 34、M4 の (a)）

| 種類 | opcode | 名前と引数 | 意味 |
| --- | --- | --- | --- |
| request | 4 | `set_rotation(uint request, string key, uint rotation)` | 度数（他は INVALID）。すぐ適用。result: OK、DENIED（active でない）、UNAVAILABLE（key が繋がっていない。`set_shown` と同じ、L11）、INVALID、UNSUPPORTED（mask に無い角度、turn の資源の失敗。何も変わらない）。off・limited の display は保存だけ（applied 1）。180 の変更は reopen なので result は open の結果（失敗は FAILED、元の向きに戻す） |
| request | 5 | `bind_touch(uint request, string identity, string key)` | p006 まで UNSUPPORTED（M4）。その後: OK、DENIED、INVALID（identity が無い・key が繋がっていない）、`saved` |
| request | 6 | `listen_touch(uint request, string key)` | p006 まで UNSUPPORTED。その後 §6.5 |
| event | 3 | `rotation(string key, uint rotation)` | snapshot で `output` の後、display ごと（shown でない display も保存した回転を当てた値。`output` の width/height も論理） |
| event | 4 | **`rotations(string key, uint mask, uint why)`**（新） | display ごとの選べる角度の mask（`KL_DISPLAY_ROTATION_0 1`・`_90 2`・`_180 4`・`_270 8`）と無効の理由（`KL_DISPLAY_ROTATE_WHY_SCANOUT 1`・`_TURN 2`、§4.10）。`rotation` の後 |
| event | 5 | `touch(string identity, string label, uint kind, string key)` | p006 から送る（それまで送らない）。kind は `KL_TOUCH_DIRECT 0x1`・`_PEN 0x2`・`_FINGERS 0x4`・`_POINTER 0x8` |

- event 3〜5 は `object->version >= KL_SYSTEM_SINCE_ROTATION` にだけ。`kl-system-protocol.h`: `KL_SYSTEM_SINCE_ROTATION 34U`・`KL_SYSTEM_MANAGER_VERSION 34U`（実装の時に main の最大を確かめ直す）。`system-protocol.c`: interface の version を `KL_SYSTEM_SINCE_ROTATION`、requests 7（`"set_rotation" "usu"`、`"bind_touch" "uss"`、`"listen_touch" "us"`）、events 6（`"rotation" "su"`、`"rotations" "suu"`、`"touch" "ssus"`）。**wire は p004 で全部決めて入れる**（p006 は中身だけ）。判定は manager の版。device の抜き差し・結び付けの変化も `KL_SYSTEM_CHANGED_DISPLAYS`。

### 7.2 libkeiland の公開 API（`KL_VERSION` の次の番号は Q1、p004 で全部）

- `struct kl_display` は変えない。`KL_DISPLAY_ROTATE_0/90/180/270`、`KL_DISPLAY_ROTATION_*`（mask）、`KL_DISPLAY_ROTATE_WHY_*`。
- `int kl_system_displays_rotation(const struct kl_system *, const char *key, unsigned *rotation);`（ENODEV、ENOTSUP: 34 未満）。
- **`int kl_system_displays_rotations(const struct kl_system *, const char *key, unsigned *mask, unsigned *why);`**（新）。
- `int kl_system_displays_set_rotation(struct kl_system *, const char *key, unsigned rotation, uint32_t *request);`（result は 0・EPERM・ENODEV・EINVAL・ENOTSUP）。
- `struct kl_touch_device { identity[96]; label[]; kind; display[]; }`、`KL_TOUCH_DEVICES_MAX 8`、`kl_system_touches_get/bind/listen`（p004 で宣言と marshal、compositor が UNSUPPORTED を答える間は ENOTSUP が返る）。
- `kl_window_output_mode()` は変えない。

### 7.3 Settings の Display の頁（`page-display.c`）

- **回転**（D1・D4）: row に 4 つの絵の button（横置き／左辺を下に 90／逆さ 180／右辺を下に 270、絵の下に度数）。選択中を強調。**mask に無い絵は無効（薄く、押せない）**、row の下に理由の 1 行: `why & SCANOUT` → 「This display cannot be rotated」（4 つとも無効、0° だけ強調）、`why & TURN` → 「90° and 270° are not available on this device yet」。押すとすぐ `kl_system_displays_set_rotation`。request 中は無効。mirror でも row に出す。manager が 34 未満なら出さない。
- 配置の図は論理の `width/height` で縦長に。card に上辺の印。
- **Touch and pen** の card と「Touch to assign」は第 2 版のとおり（§6.5。p006 まで `kl_system_touches_get` が 0 件を返すので card は出ない）。
- 結果の文字: UNSUPPORTED は「This display cannot be rotated that way」、FAILED（reopen の失敗）は「The display could not be reopened」。
- Linux・FreeBSD の build でも同じ UI（KMS が 180 を出さないので回転は無効表示、§4.8。pen の direct は property を読んだ後は同じ、M6）。

### 7.4 保存（displays.conf）

- `rotate=KEY N`（度数、0 は書かない）、`touch=IDENTITY KEY`（既定は書かない）。`version=1` のまま。古い build の往復で消える（受け入れる）。行の順は `version`・`mode`・`anchor`・`brightness`・`place`・`off`・`rotate`・`touch`。`HEADS_TEXT_MAX` を 8192 に。
- `struct kwl_display_config` に `rotations[KWL_DISPLAYS_PLACES] {key, rotation}`・`touches[KWL_DISPLAYS_TOUCHES 8] {identity, key}`。parse/format と `kwl_displays_rotation/set_rotation/touch/set_touch`。不正な行は捨てて log。
- 保存した角度がその display の mask に無い時（例: Pi で 90 を保存した後に p015 の前の build）: 0 で開き、`rotate=` は保持、log `KWL OUTPUT rotate unsupported key=… rotation=…`、Settings は保存の値でなく実際の 0 を出す。
- greeter は別の process なので login 画面は回らない（D9）。

## 8. 速さと費用

- 0° と **180°（scanout）**: compositor の frame の費用は変わらない（pass も画像も増えない）。180° への変更は reopen（swapchain の作り直し、数十〜数百 ms、`KWL MODE` と同じ計測）。
- 90°・270°（turn、i915・policy ANY の venus）: 1 frame に turn pass 1 つ。1920x1080 の全画面で 8.3+8.3 MB、60 fps で約 1 GB/s。普通の frame は damage の分だけ。
- Pi 4: 180° は hardware（追加の費用 0）。90/270 は p015 の後（turn だけ、0.3 GB/s）。p015 の前は policy ANY の計測だけ（CPU/TFU の変換あり）。
- extended の回転した head の turn は毎回全画面。
- memory: turn の入れ替えの間は 2 倍（1080p で turn 8.3 MB、scene 8.3 MB、壁紙と blur 16.6 MB、layers）。
- 計測: `KWL PERF pass` に turn の時間。

## 9. 失敗と回復、並行性

| 場面 | 扱い |
| --- | --- |
| turn の画像・pipeline・framebuffer・scene・layers・壁紙の memory が取れない、format の能力が無い | 新しい物だけ捨てて UNSUPPORTED。何も変わらない |
| 180° の reopen で open が失敗した | config を元の向きに戻してもう一度 open（output switch の戻しと同じ）。両方失敗なら `output_lost`（既存の hotplug 待ち）。result FAILED |
| driver が `ROTATE_180` を EOPNOTSUPP した（能力の印と食い違う） | libvulkan は present の失敗を返す → compositor の present の失敗の既存の経路（`KWL VULKAN_ERROR`、出力の作り直し）。起こらないはず（印と受けは同じ driver） |
| park の時間切れ | 既存（`_exit`） |
| displays.conf の不正な行、mask に無い保存の角度 | 行を捨てる／0 で開いて保持（§7.4） |
| game mode の中の `set_rotation` | config を書き `kwl_scanout_leave(ROTATED)`、次の open で適用 |
| 出力が閉じている・lost | config だけ |
| 回転中に hotplug・output switch | turn の変更は park の中で原子的。scanout の変更は reopen の中。切り替え後の open で新しい key の `rotate=` を適用 |
| present が落ちた frame | `turn.whole = 1` |
| 回転の瞬間に指・pen・grab | cancel、up と proximity_out、grab の終了 |
| listen の最中に display が抜けた・lock | 解除して UNAVAILABLE |
| 同じ identity の device が 2 つ | 同じ display（制限） |
| fullscreen の client が古い大きさの buffer | configure で論理を伝え、届くまで clip（既存） |
| Pi で反転の pointer の規則が違った（§5.4 の (a)） | p011 の実機の確認で分かる。絵が 1 行ずれる／壊れる → pointer の補正を入れる |
| thread | rotation・論理・turn・scanout は M が park か reopen の中で変える。受け箱の geometry は 1 区間で書き I が読む。C は plane を使わない。R は record を再生するだけ。lock の追加は無し |

## 10. 決めてほしい点（残る問い、推奨つき。D1〜D12 は ws.md で決定済み）

| ID | 問い | 推奨 |
| --- | --- | --- |
| D13 | QEMU（Venus）では回転が無効表示になるので、compositor の turn pass（90/270）と Settings の UI を T1 が QEMU で試せない。試験のためだけの起動の option `--rotate-policy=any`（能力の mask を 4 つ全部にし、hw180 の無い 180 は turn で）を test image の compositor に置くか | **置く**（`KWL OUTPUT rotate policy=any` の log、製品の image では使わない。無ければ turn の試験は 5330 の実機だけになる） |
| D14 | 能力の gate「hardware が 180 を回せない display は回転を一切出さない」でよいか。帰結: Linux・FreeBSD の build（KMS は IDENTITY）と p009 の前の i915 では回転が無効表示。代案は libvulkan に「virtual の display」の印を足す（標準の Vulkan の外の口） | **gate でよい**。Linux・FreeBSD の KMS の回転（DRM の rotation の property）は Future Work |
| D15 | Pi 4 の HVS の反転で pointer を最後の行に置く必要があるか等（§5.4）を、tree の値と実機で確かめる。それで分からなければ WS141 の GPL の参照の規則（値を事実として使う）を WS217 p011 に広げるか | **まず tree の値と実機で**。広げるのは分からなかった時だけ（Q1 がユーザーに聞く） |
| D16 | UAPI の追加の形（§5.1: info の `GPU_DISPLAY_ROTATE_180 4096`、present の `PRESENT_ROTATE_180 4`、plane_state の `PLANE_ROTATE_180 4`。90/270 の bit は作らない） | **この形**（ユーザーの決定「UAPI に plane の回転を足す」の具体化。Q1 がユーザーに報告） |
| D17 | 180° の scanout の出力で game mode の直の scanout を保つか（backend の claim に transform の口を足し、present に `ROTATE_180`） | **今はしない**（D6 のまま合成に落とす）。Future Work |

## 11. 敵対的レビュー（自分で、第 3 版）

| 懸念 | 評価と対処 |
| --- | --- |
| `preTransform` の意味の読み違え（Vulkan の spec では「engine が transform する」のは `preTransform != currentTransform` の時） | この設計は surface の `transform`（scanout の回転）に回転を担わせ、swapchain は `preTransform == currentTransform` で「追加の transform 無し」。使い手は tree の中だけで、約束を README に書く（§5.3）。Android 等の外の app の慣習とは違うが、display の surface を作るのは compositor と vkdemo だけ |
| i915 で 180 の時に PLANE_OFFSET/SURF の補正が要るのでは | 参照の `drm_rect_rotate` は全画面の src を同じ矩形に写す。実装の時に port の check の関数を読み、実機で確かめる（§5.2）。違えば段を止める |
| 180 の変更が reopen なので画面が一瞬消える | 稀な操作。output switch の reopen と同じ経路で、切り替えの実績がある（BUG-266 の証明の仕組みは reopen では働かないが、同じ display なので要らない） |
| hw180 の無い display（Venus）で保存した `rotate=180` を開く | 0 で開いて保持、log（§7.4）。Settings は 0 を出す |
| policy ANY の turn の 180 と scanout の 180 で入力の写像が違わないか | 写像は `rotation` だけで決まる（§2 の不変条件）。turn か scanout かは描画と shot だけの違い。host 試験は両方の plan で同じ入力の写像を確かめる |
| shot が 180 の scanout で論理の絵を返すので T1 が向きを確かめられない | QEMU では 180 の scanout が無い（Venus）。実機は kernel の log と目／写真（§12.3）。turn の 180（policy ANY）は shot が物理の絵 |
| `compose_target_pass` が `pass_load` を作る時に backdrop との二重作成 | backdrop の作成の code をこの helper に移し、backdrop はこれを呼ぶ（§4.2 の 3）。`kwl_backdrop_destroy` は `pass_load` を壊さない（次の output で作り直す今の挙動は「open の時に helper が作る」に変わる） |
| head の回転で窓の refit の既存の経路が無い（review-2 の未確認） | p005 の最初に `heads_windows_follow`・`kwl_glass_output_resized` を読み、head 版を足す（§4.6 の 7）。無ければ作る |
| touch を head に結んだ時に edge・corner・OSK が出ない | 決めた（§6.3）。利用者の使い方（LCD に pen と指で書く）で要るのは窓の操作 |
| `rotations` の event が display ごとで、hw180 を shown でない display にも計算する | `vkGetPhysicalDeviceDisplayPropertiesKHR` の `supportedTransforms` は surface 無しで読める（§4.10） |
| `--rotate-policy=any` が製品の image に残る | option の無い build は DEFAULT 固定。test image の config にだけ渡す。log に出る |
| UAPI の `ROTATE_180` の bit を古い driver（能力の印の無い）に送る client | 核は通し、driver が EOPNOTSUPP。libvulkan は印の無い display に transform を受けないので起こらない |
| Pi の console の plane が反転されない | 不透明な全画面の primary の下では list に無い（§5.4）。半透明の時は compositor は使わない |
| i915 の `MODE_ROTATE_*`・`PLANE_CTL_ROTATE_*` の定数の出典 | tree の `intel/plane.h`（WS031 が MIT の i915 の参照から textually に抽出、出典と sha256 を header に記録）。新しい code は Zlib、定数は既存の header を使う。§15 |
| HAL・UAPI | HAL は不変。UAPI は §5.1 の 3 つの定数と `INPUT_INJECT_KIND_PEN_POINTER`（D12）。driver の変更は i915（plane の回転）、bcm2711（反転）、hid-input（property） |

## 12. 試験の計画

### 12.1 host（実装の担当、`plan/ws217/tests/`）

- `host-rotate.{c,sh}`: `rotate.c` の点・矩形・固定小数・vector・大きさの総当たり、往復、境界。**`kwl_rotate_plan` と mask・why の表**（hw180 × turn_build × policy × 4 つの角度）。
- `host-displays-conf.{c,sh}`: `plan/ws113/tests/host-displays.c` を複写して拡張: `rotate=`・`touch=`、不正な行、順、4096 byte の切れ。
- `host-absolute.{c,sh}`: `kwl_output_absolute()` の 4 つの向き × direct/非 direct × head × mirror の fit の逆写像 × 境界、傾き。`plan/ws213/tests/cursor-due-host-test.c` の fake を複写して geometry の丸ごとの書き。
- `host-scanout-rules.{c,sh}`: `kwl_scanout_decide` の `rotated` × 4 つの向き。
- `host-identity.{c,sh}`: `kwl_input_identity()`（空白・制御文字・長い serial・注入の名前）、引き直し。pen と touch の同じ identity（合成の値）。
- `host-hid-pen-props.{c,sh}`: hid-input の property の規則、fixture は BUG-267 の 2 つの rdesc と合成の Digitizer。
- `host-kl-displays.{c,sh}`: `plan/tools/settings/host-kl-settings.c` の形で `set_rotation`・`bind_touch`・`listen_touch` の marshal、34 未満の ENOTSUP、`rotation`・`rotations`・`touch` の受け。
- `host-gpu-display-flags`: 核の present と plane_state の flags の検査（kernel の host 試験の形があれば。`src/drivers/gpu/` の既存の host 試験を実装の時に読む）。bcm2711 の `display-flip-host-test` に 180 の list の word（p011）。
- `host-touch-contact.{c,sh}`: contact の矩形の clamp と「anchor だけ」の判定（S7）。
- build: amd64（`$(BUILD)/dynamic/zdesktop`・`$(BUILD)/bin/settings`・libvulkan・vmunix の named target）と rpi4 の compositor・vmunix、Linux・FreeBSD の Settings と backend、warning 0。`plan/tools/keiland-os-boundary/check.sh`。

### 12.2 T1（QEMU、virtio-gpu/Venus、1 つの image、`--rotate-policy=any`）

- image: `plan/ws217/tests/config-amd64.mk`（`plan/ws113/tests/config-amd64-p006.mk` を**複写**、`CONFIG_INPUT_TEST_INJECT=y`、`ZEDBSD_TEST_SCREEN_CAPTURE=y`、Notes・Settings・wltest）。compositor の起動に `--rotate-policy=any`（D13）。2 台目の出力は WS113 p007 の image の形。撮影は keiland-shot。
- scenario は `tests/scenarios/desktop/displays/rotate.md`・`rotate-input.md`・`touch-bind.md`、`tests/scenarios/apps/notes/portrait.md`:
  1. **p002**: `--rotate=90`（1280x800）→ `KWL OUTPUT rotate … rotation=90 scanout=0 turn=90 width=800 height=1280 mode=1280x800`、shot は 1280x800 で system bar が物理の右端に縦。`--file $HOME/.config/keiland/displays.conf=plan/ws217/tests/displays-rotate.conf` でも同じ（最初の frame から縦）。**`KWL GLASS` の壁紙の大きさの行が論理（800x1280）、回転したまま壁紙を変えて壊れない**（C4）。180（policy ANY なので turn）・270・0。
  2. **部分 frame**: 窓を少し動かして `KWL DAMAGE … turn=` の物理の矩形が式と合う。`SHOT partial` の絵に縦線が無く、**damage の外に背景色の帯が無い**（C3）。
  3. **p003**: 注入の touch（物理）で App Home の tile → `KWL TOUCH` の論理が写像と合う。注入の pen（DIRECT）の論理の座標と傾き。`PEN_POINTER` は論理に直に。
  4. **p004・p010**: policy ANY: Settings の 90 の絵を押す → `KWL OUTPUT rotate`、`ZSETTINGS DISPLAY rotation`、shot。180・270・0。login し直しで `rotate=` が残る。**policy 無し（既定）**: Settings の 4 つの絵が無効で「This display cannot be rotated」、`ZSETTINGS DISPLAY rotations mask=1 why=1`、`set_rotation(90)` が UNSUPPORTED。
  5. **p005**: 2 台目の出力で head の 90、配置の再検査、**head の上の最大化の窓が縦の大きさ**、**anchor の回転で head が動いた時に head の窓が付いてくる**、mirror の正立（黒い帯）。
  6. **p006**: 「Touch to assign」→ listen → 注入の touch → `KWL INPUT bound`。2 台目に結んだ touch の `KWL TOUCH` が head の矩形に入り、**head の上の drag で窓が動く**、**head の縁の swipe は出ない**（`KWL EDGE` 無し）、mirror の head に結んだ touch が anchor の対応する点に来る。pen の proximity_in では結ばれない。抜き差しで戻る。
  7. **回帰と周辺**: 0° の既存の smoke suite。game mode の app が `direct=0 reason=rotated`。回転したまま output switch と hotplug。lock → unlock。logout → login 画面は回らない、login で回る。OSK・Wiseview・窓の refit。
  8. **D5**: 1024x600 に `--rotate=90` で Notes の toolbar（shot）。

### 12.3 実機（5330・5320、ユーザー／T1 の SSH、i915 の passthrough は p009 の範囲で可）

- **p009**: vkdemo（`--transform=180` の option を足す）か compositor の `--rotate=180` で、kernel の log `i915: plane rotate=180` と画面が逆さ（ユーザーの目／写真）。5330（display version 13）と 5320（12）。PSR のある内蔵 panel と HDMI の外付けの両方。
- **p010**: Settings の 180 の絵 → 逆さ、mouse・touchpad の向き、touch screen（内蔵）の座標。90（turn）→ 縦長の desktop、Notes。
- ペン LCD を 2 台目として縦置き（左辺を下に）、「Touch to assign」で LCD の pen を結ぶ → Notes に書く。
- 実機の証拠は QEMU と分けて記録。

### 12.4 Pi 4（T1・Q1 経由、p011。90/270 は WS141 p015 の後）

- p011: `exercise_display` の非対称の模様で 180 の向き（§5.4）、desktop の 180（Settings の絵）、host の list の word。Settings の 90/270 が「not available on this device yet」で無効（`why=2`）。
- 計測（policy ANY、記録だけ）: 1024x600 で 90 の turn の `KWL PERF pass`、App Home の開閉と drag の frame の時間、underrun。「CPU/TFU の変換あり」と明記。

## 13. 実装の段（Phase）と受け入れ

既存の ID（p002〜p008）は保ち、新しい段を p009〜p011 にした（ID の付け直しはしない）。実行の順は依存の graph のとおり。

| Phase | 内容 | 受け入れ | 依存 |
| --- | --- | --- | --- |
| p002 | compositor の中核と turn（anchor）: `rotate.c`（写像・plan・policy）、`mode_*`・`size_*`・`kwl_output_size_set()`、config の `rotate=`（parse/format、`compose_display` と open の全経路で当てる、論理の前後の比較）、turn（`turn_new`・2 つの pass・pipeline・`compose_target_*` の helper・`turn.frag`・CLEAR/LOAD の選び方）、damage（`turn.whole`、`KWL DAMAGE turn=`）、scene/backdrop/layers/glass の大きさと `output_resized(server, glass)`、`kwl_output_rotate`（turn の経路: 先に作る・park・cancel・同じ物理の点・backdrop の retire）、wl_output の論理（案 E）、`kwl_output_view` の拡張、scanout の `rotated` fact と `KWL_SCANOUT_ROTATED`、cursor の plane の停止と L24、`--rotate=N`・`--rotate-policy=any`、`KWL OUTPUT rotate`、`SHOT`/`DISPLAYS` の `rotation=`、`SHOT partial`。180 は policy ANY の turn だけ（scanout は p010） | host 12.1 の rotate・displays-conf・scanout-rules PASS、amd64・rpi4 の build warning 0、grep の表（`server->width =` 等が `kwl_output_size_set` だけ、`compose->pass\b`・`pass_load` が helper だけ）、T1 12.2 の 1・2・7（test-wait） | WS213 p004 の scoped な出力（main 済み） |
| **p009** | **scanout の 180°（kernel・libvulkan、compositor は触らない）**: UAPI の 3 つの定数（§5.1）、核の flags の検査、i915（能力の印、present/commit → `fb_rotation` → `hw.rotation`、注記の更新、**Intel の PRM の確認を最初に**）、libvulkan（flag の写し、`supportedTransforms`、surface の transform の受け、capabilities の `currentTransform`、swapchain の `preTransform == currentTransform`、present/commit の flag、README の約束）、vkdemo（`open_on` の transform、`preTransform`、`--transform=180`）、libvulkan-compat は不変 | PRM の引用が phase.md にある、kernel と libvulkan の build warning 0、host 12.1 の gpu-display-flags、**5330・5320 の実機で逆さ**（12.3、QEMU では確かめられない: Venus は印を出さない）。UAPI の定数は Q1 がユーザーに報告（D16） | p001。p002 と**並行可**（file が重ならない） |
| **p010** | **compositor の scanout の 180° と能力の policy**: `scanout_rotation`、`hw180` の読み（display の property と surface の capabilities）、`vkdemo_display_open_on` の transform を `kwl_compose_output_prepare` と `heads_open` に、`kwl_rotate_plan` で reopen の経路（`kwl_output_rotate` の 1）、mask と why の計算、保存の角度が mask に無い時の扱い、`SHOT` の `scanout=`、policy ANY の turn の 180 と scanout の 180 の切り替え | host の plan の表、build、**5330 で Settings 無しに `--rotate=180` → 逆さ（scanout=180）、`--rotate=90` → 縦（turn）**（12.3）、QEMU で `rotations mask=1 why=1` の log（policy 無し）と policy ANY の 4 つ（12.2 の 4 の log の部分） | p002、p009 |
| p003 | 入力（anchor）: `struct kwl_cursor_geometry`、`kwl_output_absolute()`、touch・tablet・受け箱の置き換え、`direct` の分類（touch screen と絶対の pointer は常に、pen は property）、傾き、kernel の hid-input の property の規則（BUG-267 の fixture）、注入の pen の DIRECT と `PEN_POINTER`（名前を分ける）、Linux・FreeBSD の backend の `EVIOCGPROP`（`input-evdev.c`） | host 12.1 の absolute・hid-pen-props、kernel の host 試験、T1 12.2 の 3 | p002 |
| p004 | protocol・libkeiland・Settings の回転: version 34 の **wire の全部**（requests 7・events 6）、`KL_VERSION` の次の番号の **API の全部**（rotation・rotations・set_rotation・touches_*）、compositor は `set_rotation`・`rotation`・`rotations` を実装し `bind_touch`・`listen_touch` に UNSUPPORTED・`touch` は送らない、`page-display.c` の 4 つの絵・無効表示と理由の文・card の印、shown でない display の論理の snapshot | host 12.1 の kl-displays、Settings の named build（amd64・Linux・FreeBSD）、T1 12.2 の 4 | p010、p003 |
| p005 | head の回転: `head->mode_*`・`kwl_head_size_set`、head の turn と head の scanout の 180、extended と mirror（元を scene の画像に、論理の fit）、配置の再検査と `heads_windows_follow`、head の cancel・refit（S9）、head の `rotate=` と snapshot | T1 12.2 の 5、host の配置の再検査 | p010、p004 |
| **p011** | **Pi 4**: bcm2711 の能力の印と反転（§5.4、`exercise_display` の非対称の模様）、`display-flip-host-test` の word、rpi4 の build の `KWL_TURN_DISABLED`（Settings の why=2）。90/270 は WS141 p015 の後に印を外す（p015 の受け入れに 1 項目、Q1 が連絡） | host の word、rpi4 の build、**Pi の実機で 180 の向き**（ユーザーの目／写真、T1 経由）、Settings の無効表示 | p009、p010、**WS214 p003a/p003b の main への統合** |
| p006 | pen・touch と display の結び付け: `kl_backend_input_uniq`（2 file）、identity（M5 の整え）、`touch=`、`bound` と resync、mirror の fit の逆写像（S8）、contact の矩形と anchor だけの gesture（S7、`touch.c`・`corner.c`・`edge.c`）、`kwl_pointer_warp_to(slot)`、listen（pen は down だけ）、compositor の `bind_touch`・`listen_touch`・`touch` の中身、libkeiland の `kl_system_touches_*` の答え、Settings の card・menu・「Touch to assign」 | host 12.1 の identity・touch-contact・absolute（mirror）・kl-displays、T1 12.2 の 6 | p003、p005。Q1 の判断で p006a（kernel/backend/compositor/config/log）と p006b（protocol の中身/Settings）に割ってよい |
| p007 | 確かめ: T1 の scenario の全部（12.2 の 7・8）、5330・5320（ユーザー）、D5、Pi 4 の 180（p011）と 90/270 の記録（p015 待ち） | 12.2〜12.4 の証拠、ws.md の完了の条件 1〜6 の判定（Pi 4 の 90/270 は p015 待ちの記録でよい） | p002〜p006、p009〜p011 |
| p008 | 規約の全文の見直し（compositor・libvulkan・kernel の変えた file、`style-check.py`、boundary の checker）。p007 の直しの後 | 違反 0、build warning 0 | p007 |

- **依存の graph**: p001 → {p002, p009}（並行）→ p010 → p003 → p004 → p005 → {p011, p006} → p007 → p008。外部: WS213 p004（p002 の前提、scoped）、WS214 p003a/b の統合（p011 の前提）、WS141 p015（Pi の 90/270、WS217 の完了を塞がない: p007 は記録でよい）。
- **p002 の分割**（Q1 の判断）: p002a（`rotate.c`・`mode_*`・`size_*`・`kwl_output_size_set`・config の読みと open の経路・wl_output・`--rotate`・scanout の fact・cursor の plane。turn 無し。**`rotate=`・`--rotate` の 0 以外は UNSUPPORTED**、L20。単独では T1 に出さない）と p002b（turn・damage・scene/backdrop/layers・`SHOT partial`・policy）。
- **T1 の束**（L23、2026-10-03 の方針）: 束 A = p002+p003（QEMU、12.2 の 1〜3・7）、束 B = p009+p010（**5330・5320 の実機**、12.3。QEMU は 12.2 の 4 の log だけ）、束 C = p004+p005（QEMU、12.2 の 4〜5）、束 D = p011（Pi）、束 E = p006（QEMU、12.2 の 6）、束 F = p007（全部と実機）。担当は依頼を Q1 に送って次の Queue へ。
- **変更する file の見込み**: compositor `wayland/rotate.{c,h}`（新）、`compose.{c,h}`、`backdrop.c`、`scene.c`、`layer-cache.c`、`glass.c`、`heads.c`、`displays.{c,h}`、`displays-shell.c`、`output-switch.c`、`display.c`、`protocol.c`、`scanout.c`・`scanout-rules.{c,h}`、`cursor-box.{c,h}`、`cursor-plane.c`、`touch.c`、`corner.c`、`edge.c`、`tablet.c`、`input.c`、`input-thread.c`、`kwl.h`、`main.c`、`shot.c`、`shaders/turn.frag`・`shaders.h`、`Makefile` 3 つ（rpi4 の `KWL_TURN_DISABLED`）; `userland/tests/vkdemo/display.{c,h}`・`main.c`; libvulkan `wsi.c`・`wsi-display.c`・`wsi-swapchain.c`・`wsi-internal.h`・`README.md`; kernel `include/uapi/gpu-display.h`、`src/drivers/gpu/gpu.c`、`i915/display/{display.c,present.c,head.c,modeset.c,plane.c,internal.h,modeset-internal.h}`、`bcm2711/{display-device.c,display-flip.c,display-flip.h,list.c}`、`generic/hid-input.c`・`input-inject.c`、`include/uapi/input-inject.h`（D12）、`userland/tests/peninject/main.c`; `libkeiland-backend/keiland-backend-evdev.h`、`libkeiland-backend-zedbsd/input-zedbsd.c`、`libkeiland-backend/evdev/input-evdev.c`; `libkeiland/system/{kl-system-protocol.h,system-protocol.c,system.c,system-view.c}`、`userland/desktop/include/keiland/keiland.h`; `settings/page-display.c`・`settings.h`。**HAL は変えない**。`src/drivers/gpu/i915/intel/plane.h` の定数は既存。

## 14. 選ばなかった事と制限と Future Work（Q1 が future-work.md へ）

- hardware の plane の 90/270（案 C、D2）: i915 の Y-tiled の scanout、UAPI の `ROTATE_90/270` と extent の入れ替えの約束、libvulkan の 90/270 の transform。契機: turn pass の費用が問題になった時。
- `set_buffer_transform` の 0 以外（案 D）と wl_output の本当の transform。契機: pre-rotate する client。
- cursor の plane・WS214 の overlay の plane の回転した出力での使用（Pi 4）。契機: Pi 4 で cursor の遅れが問題になった時。
- 180° の scanout の出力での game mode の直の scanout（D17）。契機: 回転した画面で game mode の性能が要る時。
- Linux・FreeBSD の KMS の回転（DRM の plane の rotation の property、atomic）。制限: 今は Linux・FreeBSD で回転が無効表示（D14）。
- greeter（login 画面）の向き（D9）。
- 結び付けの自動の推測と、Settings の「画面の上で使う／机の上で使う」の切り替え。契機: BUG-267 の機材と違う descriptor の device が来た時（M7）。
- **机の上の pen tablet を縦長の論理に全面で写すと縦横比が崩れる**（M10、制限）。縦横比を保つ写し（板の一部）は Future Work。契機: 机の上の板の利用者。
- 絶対の pointer が 2 つある時は 1 つの結び付け（L16、制限）。
- identity を 96 で切った時の衝突、同じ機種 2 台 serial 無し（制限）。
- 加速度 sensor による自動の回転（driver が無い）。
- Notes の toolbar の折り返し（D5、WS079）。
- Pi 4 の S1-b（常に TFU）: p015 で不要の見込み。

## 15. ライセンスと転記

- 新しい code は全て Zlib（`Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib`）。
- この設計で参照したのは: Wayland の `wl_output.transform` の意味（MIT の protocol の XML）、Vulkan の `VkSurfaceTransformFlagBitsKHR`・`VkDisplaySurfaceCreateInfoKHR.transform`・`preTransform`・render pass の dependency（仕様）、HID の Usage Tables（USB-IF）、**tree の中の MIT の i915 の参照** `plan/ws031/linux-parity/linux-reference/ubu-i915-src/display/skl_universal_plane.c`（SPDX MIT、Copyright Intel。WS031 の固定の参照環境。回転の可否の事実 §1.3 を読んだだけで、code は写さない）。
- i915 の `MODE_ROTATE_*`・`PLANE_CTL_ROTATE_*` は tree の `src/drivers/gpu/i915/intel/plane.h`（WS031 が Linux v6.8.12 の MIT の header から textually に抽出、出典と sha256 を header の注記に記録済み）の既存の定数を使う。`i915_skl_plane_ctl_rotate()`（`plane.c:778-803`、Zlib の port）も既存。新しい code は `hw.rotation` を渡す 1 本の経路だけ。
- bcm2711 の反転の bit の値 `0x80008000` は tree の `list.c:331` の既存の値（WS141 の規則で入った値）。WS217 は Guardrail の GPL の参照の規則の対象外なので、vc4 の source は読まない（読む必要が出たら D15）。
- hid-input の property の規則は HID の usage の意味から自分で書く（Linux の hid-input は読まない）。
- weston・mutter・sway・libinput・Mesa の wsi_display は読んでいない。
- Intel の公開の PRM は p009 で読む（仕様の引用は出典つきで phase.md に 1 行。code の転記は無い）。

## Event

- 2026-10-13 / ws217-p001-design-v1: P4 が第 1 版を書いた（source は agent/p4 28e5fe929）。
- 2026-10-13 / ws217-p001-review-1: design-reviewer の敵対的レビュー（致命的 2・重大 6・中 14・軽微 10、[review-1.md](review-1.md)、main f50beb833 で照合）。
- 2026-10-13 / ws217-p001-design-v2: P2 が第 2 版を書いた（review-1 の全指摘に対応、D1〜D6 と「回転は display ごと」「結び付けを含める」を設計に入れた。source は a07ad4cc7。残る問い D7〜D12）。
- 2026-10-13 / ws217-p001-review-2: Q1 の敵対的レビュー（致命的 2 C3・C4、重大 3 S7〜S9、中 10、軽微 15、[review-2.md](review-2.md)、main 5f096bb42 で照合）。判定「p002 はまだ始められない」。D7〜D12 は推奨で決定、S8・M8 も推奨を採用。
- 2026-10-13 / ws217-p001-design-v3: P2 が第 3 版を書いた（review-2 の全指摘に対応、ws.md の「ユーザーの決定（2026-10-13、scanout の回転）」を設計に入れた: 180° は scanout、90/270 は turn、Venus は無効表示、能力の告知、段 p009〜p011 の追加。source は main 217936c4c。新しい残る問い D13〜D17）。コードは書いていない。Q1 の review-3 待ち。
