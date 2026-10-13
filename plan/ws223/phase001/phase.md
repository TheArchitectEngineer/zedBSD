<!-- awesome-plan project=zedbsd record=ws223-p001 -->

# ws223-p001: 調べと設計 — 全画面は compositor の合成、game mode は Wayland の拡張で求められた時だけ、Video Player は game mode をやめる

Parent: [WS223](../ws.md)
Status: planning（第 1 版 2026-10-13 P3、design-reviewer の前）
Phase disposition: normal
Queue / attempts: Q1 の指示（2026-10-13、P3「p001 の設計を書き、ws.md の Phase の表を実装の段に分けて埋める」）。コードは書かない。

## 0. 要約（結論と、決めてほしい点）

ユーザーの原文（2026-10-13）:「Video Playerは、Game Modeをやめます。Game Modeでない全画面を実装します。Game Modeは別途、Wayland拡張で要求があったときのみ使用することにします。Game Modeを利用したアプリは現段階では存在しない、という整理にします。全画面ではコンポジターを利用しましょう。」

source を読んで分かったこと（§2 に行番号つき）:

1. **「Game Mode でない全画面」は既に実装されている。** 全画面の窓は普通に合成され（`display.c:8-19` の注記、ws099-p015）、不透明で何も重ならない時は壁紙・desktop・下の窓を描かずその窓 1 枚だけを描く（`shell.c:719-733` `fullscreen_whole`）。system bar は 2 秒で隠れ（ws035-p119）、端の swipe・App Home の角・Esc/F11 の戻り（BUG-194）・screen off の抑止（`sleep.c:401-405`、content type に依らない）は合成のまま効く。game mode は、その上に「直の scanout へ切り替える」規則（`scanout-rules.c`）が乗っているだけで、規則が「入らない」と答えれば今の合成の全画面になる。
2. **game mode の入り口は 1 つ。** `kwl_scanout_decide()`（`scanout-rules.c:26-60`）が、top の窓が fullscreen で、content type が **video か game**（`:37`）、client の GPU buffer（wl_shm でない）、出力と同じ大きさ、何も重ならない、screenshot 待ちが無い、pointer が 2000 ms 止まっている、display が断っていない、の全部が揃った時だけ `DIRECT` を返す。content type は `wp_content_type_v1`（`content-type.c`）だけから来る。
3. **今 `game` を求める app・package は無い。** `kl_window_set_content_type` を呼ぶのは Video Player の `videoplayer/main.c:240`（`KL_CONTENT_VIDEO`）だけ。移植した package（`userland/packages/`: GTK4、libavcodec、curl、openssh 等）に `wp_content_type` を使う物は無い。SDL・mpv は移植されていない。
4. **Pi 4 では game mode は今も入れない。** bcm2711 の display の QUERY は `GPU_DISPLAY_BLOB` を立てない（`display-device.c:698`）ので、backend の `scanout_display()`（`scanout-zedbsd.c:296-300`）が `ENOTSUP` を返し、`reason=backend` で合成に留まる。BLOB を立てるのは i915（`i915/display/display.c:2750`）と venus（`venus/display.c:791`）だけ。つまり **Pi 4 の全画面の動画は今日も合成で、この決定で Pi 4 の経路は変わらない**。game mode の実機の証拠は Venus（T1-244、`direct=1 switch_ms=678`）だけで、5330 の i915 の direct=1 は UAT 未実施（BUG-223）、Pi 4 は未試行。
5. WS217 p012（game の session の回転の停止）の session の定義（`KWL_SCANOUT_CONTENT_GAME`）は、この設計の推奨 (a) ではそのまま使える。p012 と WS217 の回帰の項 7・9 が前提にする「wltest の content type game の窓」は、wltest にまだ無い option なので、この WS で足す（p003）。

決めてほしい点（推奨つき、§9 にまとめ）:

| ID | 問い | 推奨 |
| --- | --- | --- |
| D1 | 「Wayland の拡張で求めた」の形 | **(a) `wp_content_type_v1` の `game` のまま**（標準の拡張、今の実装、WS217 p012 の定義も不変）。`video`・`photo`・`none` では入らない |
| D2 | Video Player の `kl_window_set_content_type(KL_CONTENT_VIDEO)` の呼び | **残す**（「動画を映している」という正しい hint で、compositor は `game` 以外で game mode に入らないので「game mode を求めない」は成り立つ）。外す案も可（§6） |
| D3 | BUG-223（UAT「全画面は直の scanout にしたい」）の処置 | **この決定で置き換えて resolved**（全画面は合成、直の scanout は `game` の client だけ）。記録は Q1 |
| D4 | Pi 4 の「全画面の再生が合成で滑らかに動く」の受け入れ | **計測の記録だけで受け入れる**（経路は今と同じ。1080p は player の CPU の decode・拡大縮小と bcm2711 の raster→UIF の CPU 変換（WS141 p015 A4 で消える）が律速で、この WS の変更で速くも遅くもならない）。閾値はこの WS では決めない |
| D5 | Pi 4 で全画面の窓を HVS の plane に載せる（compositor が持ち主のまま、WS214 の plane の延長） | **Future Work**（WS214 p004/p005 と WS141 p015 A4 の後に、計測で要れば） |
| D6 | guardrail の例外の表の文（「app が明示に頼む全画面（game mode）の時だけ」） | 「`wp_content_type_v1` の `game` を付けた全画面の時だけ」に直す（Q1） |

## 1. 由来・範囲・前提

- 由来: ws.md の原文。WS217 の D18（video の全画面にも game の session の回転の停止を当てるか）はこれで不要。
- 範囲: compositor の game mode の規則（`scanout-rules.*`、`scanout.c` の log）、Video Player、libkeiland の API の注記、試験（AAT のシナリオ・helper・host 試験・試験の client）、記録（WS122・BUG-223・guardrail・master の注記は Q1 へ提案）。
- 範囲の外: game mode の仕組みそのもの（backend の直の口、`kl_backend_scanout_*`、kernel の display の ioctl）は変えない。WS217 p012 の設計の改訂は WS217 の担当（この文書は依存と定義だけ渡す）。pointer constraints（mouse look の game）、Vulkan Video の GPU の decode（WS083・WS202）、Pi 4 の UIF（WS141 p015）は別の WS。
- 前提の規則: Guardrail「compositor は libvulkan だけを使う」の限定の例外（2026-10-06、game mode の直の scanout は backend の zedBSD の tree の中だけ）は維持。「画像の処理は GPU で」（2026-10-12）は Pi 4 の全画面の動画にそのまま当たる（§3.9）。

## 2. 調べた事実（source の行番号つき）

### 2.1 game mode の入り口の全部

| 所 | 事実 |
| --- | --- |
| `display.c:168-180` | pass の順: sleep の保留 → `kwl_top_window` → `kwl_scanout_pass(server, top)`。1 なら cursor の plane を止め（`kwl_cursor_plane_hold`、WS213 p004 の park）何も合成しない。0 なら `enter_window_mode`（swapchain が無ければ開き直す）→ 合成 |
| `scanout.c:59-101` `kwl_scanout_pass` | facts を集め `kwl_scanout_decide`。game mode の中: DIRECT かつ同じ窓なら `scanout_frame`、それ以外は `kwl_scanout_leave`。外: DIRECT でなければ `scanout_note`（log）して 0。DIRECT なら frame が飛んでいない時だけ `scanout_enter`（swapchain を閉じ、backend が display を claim） |
| `scanout.c:159-197` `scanout_facts` | pointer の静止（`server->pointer_x/y` の変化で `scanout_motion_ms` を更新）、`top->fullscreen`、`top->content_type`、`top->current->shm == NULL` で GPU buffer、`kwl_surface_size == server->width/height` かつ viewport 無し（`source[2] <= 0 && destination[0] <= 0`）で size、`scanout_overlay`、`kwl_shot_waiting`、`scanout_refused == top` |
| `scanout.c:207-266` `scanout_overlay` | `kwl_glass_overlay`（corner の hint・OSK・通知 …）、menu・network・bluetooth・volume・status panel・power dialog、switcher・lock・anim・transition、`glass && !bar_hidden`、他の client の sub-surface・popup |
| `scanout-rules.c:26-60` | 答えの順: NO_WINDOW → NOT_FULLSCREEN → **CONTENT（`content_type != VIDEO && != GAME`）** → BUFFER → SIZE → OVERLAY → SHOT → POINTER（`< 2000 ms`） → REFUSED → DIRECT |
| `scanout-rules.h:22-26` | `KWL_SCANOUT_POINTER_IDLE_MS 2000`、`KWL_SCANOUT_CONTENT_VIDEO 2`、`KWL_SCANOUT_CONTENT_GAME 3` |
| `scanout.c:374-393` `scanout_note` | fullscreen で content type が video か game の窓にだけ、理由が変わるたび `KWL SCANOUT direct=0 reason=… frames=0` を log |
| `scanout.c:275-303` `scanout_enter` | `kwl_compose_output_close`（swapchain を閉じる、R・C の thread は park）→ `kl_backend_scanout_open(width, height)`。失敗は `scanout_refused = top`、`reason=backend errno=…` |
| `scanout.c:311-349` `scanout_frame` | 新しい image だけ `kl_backend_scanout_present`。失敗は REFUSED で leave。全 surface の frame callback を済ます（`scanout_callbacks`） |
| `objects.c:354-357` | surface が去ると `kwl_content_type_object_gone` と `kwl_scanout_surface_gone`（game mode を抜け、refused・noted を忘れる） |
| `kwl.h:1516-1531` | `server->scanout`・`scanout_surface`・`scanout_front`・`scanout_frames`・`scanout_reason`・`scanout_refused`・`scanout_noted`・`scanout_pointer[2]`・`scanout_motion_ms` |
| `keiland-backend-display.h:61-86` | `kl_backend_scanout_open/present/close`。zedBSD 以外は ENOTSUP（`drm/display-drm.c:116-160`） |
| `scanout-zedbsd.c:296-300` | 最初の display が `GPU_DISPLAY_CONNECTED | GPU_DISPLAY_BLOB` でなければ **ENOTSUP** |
| `bcm2711/display-device.c:698` | QUERY の flags は `CONNECTED | ACTIVE | FIFO | HAS_CURSOR | HAS_OVERLAYS`。**BLOB 無し** → Pi 4 では game mode に入れない |
| `i915/display/display.c:2750`、`venus/display.c:791` | BLOB あり |
| `ws213/phase004/phase.md:76` | game mode の pass では `kwl_cursor_plane_hold`（C の park）。session の pause は output の close で C を止める |
| Makefile（zedBSD・Linux・FreeBSD） | `content-type.c`・`scanout.c`・`scanout-rules.c` は 3 つの build に入る（Linux・FreeBSD は backend が ENOTSUP） |

### 2.2 content type の経路（client → compositor）

| 所 | 事実 |
| --- | --- |
| `protocol.c:83` | global 28 `wp_content_type_manager_v1` version 1 |
| `content-type.c:45-98` | `get_surface_content_type`（surface に 1 つ、2 つ目は `already_constructed`）、`set_content_type`（3 より大きい値は none）、pending → commit で適用（`kwl_content_type_commit`、`protocol.c:1080`）、object の destroy で次の commit に none |
| `content-type.c:117` | 変わった時 `KWL CONTENT surface=N type=photo|video|game|none client=C` |
| `libkeiland/ui/window.c:898-921`、`:1843-1845` | `kl_window_set_content_type`: manager を bind していれば surface の object を作り `set_content_type`。無ければ ENOTSUP |
| `keiland.h:3972-3984` | KL_VERSION 42。注記「A fullscreen video or game may then be shown without composing」 |
| `libwayland/content-type-v1-client-protocol.h` | client 側の binding（enum と 2 つの request）。wltest は `desktop/libwayland` を link している（`tests/wltest/Makefile`）が、bind しているのは `wl_compositor` と `xdg_wm_base` だけ（`window.c:273-289`） |
| `videoplayer/main.c:239-240` | 「The window shows a video: in full screen the compositor may show it without composing (game mode, ws122-p005b)」→ `kl_window_set_content_type(player.window, KL_CONTENT_VIDEO)` |

### 2.3 全画面の合成の経路（game mode に入らない時の全画面）

| 所 | 事実 |
| --- | --- |
| `protocol.c:1565-1640` `kwl_window_enter_fullscreen` | head の窓は anchor へ、戻り先を覚え、`fullscreen = 1`、docked なら `fullscreen_docked`。configure は出力の大きさ（`:1729`）、log `KWL CONFIGURE … fullscreen=1` |
| `protocol.c:1652-1693` `kwl_window_leave_fullscreen` | 元の場所と大きさ、置かれていなければ中央（ws035-p138）。log `KWL WINDOW unfullscreen` |
| `shell.c:719-733`、`:1556-1600` `fullscreen_whole` | top が fullscreen で、App Home・Wiseview が閉じ、何も動かず開いておらず（`kwl_glass_still`）、image が不透明で sub-surface も glass panel も無い時、**壁紙・desktop・下の窓を描かず** `draw_window(top)` と popup・power dialog・bluetooth ask だけ |
| `shell.c:42-77`、ws035-p119 | 全画面の窓は whole に保ち、bar は 2 秒で隠れ（`bar_hidden`）、端の gesture から出す。「A fullscreen window is composed like any other」 |
| `shell.c:277-290`、BUG-194 | fullscreen から戻る key（Esc・F11）は compositor が持つ |
| `sleep.c:390-405` | 全画面の窓が前にある間は無入力の時間を数えない（content type に依らない） |
| `libkeiland/ui/present.c:8-28` | `KL_PRESENT_VULKAN` の窓は CPU の canvas を staging に写し swapchain の image へ（変わった部分だけ、ws210-p013）。swapchain の image は Wayland の WSI の共有の GPU buffer（LINEAR、`wsi-image.c`）なので `facts->gpu_buffer = 1`、全画面では `size_matches = 1` |
| `videoplayer/main.c:909-910`、`:829` | 毎 frame、decode した picture を CPU で窓の大きさに拡大縮小・BGRA 変換（`app_frame_scale`）して `kl_window_present` |

### 2.4 content type・fullscreen を求める app と package

| 誰 | content type | fullscreen | 備考 |
| --- | --- | --- | --- |
| Video Player（`videoplayer/main.c`） | **video**（`:240`） | F11・Alt+Enter・double click・menu の Full Screen（`:472-519`、`:580-583`） | 唯一の content type の利用者 |
| Image Viewer（`imageview/main.c:251-252`、`:998-1022`） | 無し | `--fullscreen`・key | photo の hint も付けていない |
| Terminal（`terminal/main.c:1132-1135`） | 無し | key | — |
| Notes（`notes/main.c`） | 無し | `kl_window_set_fullscreen` | — |
| Photos・Browser・他の app | 無し | 無し | — |
| wltest（`tests/wltest/main.c:82`） | 無し | `--fullscreen-at=N`・`--unfullscreen-at=N`・`--fixed`・`--size` | Vulkan WSI の client。WS217 §12.2 の 7・9 が「content type game の窓」として使う前提（option は無い） |
| mview（`tests/mview/window.c:163`） | 無し | `xdg_toplevel_set_fullscreen` | — |
| xserver | 無し | 無し | — |
| package（GTK4・libavcodec・curl・openssh・openssl・fonts・libs） | 無し（`grep` 0 件） | — | SDL・mpv・ffplay は移植されていない |
| ime-probe・libbrowser の `content_type` | text-input の content type・HTTP の Content-Type | — | 無関係 |

結論: **今 `game` を求める app は無い**。`video` を求めるのは Video Player だけ。

### 2.5 game mode の証拠と、実機での実態

- T1-244（2026-10-06、QEMU Venus 1280x800）: F11 の後 `ZWL SCANOUT direct=1 surface=17 client=3 switch_ms=678`、撮影の前に `direct=0 reason=shot`。
- 5330（i915）: BUG-223 の「残り: 実機の UAT だけ」。direct=1 の実機の証拠は無い。
- Pi 4: BLOB 無し（§2.1）。`reason=backend errno=ENOTSUP` になる。試みの記録も無い。

### 2.6 WS217 との関係（ws.md 末尾、review-4）

- WS217 ws.md:68-69・73: game mode では hardware でできない回転を外す（p012）。D18 は WS223 で不要。p012 の対象は「game mode を Wayland の拡張で求めた client」だけ。
- review-4 M26: 「session の定義（今の `KWL_SCANOUT_CONTENT_GAME`）は WS223 p001 の『どの拡張を求めとするか』に依存する」「WS223 が `wp_content_type_v1` 以外の拡張を選ぶと、p012 の入り口の fact と試験（wltest の content type game）が変わる」。S14〜S16 は p012 の内部（lock・head・期限・資源）で、この WS の範囲ではない。
- WS217 p001 §12.2 の 7（p002〜p011 の間の回帰）・9（p012）は「wltest の fullscreen・content type game の窓」を使い、9 の末尾に「D18 が『game だけ』なら Videoplayer の fullscreen は `reason=rotated` のまま」とある → この WS の後は Video Player の全画面は **`reason=content`（log 無し）** になる（§3.10）。

### 2.7 試験と記録で game mode・video を前提にしている物

| 物 | 内容 | 参照元（AGENTS.md の整理の基準） |
| --- | --- | --- |
| `tests/scenarios/apps/videoplayer/fullscreen.md` | step 1 の段落「game mode（ws122-p005b）: … `ZWL SCANOUT direct=1` …」、frontmatter の `paths` に scanout.c・content-type.c・scanout-zedbsd.c | tests/ のシナリオ（残す、直す） |
| `plan/tools/aat/scenarios/helpers_apps.py:686-693` | `apps.videoplayer.fullscreen` が `KWL SCANOUT direct=1|direct=0 reason=(backend|refused)` を 8 秒待ち、無ければ **fail**、`reason=shot` を step に、`item.person` に「game mode's line」 | 同上（直さないと変更の後に必ず FAIL） |
| game mode の host 試験 | **無い**（WS122 の試験は完了時に削除。`plan/tools/compositor/` は host-role だけ。WS217 §12.1 の「host-scanout-rules」は計画だけ） | 足す（p003） |
| game mode の AAT | `apps.videoplayer.fullscreen` の中だけ。この WS の後は game mode を通す client が無くなる | 足す（p003、wltest） |
| `plan/ws122/ws.md:14, 28` | 「全画面と game mode の direct scanout（T1-237・T1-244）」 | 完了した WS の結果。Q1 が日付つきの注記を足す |
| `plan/bugs/BUG-223.md`、`known-bugs.md:93` | 「全画面（composition を通さない直接の scanout、game mode）にしたい」tracking | D3 |
| `plan/guardrail.md:224` | 例外の表「app が明示に頼む全画面（game mode）の時だけ」 | D6 |
| `plan/tools/gpu-boundary/v1-check.sh:11-14` | 例外の注記（scanout-zedbsd.c が唯一の ioctl の file） | 不変 |
| `plan/master.md:762-764` | 2026-10-06 の決定の履歴 | 履歴、不変 |
| `plan/agents/T1/requests.md` T1-244 | 済み | 履歴、不変。未実行の依頼に game mode の物は無い |
| `docs/` | game mode・content type の記述 **無し** | — |
| `keiland.h:3972-3978`、`scanout.c:8-31`、`display.c:170-174`、`kwl.h:763-765, 1516-1522`、`keiland-backend-display.h:61-70`、`scanout-zedbsd.c:8-17`、`content-type.c:8-12`、`scanout-rules.h:8-10, 24` | 注記の「video or game」 | p002 で「game」に直す |

## 3. 設計

### 3.1 責務と境界（変えない物）

- compositor（`userland/desktop/wayland/`）が display の持ち主。全画面の窓は compositor が合成する（`fullscreen_whole` の経路）。
- game mode（直の scanout）は、`kwl_scanout_decide` の規則が DIRECT と答えた時だけ、backend の口（`kl_backend_scanout_*`、zedBSD だけ）を通す。Guardrail の例外は維持。
- client の「求め」は Wayland の拡張 `wp_content_type_v1` の `game`（D1）。libkeiland は `kl_window_set_content_type`（KL_VERSION 42）でそれを通す。ABI・protocol の version は変えない。
- kernel・libvulkan・backend・HAL は変えない。

### 3.2 「Wayland の拡張で求めた時だけ」の形（D1）

| 案 | 中身 | 利点 | 欠点 |
| --- | --- | --- | --- |
| **(a) `wp_content_type_v1` の `game`（推奨）** | 今の実装のまま、`video` を条件から外す | 標準の拡張（wayland-protocols の staging）。移植する game・emulator・SDL3 の app は（私の知識では SDL3 は全ての窓に `game` を付ける。tree には無く未確認）そのまま求める。WS217 p012 の session の定義・試験が不変。実装は規則の 1 行と注記 | `game` は「hint」であって「直の scanout の命令」ではない（他の compositor は latency の最適化の判断に使う）。今の規則（size・GPU buffer・overlay・pointer 2 秒）が揃った時だけ入るので、hint の意味に合う |
| (b) zedBSD 独自の拡張（例 `kl_game_mode_v1`、request `claim`／event `granted`・`revoked`） | 明示の「直の scanout を求める」契約と、compositor からの answer | 意図が明確。answer で app が frame pacing を変えられる | 今の app は誰も使わず、移植した app も使わない → game mode の code が永久に未使用。protocol・libkeiland の API・wltest・host 試験の追加が要る。WS217 p012 の定義と試験を変える（review-4 M26 の懸念）。「Game Mode を利用したアプリは現段階では存在しない」の整理にも (a) で足りる |
| (c) (a) + 独自の拡張を後で | `game` で入り、将来 answer が要る app が出たら (b) を足す | 今は (a) の費用だけ | — |

推奨は (a)。(c) は Future Work に「game の client への answer（直の scanout の可否・refresh）」として残す。

`photo`・`video`・`none` は hint として受け（log `KWL CONTENT` は残す）、game mode の条件にはしない。

### 3.3 規則の変更（p002 の差分の設計、code は書かない）

| file | 変更 |
| --- | --- |
| `scanout-rules.h` | `KWL_SCANOUT_CONTENT_VIDEO` を削除。`KWL_SCANOUT_CONTENT_GAME 3U` は残す（値は `wp_content_type_v1` の enum）。注記「a fullscreen video or game」→「a fullscreen game（the one content type that asks, WS223）」。答えの番号（0〜9）と `facts` の struct は不変（WS217 p002a が `KWL_SCANOUT_ROTATED 10`、p012 が `UNROTATE 11` を足す前提を壊さない） |
| `scanout-rules.c:37` | `if (facts->content_type != KWL_SCANOUT_CONTENT_GAME) return KWL_SCANOUT_CONTENT;`。他の順は不変 |
| `scanout.c:382` `scanout_note` | `top->content_type != KWL_SCANOUT_CONTENT_GAME` なら返す（video の窓は log しない）。`:8-31` の注記を「a fullscreen window that says it shows a game」に |
| `display.c:170-174`、`kwl.h:763-765, 1516-1522`、`keiland-backend-display.h:61-70`、`scanout-zedbsd.c:8-17`、`content-type.c:8-12` | 注記の「video or game」→「game」 |
| `keiland.h:3972-3978` | 「A fullscreen **game** may then be shown without composing (the compositor's game mode); a video is composed like any other window (WS223)」。KL_VERSION は上げない（API も ABI も不変） |
| `videoplayer/main.c:239-240` | D2 で「残す」なら注記だけ「The window shows a video (a hint; the compositor composes a fullscreen video, WS223)」。「外す」なら 2 行を削る |

Linux・FreeBSD の build は同じ source なので同じ変更が入る（backend は ENOTSUP のまま）。

### 3.4 状態遷移

game mode の状態機械（window mode ⇄ game mode、leave の理由、refused の寿命）は不変。変わるのは「誰が入れるか」だけ:

| 窓 | 変更前 | 変更後 |
| --- | --- | --- |
| fullscreen の video（Video Player） | 条件が揃えば DIRECT（swapchain を閉じ claim）。揃わない間 `KWL SCANOUT direct=0 reason=…` を理由ごとに log | 常に `KWL_SCANOUT_CONTENT`、**log 無し**。`enter_window_mode` は既に windowed なので何もせず、`fullscreen_whole` が窓 1 枚を描く。bar は 2 秒で隠れ、cursor の plane・端の gesture・screenshot は普通の窓と同じ |
| fullscreen の game（content type game） | 同上 | 不変（size・GPU buffer・overlay・shot・pointer 2 秒・refused） |
| fullscreen の photo・none | CONTENT、log 無し | 不変 |

Video Player の全画面の 1 frame の流れ（変更後、QEMU・5330）: player が `kl_window_present`（staging → swapchain の image の変わった部分の copy）→ commit → compositor が `adopt_commit` → `fullscreen_whole` → `draw_window`（image を sample する 1 枚の quad、不透明の pipeline）→ swapchain の present（i915・venus は flip、bcm2711 は scanout の image への copy）。

### 3.5 資源の所有権・寿命・並行性・失敗と回復

- 変更は規則の判定だけで、所有権（`scanout_front` の buffer の参照、backend の fd・lease）、寿命（surface が去った時の leave、refused の忘れ）、thread（R・C の park は `kwl_compose_output_close` の中、WS213 p004）、失敗（claim・present の失敗 → refused → 合成）は不変。
- 新しい失敗の経路は無い。video の窓で `scanout_enter` が呼ばれなくなるので、Venus で観測された switch（678 ms、swapchain の閉じ開き）が全画面の動画で起こらなくなる（T1-244 の `switch_ms`）。これは全画面に入る時の 1 回の止まりが消える、という改善。
- 割込み・DMA: kernel には触れない。直の scanout の ioctl の経路は game の client の時だけ、今のまま。

### 3.6 Video Player（D2）

- game mode をやめる＝player の側では「求めない」こと。推奨は `KL_CONTENT_VIDEO` の呼びを残す: (1) `wp_content_type_v1` の `video` は標準の hint で、compositor が後で別の目的（例: 全画面でなくても screen off を抑える、WS217 の回転の policy、色の処理）に使える、(2) AAT の `ZWL CONTENT surface=N type=video` の行が残る、(3) compositor の規則で `video` は game mode に入らないので「求めない」は成り立つ。外す案は「Video Player の source に game mode の痕跡を残さない」が利点だが、hint の情報を捨てる。
- F11・Alt+Enter・double click・menu の Full Screen・Esc（`main.c:453-519`、BUG-194 の compositor の key）は不変。「Game Mode でない全画面」は既にこの経路。

### 3.7 libkeiland の API

- `kl_window_set_content_type` と `KL_CONTENT_*` は不変（注記だけ）。`kl_window_set_fullscreen`・`kl_window_fullscreen` は不変。
- 新しい API は足さない（(b) を採らないため）。

### 3.8 既存のフレームワークとの関係

- WS213（thread 化）: game mode の pass の park（p004 §76）は不変。video の全画面では park が起こらなくなる（R・C が動き続ける）。
- WS214（HVS の plane）: p001 §5.5 の plane の条件「fullscreen でない」は drag の窓の plane の条件で、この WS とは独立。全画面の動画を plane に載せる案は D5（Future Work）。WS217 §4.5 の「`rotation != 0` では plane に載せない」も独立。
- WS211（damage）: 全画面の video の commit は窓全体の damage（player は毎 frame 全体を描く、`present.c` の注記）。変更なし。
- WS141 p015（UIF）: A4（WSI の swapchain の image を UIF に）は「HVS は UIF を読めない」（p015 の末尾）ので、A4 の後は game の client の buffer が UIF なら直の scanout に出せない。`kl_backend_scanout_present` が kernel の拒否（EINVAL）を返し `REFUSED` で合成に落ちる、今の失敗の経路で足りる。p015 が `scanout-rules` を A4 の file に挙げている（`phase015/phase.md:121`）のは「buffer が scanout できる物か」の fact を足す予定と読む。この WS では足さない（Pi 4 は BLOB 無しで元々入らない）。
- WS083・WS202（Vulkan Video、libmedia の自前の decoder）: player の frame の作り方（CPU の NV12→BGRA の拡大縮小、WS202 H6「CPU」）はこの WS の外。全画面の動画の Pi 4 の速さはそちらが律速（§3.9）。
- WS113（複数 display）: 全画面は anchor へ（`protocol.c:1579-1582`）、game mode も anchor だけ。不変。

### 3.9 全画面の合成の速さ（5330・Pi 4）

事実と見積もり（計測は p004 で記録。数字の出典を明記、推測は「見積もり」と書く）:

- **5330（i915、1920x1200・1920x1280）**: 全画面の合成は 1 枚の不透明な quad と flip。desktop 全体が 60 fps で動く実機の確認（master 2026-10-13「動作確認できました」）があり、1 枚の quad が律速になる事実は無い。game mode の利点は合成の 1 frame の遅れ（≤ 16.7 ms）と GPU の 1 pass の省略だけで、費用は入る時の swapchain の閉じ開き（Venus で 678 ms、i915 は未測）と、overlay・pointer・shot のたびの往復。**合成で滑らかに動く、の受け入れは `VIDEOPLAYER FRAMES shown= late=` の late と、compositor の `KWL LAT` の frame の間隔で判定**（§4）。
- **Pi 4（bcm2711）**: game mode は BLOB 無しで入れないので、**今も合成**。この WS の前後で経路は同じ。全画面の動画の費用（1920x1080、4 byte、8.3 MB/frame）の内訳: (1) player の decode（libavcodec の software、SMP 前）と CPU の拡大縮小・BGRA 変換（8.3 MB の書き）、(2) libkeiland の staging → swapchain の copy（GPU、8.3 MB の読み書き）、(3) compositor の sample の前の **raster→UIF の CPU 変換（kernel、1904x1020 で約 50 ms、WS141 p015 の問題 2）** — これが compositor の側の律速で、WS141 p015 の A4（UAPI の承認の後）で消える、(4) 1 枚の quad の render（p013 の計測で 1 run 約 23 ms が desktop の全画面の pass、動画は 1 枚の texture なので短い、見積もり）、(5) present の copy（swapchain → scanout の image、GPU の TLB copy、全 tile）、(6) HVS の読み 498 MB/s（60 Hz）。memory の帯域の合計は見積もりで frame あたり約 50 MB、30 fps で 1.5 GB/s（LPDDR4 の実効の数 GB/s の中）。**(3) だけで 1080p は 20 fps 未満**になるので、Pi 4 の 1080p の全画面の動画は A4 の前には滑らかにならず、それは game mode の有無と無関係。1280x720・1024x600 では (1)(3) が比例して軽い。
- WS214 の overlay の plane（D5）: 全画面の動画の窓を HVS の overlay（LINEAR）に載せれば (3)(4)(5) が消え、compositor は primary の swapchain を持ったまま、bar・gesture の feedback は上の帯の plane か合成への fallback で出せる。これは「compositor の合成」の一種（持ち主は compositor、libvulkan の overlay の API、WS214 p004）で game mode ではない。WS214 p004（libvulkan）・p005（compositor の drag の plane）と帯域の実測（p003b）の後に、計測で要れば別の Phase（WS214 か新しい WS）。Future Work に登録（Q1）。
- **Venus（QEMU）**: 合成の 1 frame は F-021 の通り遅く（1280x800 で約 100 ms）、動画の滑らかさの判定には使わない。AAT は log の行と撮影だけ。

### 3.10 WS217 p012 への受け渡し

- session の定義: 「top の窓が fullscreen で、`wp_content_type_v1` の content type が `game`（`KWL_SCANOUT_CONTENT_GAME`）」のまま。video は含めない（D18 は不要）。
- WS217 p001 第 5 版への依頼（WS217 の担当が書く、Q1 経由）: §4.12 の「video は D18」を「不要（WS223 D1）」に、§10 の D18、§12.2 の 9 の末尾「Videoplayer の fullscreen は `reason=rotated` のまま」を「Video Player の全画面は content で合成（log 無し）」に、§13 の p012 の依存に **WS223 p002・p003**（規則と wltest の `--content-type=game`）を足す。
- WS217 §12.2 の 7（p002〜p011 の間の回帰「90 の出力で wltest の fullscreen・content type game の窓が `reason=rotated`」）も p003 の wltest に依存する。

### 3.11 記録の整理（Q1 への提案の文）

| 記録 | 提案 |
| --- | --- |
| `plan/ws122/ws.md` 結果 | 末尾に「2026-10-13 ユーザー（WS223）: Video Player は game mode をやめ、全画面は合成。game mode は `wp_content_type_v1` の `game` を付けた client だけ。T1-237・T1-244 の direct=1 は当時の証拠として残す」 |
| BUG-223 | D3 で resolved: 「2026-10-13 ユーザーの決定（WS223）で、全画面は合成に、直の scanout は game の client だけ。UAT の希望は置き換え。実機の direct=1 の確認は不要」。known-bugs.md の行も |
| guardrail 例外の表 | D6 の文 |
| master | WS223 の行の状態、Future Work の 2 件（§7）、decisions-log |
| WS217 ws.md | p012 の行の依存に WS223 p002・p003 |

## 4. 試験計画

### 4.1 host（guest を起動しない）

- **新しい host 試験 `plan/tools/compositor/scanout-rules-host-test.c`・`.sh`**（p003。長く使う回帰なので最初から `plan/tools/` に置き、master の Tools に登録を Q1 に頼む）: `userland/desktop/wayland/scanout-rules.c` を単独で compile（server に依存しない設計、`scanout-rules.h:13`）し、表で確かめる: game+fullscreen+gpu+size+still → DIRECT、**video+同じ → CONTENT**、photo・none → CONTENT、game で shm → BUFFER、size 違い → SIZE、overlay → OVERLAY、shot → SHOT、pointer 1999 ms → POINTER、refused → REFUSED、window 無し → NO_WINDOW、non-fullscreen → NOT_FULLSCREEN、`kwl_scanout_reason_name` の 10 個と範囲外「unknown」。ASan・UBSan。WS217 p002a（ROTATED）・p012（UNROTATE）はこの表に行を足す。
- build の関門: zedBSD（amd64）の `$(BUILD)/bin/wayland`・`videoplayer`・`wltest`・`dynamic/libkeiland.so` を名指して make、warning 0。Linux・FreeBSD の compositor の build（`Makefile.linux`・`Makefile.freebsd`）は source が同じなので、Q1 の判断で keiland-linux の build 1 回。
- `plan/tools/gpu-boundary/v1-check.sh`・`keiland-os-boundary/check.sh` を流す（変更しないが、file の一覧の整合）。

### 4.2 QEMU（T1、AAT）

- `apps.videoplayer.fullscreen`（直す、p002）: step 1 の game mode の段落を「content type video の行 `ZWL CONTENT surface=N type=video` は出る。**F11 の後 3 秒待っても `ZWL SCANOUT` の行は出ない**（全画面は合成）」に置き換え、`paths` から scanout.c・scanout-zedbsd.c を外す。helper は `run.wait(... 8)` の check を「3 秒の間に `KWL SCANOUT direct=1` が無い」の否定の check に、`reason=shot` の step を削除、`item.person` の文から game mode を外す。撮影（f11・double-click・bar-hidden）は残す。
- **新しい `desktop.game-mode.wltest`**（p003、`tests/scenarios/desktop/game-mode/wltest.md`、`machine: either`、`full.suite` に `desktop.game-mode.*` を足す）: AAT の image に wltest（`plan/tools/aat/config-amd64-aat.mk` に `ZEDBSD_USER_PROGRAMS += wltest`）。kei で `wltest --content-type=game --fullscreen-at=1 --frames=600 --mode=fifo` → `ZWL CONTENT … type=game`、pointer を止めて 3 秒以内に `ZWL SCANOUT direct=1`（Venus、T1-244 の形）か `direct=0 reason=backend`（BLOB の無い display）、撮影の前に `reason=shot`、mouse を動かすと `reason=pointer`、2 秒後に `direct=1`、Esc か `--unfullscreen-at` で `direct=0 reason=not-fullscreen`、窓を閉じて `reason=no-window`。同じ scenario に **`--content-type=video` の対照**（`ZWL SCANOUT` の行が出ない）を 1 段。compositor の log に `FAILED`・`MODE_ERROR` が無い。
- 回帰: `apps.videoplayer.play`・`apps.videoplayer.open-from-home`、`desktop.windows.*` の既存。

### 4.3 実機

- **5330（i915、UAT か T1 の SSH）**: (1) Video Player の全画面（sample.mp4 1080p H.264）で `VIDEOPLAYER FRAMES shown=150 late=…`（late が window の時と同じ桁）、`KWL SCANOUT` の行が無い、bar が 2 秒で隠れ端の swipe が効く、Esc で戻る。(2) `wltest --content-type=game` で `direct=1`（BUG-223 の残りだった i915 の direct=1 の証拠を、この client で取る。失敗なら `reason=backend errno=` を記録）。
- **Pi 4（ユーザーの焼き直し）**: Video Player の全画面（1280x720 と、ディスプレイの mode が 1080p なら 1080p）で `FRAMES shown= late=` と `KWL LAT`（`--log-frames`）の frame の間隔の p50/p90、kernel の `v3d: perf` の texture の CPU 変換の数を記録する。合否は D4（記録だけ）。`wltest --content-type=game` は `reason=backend errno=ENOTSUP`（§2.1 の事実の確認）。
- QEMU の証拠と実機の証拠は分けて書く。未実施は「未実施」と書く。

## 5. 段（Phase）と受け入れ条件

| 段 | 内容 | 受け入れ | 依存 |
| --- | --- | --- | --- |
| p001（これ） | 調べと設計 | design-reviewer（致命なし）→ ユーザーの D1〜D6 | — |
| **p002** | 規則: `scanout-rules.h/.c` の CONTENT を game だけに、`scanout_note` を game だけに、注記（§3.3 の表）、Video Player の注記か呼びの削除（D2）、`keiland.h` の注記。AAT の `apps.videoplayer.fullscreen` と helper の直し（§4.2）。host 試験はまだ無いので build（warning 0）と自分の review | zedBSD の build warning 0。T1: `apps.videoplayer.fullscreen`（直した物）・`apps.videoplayer.play` PASS、compositor の log に `KWL SCANOUT` の行が無い | p001、D1・D2 |
| **p003** | 試験の client と host 試験: wltest に `--content-type=none|photo|video|game`（`wp_content_type_manager_v1` を bind、surface に object、最初の commit の前に set）、`desktop.game-mode.wltest` の scenario と `full.suite`、AAT の image の `wltest`、`plan/tools/compositor/scanout-rules-host-test`（§4.1）。master の Tools への登録は Q1 | host 試験 PASS（ASan・UBSan）。T1: `desktop.game-mode.wltest` で Venus の `direct=1`／`reason=shot`／`reason=pointer`／`reason=no-window` と video の対照 PASS | p002 |
| **p004** | 実機の確かめ（§4.3）: 5330 の Video Player の全画面と wltest の game、Pi 4 の計測の記録。依頼は Q1 → T1／ユーザー | 5330: `KWL SCANOUT` 無しの全画面の再生、`late` が window と同じ桁、wltest の `direct=1`（または理由の記録）。Pi 4: 数字の記録（D4） | p003 |
| **p005** | 記録の整理の確認（§3.11 の Q1 の編集が済んだこと、WS217 p001 第 5 版の反映）と規約の全文の見直し（p002・p003 の C と Python の変更） | `plan/coding-style.md` の全文に照らした review、style-check | p002〜p004 |

T1 の束: p002 と p003 は 1 つの image でまとめてよい（Q1 の判断）。p004 の 5330 は T1-435 の形の SSH、Pi 4 はユーザー。

## 6. 選ばなかった案と理由

| 案 | 理由 |
| --- | --- |
| (b) zedBSD 独自の game mode の拡張 | §3.2。利用者が無く、標準の hint で足りる。WS217 p012 の定義を変える |
| content type に依らず「全画面＋GPU buffer＋size 一致」で直の scanout（旧 fullscreen mode、ws099-p015 で削除） | ユーザーの 2026-09-30 と 2026-10-06 の決定（普通の全画面は合成、端の swipe を保つ）に反する |
| `video` も残して「Video Player だけ `KL_CONTENT_NONE` にする」 | 原文「Game Mode は Wayland 拡張で要求があったときのみ」の「要求」が `video` を含むと、移植した player（mpv は `video` を付ける）が勝手に game mode に入る。`game` だけが「求め」 |
| game mode の code（scanout.c・backend の口）を削る | 原文は「別途、Wayland 拡張で要求があったときのみ使用」で、残す。削ると WS217 p012 と Guardrail の例外の前提が消える |
| Pi 4 に BLOB を足して game mode を Pi でも使えるようにする | ユーザーの決定の方向（全画面は合成）に逆行。Pi 4 の速さは WS141 p015・WS214 の plane で取る（D5） |
| pointer の 2 秒の規則を game 向けに変える（mouse look） | 範囲の外。pointer constraints（lock・confine）の protocol が compositor に無いので、今は game の pointer が動く限り合成。Future Work |
| KL_VERSION を上げる | API・ABI が変わらない（注記だけ） |

## 7. Future Work（Q1 が登録）

1. Pi 4 の全画面の窓（動画）を HVS の overlay の plane に載せる（compositor が持ち主のまま、WS214 p004 の API、bar・gesture は帯の plane か fallback）。きっかけ: WS214 p005 と WS141 p015 A4 の後の Pi 4 の全画面の動画の計測（p004）。
2. game の client への answer（直の scanout に入った・出た・refresh）と pointer constraints。きっかけ: game mode を使う app の移植（SDL3 等）。

## 8. ライセンス

- 変更する source は全部 zedBSD（Zlib、`SPDX-License-Identifier: Zlib`）。転記は無い。
- `wp_content_type_v1` は wayland-protocols（MIT）の staging の protocol で、tree の binding（`libwayland/content-type-v1-client-protocol.h`、`content-type.c`）は zedBSD の独自の実装。wltest の option は同じ binding を使う。
- GPL の source は読んでいない（Linux の vc4・i915、SDL・mpv の source も読んでいない。SDL3・mpv の content type の挙動は私の知識で、tree では未確認）。

## 9. 決めてほしい点（まとめ）

- D1 求めの形: (a) `wp_content_type_v1` の `game`（推奨）／(b) 独自の拡張／(c) (a)+後で (b)。
- D2 Video Player の `KL_CONTENT_VIDEO` の呼び: 残す（推奨）／外す。
- D3 BUG-223: この決定で resolved（推奨）／tracking のまま 5330 の UAT で `wltest` の direct=1 を見てから。
- D4 Pi 4 の受け入れ: 計測の記録だけ（推奨）／閾値（例 720p で late 0）を置く。
- D5 Pi 4 の全画面の plane 化: Future Work（推奨）／この WS に段を足す。
- D6 guardrail の例外の文の更新: Q1 が直す（推奨）。

## 10. 敵対的レビュー（自分で）

| # | 疑い | 確かめ・答え |
| --- | --- | --- |
| R1 | 「全画面の合成」は本当に既に実装されているか。game mode を外すと何か欠けないか | `display.c:8-19` と `shell.c:77`「A fullscreen window is composed like any other」、`fullscreen_whole` の経路、BUG-194 の key、ws035-p119 の bar。game mode は規則の上乗せで、`reason=content` の経路は photo・none の全画面（Image Viewer の `--fullscreen`）で今日も通っている |
| R2 | video を外すと `scanout_note` の log が消え、AAT が FAIL する | §2.7。p002 で helper を否定の check に直す。直さなければ必ず FAIL（`run.wait` 8 秒 → `item.check(scanout, …)`） |
| R3 | game mode を使う client が無くなり、game mode の code が未試験になる | p003 の wltest `--content-type=game` と `desktop.game-mode.wltest`、host 試験。WS217 p012 もこれに依存 |
| R4 | Pi 4 で「滑らかに動く」を受け入れられるか | Pi 4 は BLOB 無しで今も合成（§2.1）。この WS で速さは変わらない。律速は player の CPU と WS141 p015 の変換（§3.9）。D4 で「記録だけ」を問う。「滑らか」を約束しない |
| R5 | 5330 で game mode をやめると tearing・遅れが見えるか | 合成は FIFO の swapchain で tearing は無い。遅れは ≤ 1 frame。desktop が 60 fps で動く事実。p004 で `late` を記録 |
| R6 | `video` を hint として残すと、後で誰かが「video も game mode」と戻す | `scanout-rules.h` の注記と host 試験の行「video → CONTENT」が規則を守る。ユーザーの原文を注記に引く |
| R7 | WS217 p012 の設計が壊れるか | 定義は `CONTENT_GAME` のまま（M26 の懸念は (a) で解消）。p012 の依存に p002・p003 を足す（§3.10）。review-4 S14〜S16 は p012 の内部 |
| R8 | Linux・FreeBSD の build | 同じ source、backend は ENOTSUP。変更は判定だけ |
| R9 | WS141 p015 A4 の後、game の client の UIF の buffer を直に出せない | 既存の REFUSED の経路で合成に落ちる（§3.8）。p015 の A4 の側で「scanout できる buffer」の fact を足す計画があり、この WS は触らない |
| R10 | 「Game Mode を利用したアプリは現段階では存在しない」は正しいか | §2.4 の表（grep の結果、package も）。SDL3 は tree に無い |
| R11 | 入る時の swapchain の閉じ開き（678 ms）が無くなるのは改善だが、全画面の入りの configure の後の最初の frame（T1-235 の 960x600 の撮影）は別の問題 | ws122-p005a で直した（`VIDEOPLAYER PRESENTED` を待つ）。この WS の範囲外 |
| R12 | `KWL_SCANOUT_CONTENT_VIDEO` を消すと WS217 p001 の文書が参照する定数が無くなる | WS217 p001 §4.12 の「ユーザーが video も含めると決めたら `KWL_SCANOUT_CONTENT_VIDEO` も」は D18 の不要化で消える文（§3.10 の依頼） |

## 11. 確かめていない点（推測を含む）

- SDL3・mpv が `wp_content_type_v1` にどの type を付けるかは私の知識（SDL3 は全ての窓に `game`、mpv は `video`）で、tree に無く source も読んでいない。(a) の評価に使ったが、結論（今 `game` の app は無い）は tree の grep で確かめた。
- Pi 4 の全画面の動画の各段の ms（§3.9 の (1)(2)(4)(5)）は WS141 p013・p015 の既存の数字からの見積もりで、動画の再生そのものの計測は無い（p004 で取る）。
- 5330 の i915 の game mode の direct=1 は実機で未確認（BUG-223）。p004 の wltest で取る。
- `fullscreen_whole` の「image が不透明」の判定（`shell.c:1580-1583`、`image->draw == KWL_DRAW_OPAQUE`）は、import した image が既定で opaque（`import.c:61`「drawn opaque until set_alpha says otherwise」）で、libkeiland が alpha を告げるのは premultiplied の glass の窓だけ（`window.c:832`）、Video Player は `style.glass = 0`（`main.c:245`）なので真になる見込み。実機の log（`KWL` の frame の draw の数）で p004 に確かめる。偽でも「壁紙も描く」だけで、動作は正しい。
- wltest の `--content-type` は libwayland が `wp_content_type_manager_v1_interface`・`wp_content_type_v1_interface` を export している（`content-type-protocol.c:31, 49`）ので、`window_global` に 1 つ bind を足すだけで作れる（確認済み）。
