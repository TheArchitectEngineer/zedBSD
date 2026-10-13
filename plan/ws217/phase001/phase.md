<!-- awesome-plan project=zedbsd record=ws217-p001 -->

# ws217-p001: 設計 — 画面の回転（display ごとの 0°・90°・180°・270°）

Parent: [WS217](../ws.md)
Status: planning（第 1 版、2026-10-13 P4。design-reviewer と Q1 の確かめ、ユーザーの判断 D1〜D6 待ち）
Disposition: normal
Primary Milestone: MG006（WS から継承）
Queue / attempts: Q1 の指示（2026-10-13、P4「WS217 の p001 の設計を書き、ws.md の Phase の表を実装の段に分けて埋める」）。コードは書かない。
Purpose / goal: Settings の Display に display ごとの回転を足し、縦長で Notes を使える（ペンで書ける）ようにするための、compositor・入力・protocol・Settings・保存・試験の設計と、実装の段の分け方。
Prerequisites: 無し（調べは下の §1）。実装の段は WS213 p004（R・C・I の thread）と WS113（displays.conf・`kl_system_displays_*`）の main の統合済みの code を前提にする。

## 0. 要点（design-reviewer・Q1 向け）

- **どの GPU の driver も plane の回転を持たない**（i915 は Linux から写した `PLANE_CTL_ROTATE_*` の符号化はあるが `hw.rotation` は常に `MODE_ROTATE_0`、scanout は linear XRGB8888 だけ。HVS は flip の bit を読むだけ。venus は無し。UAPI・libvulkan に transform の field は無い）。Intel の 90/270 は Y-tiled の buffer が要り、今の scanout は linear。HVS は 90/270 を物理的に持たない。
- よって **compositor が回す**（案 A）。回転した出力では、scene を「論理の大きさ（幅と高さを入れ替えた）」の自分の画像（logical image）に描き、最後に 1 つの描画 pass（turn pass）で swapchain の画像（物理）へ回して写す。0° の出力は今の経路のまま（費用 0）。GPU だけで行う（2026-10-12 の規則）。Pi 4 の費用は 1 frame に 1 pass（damage の分だけ）。
- **論理と物理を分ける**: `server->width/height`（と head の `width/height`、plane の矩形、wl_output の configure・bounds、shell の配置の全部）は論理（回した後）、`compose->output.width/height`（swapchain・viewport・scissor・shot）は物理。今は同じ数なので、両者が出会う 5 か所（§3.3）だけに回転の写像を入れる。
- **入力**: 相対（mouse・touchpad）は変えない（利用者の見る向きの plane に対する動きなので）。絶対（touch・pen・QEMU の tablet）は「物理の画素へ scale → 逆回転で論理へ」。cursor の plane は C が論理→物理に写し、像と hotspot は M が像の変化の時だけ回す。
- **client**: wl_output の geometry に本当の transform、mode は物理（規格どおり）。libkeiland の `kl_window_output_mode` だけ transform で入れ替える（使うのは Settings の About だけ）。client は何も知らなくてよい。`set_buffer_transform` は今どおり 0 だけ受ける。
- **Settings と保存**: `kl_system_displays_v1` に `set_rotation` の request と `rotation` の event（version 20）、`struct kl_display` に `rotation`、Settings の Display の row に回転の選択（すぐ反映、Apply 不要）。`displays.conf` に `rotate=KEY 90`（version は 1 のまま。古い build は skip する）。
- **決めてほしい点** D1〜D6（§9）。特に D3: 今は touch・pen は anchor（主の出力）にしか写らないので、外付けのペン LCD を縦にして使うのは「その LCD が唯一の（または主の）出力」の時だけ。拡張で 2 台目の LCD に pen を結ぶのは別の仕事。

## 1. 調べた事実（source、file:line は agent/p4 28e5fe929 の tree）

### 1.1 compositor の出力の持ち方

| 事実 | 所在 |
| --- | --- |
| desktop は 1 つの plane。anchor（主の出力）の左上が原点、slot 0 が anchor、1+N が head N。`struct kwl_plane_rect {x,y,width,height}`、`KWL_PLANE_SLOTS 8` | `wayland/plane.h:12-43` |
| anchor の大きさは `server->width/height/refresh`（`kwl.h:1147-1150`）、swapchain は `compose->output`（`compose.h:361`、`.width/.height` は物理）。**anchor の swapchain は `server->width/height` で開く**ので今は論理＝物理 | `compose.c:307`（`vkdemo_display_open_on(..., server->width, server->height, ...)`） |
| head は `struct kwl_head`（`x,y,width,height,refresh`、`output`（swapchain）、`global`） | `compose.h:235-256` |
| `kwl_outputs()` が plane の矩形を作る（anchor は `{0,0,server->width,server->height}`、head は `head->x - output_x` …）。`kwl_output_rect()`・`kwl_output_at()` | `heads.c:1615-1690` |
| wl_output に送る値は `struct kwl_output_view {index,x,y,width,height,refresh}`、`kwl_output_view()` | `kwl.h:2004-2011`、`heads.c:713-752` |
| 描く時の head の view: `server->view_output/view_x/view_y/view_width/view_height`（`view_width` 0 で anchor） | `kwl.h:1290-1294` |
| scale・transform・rotation の field は無い。wl_output の scale は常に 1 | `protocol.c:699-704` |
| anchor の大きさの変更の経路: `output_resized()`（`kwl_glass_resize`、`kwl_glass_output_resized`（窓の refit と `kwl_arrange_output_resized`）、pointer の warp と clamp）→ `kwl_outputs_changed` | `output-switch.c:662-686`、`:257-259` |
| mode は `compose->display_mode`（`KWL_DISPLAYS_EXTENDED 0`・`MIRROR 1`）。off は `config.off[]` | `compose.h:577`、`displays.h:118-119,162-163` |

### 1.2 描画と scanout の経路

| 事実 | 所在 |
| --- | --- |
| anchor の surface は VK_KHR_display の plane surface、`transform = IDENTITY`、`imageExtent` = 幅×高さ。swapchain も `preTransform = IDENTITY`（識別子以外は `vkdemo_display_create_swapchain_exact` が断る） | `userland/tests/vkdemo/display.c:133-140, 286, 317-322` |
| IDENTITY を持たない display は anchor に選ばない | `compose.c:1345-1352` |
| render pass の `renderArea`・viewport・scissor は `compose->output.width/height`（物理）か damage の矩形 | `compose.c:2796-2822` |
| **投影の行列は無い**。quad ごとに CPU で NDC の矩形を計算して push constant に入れる: `compose_quad_part()` は `server->width/height`（head なら view）で `2x/width-1`、`glass_shape_draw()` も同じ。batch も同じ constants | `compose.c:2482-2523`、`glass.c:1905-1950`、`batch.c:156-233` |
| shader: `quad.vert:21` は `mix(rect.xy, rect.zw, corner)`、`panel.vert:50-54` は `pixel = (place*0.5+0.5)*screen.xy`（出力の画素で box・角の丸み・blur を計算） | `wayland/shaders/` |
| present は `vkQueuePresentKHR` に素の `VkPresentInfoKHR`（`VkPresentRegionsKHR` 無し）。head も同じ | `compose.c:3793-3815` |
| damage は desktop（＝anchor の画素）の座標で集め、`compose_region()` が buffer age の履歴を足して `compose->output.width/height` に clamp、`renderArea`・scissor・`compose->clips` になる | `damage.c:87-229, 634`、`compose.c:2537-2649, 2796-2834`、`compose-bind.c:110-232` |
| scene の写し（cursor 無しの絵）は出力の画像から同じ矩形で copy | `scene.c:114-150` |
| layer の絵は `compose->output.width/height` で作り、`server->width/height` と比べて stale を判定 | `layer-cache.c:101-102, 411, 591-592` |
| backdrop（glass の blur の元）は出力の 1/8 の画像、`kwl_backdrop_end` は `output.width/height` で pass を再開 | `backdrop.c:59-132` |
| 壁紙と blur の host image は `server->width/height` | `glass.c:766, 843, 1159` |
| mirror: anchor の**完成した swapchain の画像**を head へ `vkCmdBlitImage`（LINEAR）で fit（`kwl_displays_fit`）。`compose->readback` が無ければ黒 | `heads.c:1163-1223`、`displays.c:37` |
| extended の head: head の framebuffer に別の pass、viewport は head の大きさ、`server->view_*` を立てて描く | `heads.c:1235-1329` |
| game mode の直の scanout: `kwl_scanout_decide()` の facts に `size_matches`（client の buffer が `server->width/height` と同じ）。backend は `gpu_display_present`（src/dst・transform 無し） | `scanout-rules.c:27`、`scanout.c:185-190, 275-329`、`libkeiland-backend-zedbsd/scanout-zedbsd.c:144-196` |
| cursor の plane: `cursor-plane.c:122` `transform = IDENTITY`、C が `x = box.x - hotspot_x` を `VkDisplayPresentInfoKHR.dstRect` に（anchor の時だけ）。libvulkan は `GPU_DISPLAY_CURSOR_SET`（像）と `CURSOR_MOVE`（x,y）。像は M が host image の 2 枚の copy に全体を写す | `cursor-thread.c:221-225, 539-545`、`cursor-plane.c:753-802`、`libvulkan/wsi-display.c:2107` |
| WS214 の overlay の plane: 標準の VK_KHR_display + display_swapchain の `srcRect/dstRect` だけ（transform 無し）。compositor はまだ使わない（p005・p006 は未投入） | `libvulkan/wsi.c:1562-1615`、`wsi-swapchain.c:4642-4710` |
| shot（試験の撮影）は完成した swapchain の画像（物理）を copy | `shot.c:274-340`、`compose.c:2891` |

### 1.3 driver と UAPI の回転の能力

| platform | 事実 | 所在 |
| --- | --- | --- |
| UAPI | `gpu_display_present`・`gpu_display_plane_state`・`gpu_display_commit`・`gpu_display_overlay_info`・cursor の 3 struct のどれにも rotation/transform/flip の field は無い。`gpu_image_descriptor.tiling` は `GPU_IMAGE_LINEAR` だけ（他は `gpu.c:4683` が EOPNOTSUPP） | `include/uapi/gpu-display.h:189-424`、`gpu.h:36, 97-111` |
| i915 | `MODE_ROTATE_0/90/180/270`・`PLANE_CTL_ROTATE_*`・`i915_skl_plane_ctl_rotate()` は Linux から写してあるが、`hw.rotation` の代入は `MODE_ROTATE_0` の 2 か所だけ。scanout は XRGB8888・`FORMAT_MOD_LINEAR` だけ（`drv_i915_scanout_create` が他を断る）。cursor plane は未使用（`plane_count = 1`）。「A tiled buffer must revisit that decision」 | `intel/plane.h:121-139, 232-236`、`display/plane.c:293, 378, 777-803`、`display/scanout.c:137-139`、`display/internal.h:2525`、`display/display.c:223-245` |
| i915 の hardware の制約（Linux の i915 の規則の知識、**推測ではなく公知の仕様**: Gen9 以降、90/270 の plane の回転は Y-tiled（Gen12 では Tile-Y、Tile-4 は Gen12.5 以降）の framebuffer だけ。linear と X-tiled は 0/180 だけ。cursor plane は 180 だけ）。この tree にはその検査は写していない | — |
| bcm2711（Pi 4、HVS） | dlist の position word の bit 15・31（flip）を読んで firmware の plane を断るだけで、書く側は flip も rotation も無い。HVS は 90/270 を持たない（flip のみ） | `bcm2711/list.c:329-332`、`display-flip.c:1664-1708, 2234` |
| venus（QEMU） | cursor・commit・overlay の op は NULL、transform 無し | `venus/display.c:154-167, 803-806` |
| libvulkan | `supportedTransforms`/`currentTransform` は IDENTITY だけ（`wsi.c:125`、`wsi-display.c:584-585`）、surface と swapchain は IDENTITY 以外を `VK_ERROR_INITIALIZATION_FAILED`（`wsi.c:620-624`、`wsi-swapchain.c:1213-1215`） | — |
| Linux・FreeBSD の backend | KMS は libvulkan-compat の legacy `SETCRTC`/`PAGE_FLIP`（dumb buffer、atomic・`rotation` property 無し）、`supportedTransforms` IDENTITY | `libvulkan-compat/kms.c:412, 431`、`wsi-display.c:63, 412` |

### 1.4 入力の経路

| 事実 | 所在 |
| --- | --- |
| 絶対の device はどれも **anchor に写る**（device と出力の結び付けは無い）: QEMU の tablet（ABS_X/Y）は I thread の `kwl_cursor_box_place()` が `box->outputs[ANCHOR]` の幅と高さへ scale。touch screen は main の `read_report()` が `server->width/height` へ `scale_fixed`。pen は main の `place_pointer()` が同じ | `cursor-box.c:131-159`、`touch.c:964-969`、`tablet.c:1006-1028` |
| 相対（mouse・touchpad）は I thread が加速の後 `kwl_cursor_box_move()` → `kwl_plane_move()`（plane の座標、隣の出力へ渡る） | `input-thread.c:994-1014, 1085-1101`、`cursor-box.c:103-122`、`plane.c:50-114` |
| 受け箱（`struct kwl_cursor_box`）は出力の矩形の写し `outputs[]`・`output_count` を持ち、M が毎 pass `kwl_input_sync()` → `kwl_cursor_box_outputs()` で更新。warp は矩形と位置を 1 つの区間で置き換える | `cursor-box.h:112-139`、`input.c:484-508`、`cursor-box.c:170-225` |
| I は park しない（Vulkan を呼ばない）。park は R と C（`kwl_compose_park/unpark`、`kwl_cursor_thread_park`、`kwl_cursor_plane_hold`） | `plan/ws213/phase001/phase.md:329-337`、`compose.c:1002-1046`、`cursor-thread.c:313-351`、`cursor-plane.c:363-385` |
| surface の座標は plane の位置 − surface の位置（出力の項は無い）: pointer（`seat.c:593-594`）、wl_touch（`touch.c:2288-2289`）、tablet（`tablet.c:1149-1150`） | — |
| 端・角の gesture は `server->width/height`（anchor）で判定（`edge.c` は純、`corner.c:626-640`、`home.c:429`、`shell.c:7880`） | — |

### 1.5 client と protocol

| 事実 | 所在 |
| --- | --- |
| wl_output.geometry は 48 byte を memset 0 の上に x,y,make,model だけ書く → **transform は常に 0（normal）**、physical size 0 | `protocol.c:669-687` |
| `set_buffer_transform` は 0 以外、`set_buffer_scale` は 1 以外を EPROTO（`kwl_error` で client を切る） | `protocol.c:1036-1047`、`wire.c:292-326` |
| `wl_compositor` は version 4（`preferred_buffer_transform` は送らない）。xdg_output・fractional scale は無い。`wp_viewporter` はある | `protocol.c:55-85`、`viewport.c` |
| fullscreen の configure と `configure_bounds` は `server->width/height` | `protocol.c:1729-1731, 1838-1839` |
| libkeiland は最初の wl_output を v2 で bind し、geometry の transform を捨て、mode の幅・高さ・refresh だけ `kl_window_output_mode()` で返す。使うのは Settings の About の文字だけ。窓の大きさ・fullscreen・popup に出力の大きさは使わない | `libkeiland/ui/window.c:1895-1901, 2799-2871`、`settings/main.c:928-934` |
| xserver は wl_output を bind せず、画面は `--size` か 1280x800。GTK4 は upstream のまま（transform の patch 無し。upstream の GTK は wl_output の transform で monitor の geometry を入れ替えるが、自分の surface を pre-rotate しない）。libegl の display plane は IDENTITY 固定 | `xserver/wayland.c:1184-1189`、`packages/desktop/gtk4/patches/`、`libegl/vulkan.c:698-704` |
| Notes は窓の大きさから page を両軸で fit（縦長で問題無い）。toolbar は 1 行の card で、窓が狭いと `card_x = 8` に寄せるだけで折り返さない | `notes/geometry.c:343-376`、`notes/ui.c:270-276` |

### 1.6 Settings と保存

| 事実 | 所在 |
| --- | --- |
| `kl_system_displays_v1`（version 19 = `KL_SYSTEM_SINCE_SHOWN`）: request `destroy`・`apply(request, serial, mode, places)`・`set_brightness(request, key, percent)`・`set_shown(request, key, shown)`、event `output(key, label, x, y, width, height, refresh_mhz, flags, brightness)`・`done(serial, mode)`・`result(request, applied, saved)` | `libkeiland/system/kl-system-protocol.h:103-134, 534-551`、`system-protocol.c:506-528`、`displays-shell.c:341-466, 634-817` |
| 公開 API: `struct kl_display {key,label,x,y,width,height,refresh_mhz,flags,brightness}`、`kl_system_displays_get/mode/apply/set_brightness/set_shown`、`KL_VERSION 92` | `include/keiland/keiland.h:49, 1982-2042` |
| Settings の Display の頁は `page-display.c`（Extend/Mirror、配置の drag、row ごとの on/off switch、明るさ） | `settings/page-display.c:9-30, 35-60` |
| 保存は `~/.config/keiland/displays.conf`（`version=1`、`mode=`、`anchor=`、`brightness=`、`place=KEY X Y`、`off=KEY`）。**知らない key は「後の version の key」として skip** | `displays.h:90-106`、`displays.c:593-660`、`heads.c:57-61, 126-168, 1517` |
| 読み込みは出力を開いた後の `kwl_heads_config_load()`（compose.c:472）、KEY は driver の connector key（`zedbsd-port-v1:pci:…:edp:A`、D-ID A2） | `compose.c:465-480`、`i915/display/display.c:3326` |
| displays.c の parse/format の host 試験がある | `plan/ws113/tests/host-displays.{c,sh}` |

## 2. 用語と向きの約束

- **物理（physical）**: panel の画素の並び。swapchain・viewport・scanout・shot・driver の世界。`compose->output.width/height`、`head->output.width/height`。
- **論理（logical）**: 利用者が見る向きの desktop の画素。plane の矩形、`server->width/height`、`head->width/height`、窓・shell・gesture・wl_output の configure/bounds・Settings の配置の図。90°・270° では幅と高さが入れ替わる。
- **回転の値**: 利用者に見せる値は**時計回りの度数** 0・90・180・270（Q1 が ws.md に書いた形。「90°」＝画面の上辺が右へ行く）。protocol・displays.conf・compositor の内部もこの度数（`KL_DISPLAY_ROTATE_0/90/180/270` = 0,1,2,3）で持ち、**wl_output の transform に変換する 1 か所**（`output_events`）だけで Wayland の反時計回りの enum へ写す: 0→`NORMAL(0)`、90→`270(3)`、180→`180(2)`、270→`90(1)`。理由: 値の約束が 2 つある混乱を wire の 1 行に閉じ込める（D1 で確かめる）。
- **写像**（物理の幅 Wp・高さ Hp、論理の幅 Wl・高さ Hl、90/270 では Wl=Hp・Hl=Wp）。論理の点 (x,y) → 物理の点:
  - 0: (x, y)
  - 90（時計回り）: (Wp−1−y, x)
  - 180: (Wp−1−x, Hp−1−y)
  - 270: (y, Hp−1−x)
  - 矩形は 4 隅を写して正規化（軸に平行なまま）。逆写像（物理→論理、絶対の入力に使う）は 90 と 270 を入れ替えた式。これを**純粋な module `rotate.c`/`rotate.h`**（`kwl_rotate_point`・`kwl_rotate_point_back`・`kwl_rotate_rect`・`kwl_rotate_size`・`kwl_rotate_to_wayland`）に置き、host 試験で 4 つの向き×境界を確かめる（§7.1）。

## 3. 案と選択

| 案 | 内容 | 採否と理由 |
| --- | --- | --- |
| **A. 論理の画像 ＋ turn pass（採用）** | 回転した出力では scene を論理の大きさの自分の `VkImage`（logical image、COLOR_ATTACHMENT・SAMPLED・TRANSFER_SRC）に今どおり描き、最後に 1 pass で swapchain の画像へ回して写す（fragment shader が logical image を回した uv で sample）。0° は今の経路（費用 0） | 変更が compose.c の target と最後の pass、damage の写像、mirror の元、cursor の plane に閉じる。shader・batch・glass の計算（box・角・blur・影の向き）に触れない。全 platform（i915・Pi 4・QEMU・Linux・FreeBSD）で同じ。費用は回転した出力だけ 1 pass |
| B. NDC の関門（`compose_quad_part`・`glass_shape_draw`・batch）で quad を回す | 矩形と uv を 90° 回して push constant に入れ、`screen` を入れ替える | 不採用: `panel.vert` の `pixel` と frag の box・角の丸み・blur の方向（`color.xy` の step）・影・gradient が出力の画素の向きに依存し、全 shader と全 primitive を直すことになる。画像の uv の 90° は `mix(rect.xy, rect.zw, corner)` では表せず shader の変更が要る。回帰の範囲が広く、Pi 4 の節約（1 pass）に見合わない |
| C. hardware の plane の回転（i915 の `PLANE_CTL_ROTATE`） | UAPI（`gpu_display_present`/`plane_state` に transform）、libvulkan の `supportedTransforms`、compositor が `preTransform` を使う | 今は不採用、Future Work: i915 の 90/270 は Y-tiled の scanout が要り（今は linear だけ、`internal.h:2525`）、render 側の tiled の出力と GTT の rotated mapping（Linux の DPT 相当）の実装が要る。Pi 4（HVS）は 90/270 を持たず、venus も無いので、速さが要る Pi 4 を助けない。180 だけなら i915 linear と HVS の flip で可能だが、別の経路を増やす価値が無い。設計は A の論理/物理の分離がそのまま C の前提になる（C は「turn pass を plane に任せる」置き換え） |
| D. client に pre-rotate させる（`set_buffer_transform` を受け、buffer をそのまま出す） | Wayland の本来の最適化 | 不採用（今は）: この tree の client は誰も transform を使わず、direct scanout は game mode だけ。`set_buffer_transform` は今どおり 0 だけ受ける（0 以外は EPROTO のまま。規格では受けるべきだが現状の制限を保つ。GTK4 は使わない） |

選択の根拠: 利用者の目的（縦長で Notes、ペン）は A で全 platform に届く。速さの条件（ws.md の 5）は「plane の回転が使えればそれを、使えなければ compositor」で、使える platform が 1 つも無い（§1.3）。

## 4. compositor の設計

### 4.1 状態と所有

- `struct kwl_compose` に anchor の `unsigned rotation`（度数の enum）、`struct kwl_head` に `unsigned rotation`。所有は M（main）、変更は **park の中だけ**（§4.6）。
- `server->width/height` は論理。anchor の物理は `compose->output.width/height`（swapchain の大きさ、display の mode）。head の `width/height` は論理、`head->output.width/height` は物理。`kwl_output_view()`（wl_output・Settings の snapshot）は論理の幅・高さ（mode の event だけ物理、§4.7）。
- 論理の画像（`struct kwl_turn { VkImage image; VkDeviceMemory memory; VkImageView view; VkFramebuffer framebuffers[...]; VkDescriptorSet set; uint32_t width, height; unsigned rotation; }`）を anchor（`compose->turn`）と head ごと（`head->turn`）に 1 つ。回転 0 では `image == VK_NULL_HANDLE`。寿命は出力の open/close と回転の変更（park の中で作る・壊す）。R（描画 thread）は record された command を再生するだけなので、持ち主は M のまま（WS213 の 4.7: GPU の object の破棄は R の step 1 に頼む規則に従う）。

### 4.2 描く経路（anchor、回転 ≠ 0）

1. `compose_targets`（compose.c:1987）: framebuffer を swapchain の画像ではなく `turn.image` の view に作る（論理の大きさ）。pass の `renderArea`・viewport・scissor・`pass_area`・`scissor_now`・`compose->clips` は**論理**（今 `compose->output.width/height` を読む 5 か所 `compose.c:2600-2607, 2637, 2796-2822` を「target の大きさ」を返す helper `compose_target_size()` に置き換える。0° では物理＝論理なので挙動不変）。
2. scene・backdrop・layers・glass の host image は論理の大きさ（`scene` `compose.c:455-459`、`backdrop.c:71, 132`、`layer-cache.c:101-102`、`glass.c:766, 843, 1159`）。これらは `server->width/height` か `compose_target_size()` を読むようにし、物理を読まない。scene の写しの元は `turn.image`。
3. **turn pass**: scene の pass（と cursor の合成）の後、swapchain の画像を attachment にした pass で、`turn.image` を sample する全画面の quad を描く。新しい pipeline `turn`（vertex: 4 隅の位置と uv を push constant で受ける（`quad.vert` の `mix` では uv の 90° を表せないので、uv の 4 隅を別に渡す小さな `turn.vert`）、fragment: `texture(image, uv)` だけ）。描く矩形は**物理の damage**（§4.3）に限り、scissor も物理。shot（`shot.c`）は今どおり完成した swapchain の画像（物理、回した絵）を写す（試験は「物理に見える絵」を見る。§7）。
4. R の record の列: `compose_record` の最後に turn pass を足す。cursor の合成（`compose_cursor`）は scene の pass の中（論理）で今どおり。`kwl_shot_record` は turn の後。
5. 0° の出力は 1〜4 を通らない（turn は無く、framebuffer は swapchain の画像）。

### 4.3 damage と buffer age

- damage は今どおり**論理**で集める（`damage.c`、client の damage は窓の位置を足した論理の矩形）。
- 論理の画像は frame をまたいで残る（swapchain のように巡らない）ので、scene の pass に要るのは「今回の damage」だけだが、第 1 版では `compose_region()` の age の union をそのまま使う（描き過ぎるが正しい。最適化は後）。
- turn pass の矩形は、`compose_region()` の出した論理の矩形（age の union 込み）を `kwl_rotate_rect` で物理に写した物。swapchain の画像の age の意味は今と同じ（その画像に最後に描いてからの frame 数）なので、`compose_region` の帳簿（`image_frames`）はそのまま turn の対象の画像に対して使う。
- `kwl_compose_clip_visible/draw`（compose-bind.c）は論理のまま。

### 4.4 mirror と extended の head

- **mirror**: `heads_record_mirror` の元を「anchor の完成した swapchain の画像」から「anchor の scene の画像（回転 0 なら swapchain の画像、回転ありなら `turn.image`）」に変える。こうすると縦長の anchor を横長の head に写す時、絵は**正立**で左右に黒い帯（利用者が「同じ絵」と見る形。回した swapchain を写すと横倒しになる）。head 自身が回転していれば、head の `turn.image` に fit で blit してから head の turn pass（§4.2 と同じ）。`compose->readback` の条件（TRANSFER_SRC）は `turn.image` には要らない（自分の画像は常に TRANSFER_SRC で作る）。
- **extended**: head の論理の矩形は回転で幅と高さが入れ替わる。`heads_record_extended` は head の framebuffer（回転ありなら head の `turn.image`）に描き、`server->view_width/height` は論理。head の turn pass を足す。
- 配置の検査（`kwl_displays_validate`: 重ならない・辺で連結）は論理の矩形で行う。anchor や head の回転で配置が無効になったら、`heads_place` の既存の fallback（右端の右へ）で**その出力だけ**置き直し、snapshot を送る（Settings の図が追う）。保存の `place=` はその結果。

### 4.5 直の scanout・overlay・cursor の plane

- **game mode の直の scanout**: `kwl_scanout_facts` に `rotated` を足し、規則は回転した出力では scanout しない（client の buffer は論理の向きで、driver は回せない）。今でも `size_matches`（`server->width/height` と比べる）が物理と違って偽になるが、理由を log に出すために明示の fact にする。180° も同じ（driver の flip の経路を作らない、§3 C）。
- **WS214 の overlay の plane**（p005・p006 は未投入）: HVS は回せないので、回転した出力では窓を plane に載せない（compositor の判定に `rotation != 0` → fallback）。p005 の設計に「回転した出力は対象外」を 1 行足す（foreign Phase への連絡は Q1）。
- **cursor の plane**（i915 は plane_count 1 で未使用、Pi 4 と venus… venus は cursor 無し。実際に使うのは Pi 4 だけ）:
  - 像: M が `plane_publish` で受け箱に書く 2 枚の copy は、像が変わった時に M が host image へ全体を写す（`cursor-plane.c:753-802`）。その写しの時に**回して写す**（64×64 以下、像の変化の時だけ、2026-10-12 の規則の「内容が変わった時の一度だけの用意」に当たる。毎 frame ではない）。hotspot も回す（`kwl_rotate_point` の像の中での写像）。
  - 位置: C は `snapshot.x - hotspot_x`（論理）を `dstRect` に入れる（`cursor-thread.c:221-225`）。受け箱に `rotation` と anchor の**物理**の大きさを足し（M が `kwl_cursor_box_outputs` の拡張で、矩形と同じ区間で書く）、C が論理の左上を `kwl_rotate_point` で物理に写す（像の大きさは正方形の extent なので左上の写像は「回した像の左上」＝論理の点を写した後に extent 分を引く向きを場合分け。`rotate.c` に `kwl_rotate_cursor(x, y, extent, rotation, Wp, Hp, &px, &py)` として式を 1 つにし host 試験で確かめる）。
  - 回転の変更は park の中（C は parked）なので、C が古い rotation で present することは無い。

### 4.6 回転の変更（実行中）

`kwl_output_rotate(server, slot, rotation)`（新、output-switch.c か heads.c）:

1. 変更が無ければ 0。anchor の時:
2. `kwl_compose_park`（R と C が止まる。§1.4）。`kwl_cursor_plane_hold` も（WS213 4.8 のとおり park で両方）。
3. `compose->rotation = rotation`、`server->width/height` を `kwl_rotate_size(物理)` に。swapchain・display の mode は**触らない**（modeset 無し、画面は消えない）。`turn` を作り直す（古い物は `kwl_compose_forget` の後に R の step 1 で破棄）。`compose_targets` の framebuffer を作り直す。scene・backdrop・layers を論理の大きさで作り直す（`kwl_layers_open/close` 相当、`output_resized` に既にある `kwl_glass_resize`）。
4. `output_resized(server)`（既存: 壁紙・窓の refit・`kwl_arrange_output_resized`・pointer の warp と clamp）。pointer の warp は「同じ物理の点」に留める: 論理の古い位置 → 物理 → 新しい論理（`rotate.c` の 2 回の写像）。cursor は利用者の指の下に残る。
5. `kwl_input_sync`（受け箱の矩形・rotation・物理の大きさを 1 区間で更新）。
6. `kwl_compose_unpark`。`kwl_outputs_changed`（wl_output の geometry/mode/done を全 client に再送、fullscreen の窓は configure が新しい大きさで届く）、`kwl_displays_tell`（Settings の snapshot）、`heads_save`（displays.conf）。
7. head の時: 2・3 を head の `turn`・`width/height` に、配置の再検査（§4.4）、6。
8. 失敗: `turn` の作成（memory）に失敗したら rotation を元に戻して unpark し `KL_SYSTEM_RESULT_UNSUPPORTED` を返す。park の失敗は既存の `KWL FAILED site=park` と `_exit`。
9. log: `KWL OUTPUT rotate key=… rotation=… width=… height=…`（試験が読む）。

起動時: `kwl_heads_config_load`（compose.c:472、出力の open の中＝park の中）が `rotate=` を読んだら、その場で 3 を行う（`output_resized` は窓が無いので軽い）。anchor の出力の切り替え（`kwl_output_switch`）と hotplug の再 open では、新しい display の key の `rotate=` を適用する（`kwl_compose_output_open` の後、同じ経路）。`--width/--height` が与えられた時も物理＝その大きさ、論理はその回転。

### 4.7 wl_output と client

- `output_events`（protocol.c:651）: geometry の transform（byte 44）に `kwl_rotate_to_wayland(rotation)`、mode の幅・高さは**物理**（規格: mode は transform の前の大きさ）、x/y は論理の plane。description は論理（「Display 1080x1920」）。
- libkeiland `window_output_geometry`（ui/window.c:2799）が transform を保持し、`kl_window_output_mode()` は 90/270 で幅と高さを入れ替えて返す（About の文字が論理になる）。ABI は変えない（内部の field を足すだけ）。
- fullscreen の configure・bounds（protocol.c:1729, 1838）は `server->width/height`（論理）のまま。xserver（固定の大きさ、wl_output を見ない）・GTK4（transform で geometry を入れ替える upstream の挙動）・libegl・Vulkan の WSI（Wayland の surface は identity のまま）は変更無し。
- `set_buffer_transform`: 0 だけ（不変）。`preferred_buffer_transform` は送らない（wl_compositor v4 のまま）。

### 4.8 Linux・FreeBSD

backend は変えない（libvulkan-compat の VK_KHR_display は identity だけ。compositor の経路 A は Vulkan の標準 API だけ）。WS131 の境界（compositor に OS の header を入れない）に触れない。

## 5. 入力の設計

- **相対（mouse・touchpad）**: 変更無し。plane は利用者の見る向きなので、机の上で右へ動かせば画面の右へ動く。touchpad の端の gesture は pad の座標（不変）。
- **絶対**（§1.4 の 3 経路）: 「生の値 → anchor の**物理**の画素へ scale → `kwl_rotate_point_back` で論理へ」。
  - I thread の `kwl_cursor_box_place()`: `box->outputs[ANCHOR]`（論理）に加えて受け箱の `anchor_physical{width,height}` と `rotation` を使う（M が §4.5 の区間で書く）。
  - main の `touch.c:964-969`（`read_report`）と `tablet.c:1014-1015`（`place_pointer`）: `server->width/height` の代わりに compose の物理の大きさと rotation を読む helper `kwl_output_absolute(server, raw_x, min, max, raw_y, …, &x, &y)`（kwl.h、heads.c）を呼ぶ。24.8 固定小数の精度は保つ（物理で scale してから回す。回した後に 256 倍の単位のまま）。
  - 画面の端の判定（`touch.c:1020-1021` の keyboard の zone、edge の group）は論理のまま（自動で新しい端に従う）。
- **device と出力の結び付け**は今無い（全部 anchor）。縦にした外付けのペン LCD を拡張の 2 台目として使う場合は、その device の座標を head の矩形と head の回転で写す仕組み（device の名前か大きさで出力を選ぶ、Settings の Input で結ぶ）が別に要る（D3）。この WS では「ペン LCD が唯一の出力（または `kwl_output_switch` で anchor）」を対象にする。
- WS213 の不変条件（I は park しない、受け箱は 1 区間で更新）を保つ。rotation と物理の大きさと矩形を同じ `kwl_cursor_box_outputs` の区間で書くので、I が「新しい矩形と古い rotation」を見ることは無い。

## 6. protocol・libkeiland・Settings・保存

### 6.1 `kl_system_displays_v1` version 20（`KL_SYSTEM_SINCE_ROTATION`）

| 種類 | opcode | 名前と引数 | 意味 |
| --- | --- | --- | --- |
| request | 4 | `set_rotation(uint request, string key, uint rotation)` | rotation は 0..3（0°・90°・180°・270°、時計回り）。すぐ適用（Apply 不要、`set_shown` と同じ形）。result は `applied`・`saved` を別に（既存の `result` event）。active な session でなければ DENIED、key が無ければ INVALID、4 以上は INVALID、`turn` の作成に失敗したら UNSUPPORTED |
| event | 3 | `rotation(string key, uint rotation)` | snapshot の `output` の後、`done` の前に出力ごとに送る（version 20 以上の client にだけ）。既存の `output` の引数は変えない（wire の signature を保つ） |

`kl-system-protocol.h`・`system-protocol.c`（signature `"set_rotation" "usu"`、`"rotation" "su"`）、`displays-shell.c`（`displays_rotation_request`、snapshot で `displays_rotation` を送る）、`KL_SYSTEM_MANAGER_VERSION` は 33 のまま（displays の object の version だけ上げる）。

### 6.2 libkeiland の公開 API（KL_VERSION の次の番号、Q1 が割り当て。master の「次の KL_VERSION は 93」）

- `struct kl_display` に `unsigned rotation;` を**末尾**に足す（配列の要素の大きさが変わるので `KL_VERSION` を上げる。`kl_system_displays_get` の capacity の意味は同じ）。
- `int kl_system_displays_set_rotation(struct kl_system *system, const char *key, unsigned rotation, uint32_t *request);`（manager の displays の version が 20 未満なら ENOTSUP、`set_shown` と同じ形）。
- `KL_DISPLAY_ROTATE_0/90/180/270` の定数（0..3）。
- `kl_window_output_mode()` の入れ替え（§4.7）。

### 6.3 Settings の Display の頁（`page-display.c`）

- 出力の row（`display_rows`）に回転の選択を足す: 「Rotation」の 4 つの小さな button（0°・90°・180°・270°、選択中を強調）。押すとすぐ `kl_system_displays_set_rotation`（draft ではない。Apply/Revert は配置と mode のまま）。request 中は row を無効（`display->request` の既存の形）。
- 配置の図（`display_arrangement`・`display_card`）の card は snapshot の `width/height`（論理、回転で入れ替わる）で描くので自動で縦長になる。card の中に回転の印（上辺の向きの小さな三角）を 1 つ。
- 結果の文字: 適用できたが保存できなかった（`saved == 0`）は既存の message の形。UNSUPPORTED は「This display cannot be rotated」。
- 1 台でも使える（mode の二択と違い、1 台の時にこそ使う）。
- Linux/FreeBSD の build でも同じ UI（compositor が同じ経路で回す）。

### 6.4 保存（displays.conf）

- 行 `rotate=KEY N`（N は度数 0/90/180/270。人が読んで分かる形。key に空白があり得るので X Y と同じく最後の語が値）。`version=1` のまま（§1.6: 知らない key は skip されるので、古い build で開いても壊れない）。
- `struct kwl_display_config` に `rotations[KWL_DISPLAYS_PLACES]{key, rotation}` と count。`kwl_displays_parse/format` に parse と出力、`kwl_displays_rotation(config, key)`・`kwl_displays_set_rotation(config, key, rotation)`。0 は書かない（行が無い＝0）。
- 保存と適用は別（`applied`・`saved`）。保存の失敗は Settings に伝える（既存）。
- 切断した display の `rotate=` は保持（`place=` と同じ方針: 一時的な抜き差しで消さない）。

## 7. 速さと費用

- 回転 0 の出力: 変更無し（framebuffer は swapchain の画像のまま、turn pass 無し）。helper の分岐が増えるだけ。
- 回転あり: 1 frame に turn pass 1 つ（damage の分の画素を 1 回読み 1 回書く）。全画面の時: 1024x600（OSC Tokyo の demo の LCD の既定）で 2.4 MB の読み＋2.4 MB の書き、60 fps で約 0.3 GB/s。1920x1080 で約 1 GB/s（Pi 4 の帯域の目安 ~4 GB/s、HVS の scanout と合わせても余裕はあるが、WS214 の帯域の予算（1.5 GB/s 仮）と一緒に Pi で測る）。普通の frame は damage の分だけ。
- 5330（i915）: 無視できる。
- 計測: 既存の `KWL PERF pass` の行に turn の時間を足す（`kwl_compose_count_*` の計数に pass の種類）。Pi 4 で回転あり/無しの App Home の開閉・窓の drag の frame の時間を比べる（p006）。
- GPU だけで行う（CPU は cursor の像の回転（像の変化の時だけ）以外に画素に触れない）。

## 8. 失敗と回復、並行性

| 場面 | 扱い |
| --- | --- |
| `turn` の画像の memory が取れない | rotation を戻し UNSUPPORTED。log `KWL OUTPUT rotate refused errno=` |
| park の時間切れ | 既存（`KWL FAILED site=park`、`_exit`）。回転は出力の open/close と同じ頻度の稀な操作 |
| displays.conf が読めない・`rotate=` の値が不正 | 無視して 0（`place=` と同じ）。log `KWL DISPLAYS config` |
| 回転中に hotplug・output switch | 回転の変更は park の中で原子的。切り替え後の open で新しい key の `rotate=` を適用（§4.6） |
| fullscreen の client が古い大きさの buffer を出し続ける | `kwl_outputs_changed` と configure で新しい大きさを伝える。届くまでは窓は論理の中に clip（既存の挙動） |
| 試験の shot | 物理の絵（回した後）。scenario は「bar が物理の右端にある」等、回した位置で確かめる |
| thread | rotation・論理の大きさ・`turn` は M が park の中で変える。受け箱の rotation・物理の大きさ・矩形は 1 区間で書き I が読む。C は snapshot で読む。R は record を再生するだけ。lock の追加は無し（受け箱の既存の lock） |

## 9. 決めてほしい点（推奨つき）

| ID | 問い | 推奨 |
| --- | --- | --- |
| D1 | 回転の値の見せ方と約束。(a) 「0°・90°・180°・270°」（時計回り）、(b) GNOME 風「Landscape / Portrait right / Portrait left / Landscape (flipped)」 | **(a)**（ws.md の書き方に合う。card の上辺の印で向きが分かる）。内部と file も時計回りの度数、Wayland へは 1 か所で変換 |
| D2 | hardware の plane の回転（案 C）を今やるか | **やらない**。どの driver にも無く、Pi 4 の HVS は 90/270 を持たない。Future Work に「i915 の Y-tiled scanout と plane の回転」を置く |
| D3 | 外付けのペン LCD を**拡張の 2 台目**として縦にし、pen をその LCD に写す（device と出力の結び付け）までこの WS でやるか | **この WS は anchor（唯一の出力、または主の出力）だけ**。結び付けは別の WS（Settings の Input で device を出力に結ぶ）。利用者の「縦長で Notes」は、LCD を唯一の出力（WS079 の「HDMI を主な出力」）にすれば届く |
| D4 | 回転の適用の形: すぐ反映（推奨）か、配置と同じ Apply/Revert か。元に戻す確認（15 秒の「この設定を保つ？」）を出すか | **すぐ反映、確認は無し**（同じ row で戻せる。180° で上下が逆でも touch の写像が追うので操作できる） |
| D5 | 1024x600 の LCD を縦にすると幅 600 px。Notes の toolbar（1 行の card）が収まるか未確認（`ui.c:270-276` は折り返さない）。収まらなければ WS079 で折り返し（2 行）か縮小を入れるか | p006 の QEMU（`--width=600 --height=1024` の Notes）で先に測り、収まらなければ **WS079 に小さな Phase**（この WS の受け入れの条件 2 の「縦長で Notes」に要る） |
| D6 | 回転した出力で game mode（直の scanout）を合成に落とすことの可否 | **可**（driver が回せないので他に無い。180° も同じ） |

## 10. 敵対的レビュー（自分で）

| 懸念 | 評価と対処 |
| --- | --- |
| 論理の画像が frame をまたいで残るのに buffer age の union で描き過ぎる | 正しさは保つ。費用は回転時だけ。最適化（scene の pass は今回の damage だけ）は p002 の後の版で計測してから |
| `compose->output.width/height` を読む場所の取りこぼし（物理を論理と混ぜる） | §1.2 の表の 5 か所 ＋ scene・backdrop・layers・glass・scanout・heads（§1.1 3.16）。p002 の受け入れに `grep -n 'output\.width\|output\.height' userland/desktop/wayland/*.c` の全件を「物理が正しい」と注記付きで表にすることを入れる |
| shot が物理の絵なので既存の T1 の試験の比較が回転の試験で使えない | 回転の scenario は物理の位置で確かめる（bar の位置、`KWL OUTPUT rotate` の行、Settings の log）。0° の試験は不変 |
| I thread が回転の途中の値を見る | rotation・物理の大きさ・矩形を 1 区間で書く（§5）。warp の番号で古い motion は捨てられる（`apply_motion`、input.c:1461-1508） |
| 回転の変更で pointer が飛ぶ | 同じ物理の点に warp（§4.6 の 4） |
| mirror で回した anchor を head に写すと横倒し | 元を scene の画像にする（§4.4）。review で「同じ絵」の意味（正立）を確かめてほしい |
| GTK4 が transform を見て geometry を入れ替え、mode（物理）と合わない | 規格どおり（mode は物理、transform で入れ替える）なので GTK は正しく論理を得る。libkeiland も同じ規則に直す |
| `set_buffer_transform` を 0 以外で切る現状は規格違反 | 不変（この WS の範囲外。Future Work に「受けて compositor が解く」を置く） |
| cursor の plane の像を CPU で回す | 64×64 以下、像の変化の時だけ。2026-10-12 の規則の例外（一度だけの用意）に当たる。毎 frame の経路は GPU |
| 回転した出力で WS214 の plane が使えないと Pi 4 の drag が 60fps にならない | 縦長の Pi 4 の demo は想定外（ws.md は i915 を主、Pi 4 は「動くか記録」）。WS214 p005 に 1 行の制約を足す |
| head の回転で配置が無効になる | その出力だけ既存の fallback で置き直し snapshot を送る（§4.4）。Settings の drag の draft は stale で取り直し（既存の STALE） |
| Notes の toolbar が 600 px に収まらない | D5 |
| i915 の plane 回転の定義（Linux からの写し）を使わない設計で、転記の疑いは増えない | 新しい code は全て Zlib の自作。参照は Vulkan の仕様と Wayland の protocol の文書だけ。i915 の既存の defines の出典（SHA）は `intel/plane.h:77-80` のまま |
| HAL・UAPI | 変更無し（案 A）。案 C を選んだ時だけ UAPI の差分の承認が要る |

## 11. 試験の計画

### 11.1 host（実装の担当）

- `plan/ws217/tests/host-rotate.{c,sh}`: `rotate.c` の点・矩形・大きさ・cursor の左上・Wayland の enum、4 つの向き × 境界（0・端−1・端）、往復（back(forward(p)) == p）、ASan/UBSan。
- `plan/ws113/tests/host-displays.c` を拡張（`rotate=` の parse と format、知らない行の skip、0 は書かない）。
- `plan/ws213/tests/cursor-due-host-test.sh` の fake を使って `kwl_cursor_box_place` の回転（受け箱の rotation と物理の大きさ）。
- `plan/tools/settings/host-kl-settings.c` の形で `kl_system_displays_set_rotation` の marshal と version 20 未満の ENOTSUP。
- build: amd64（`$(BUILD)/dynamic/zdesktop` 等の named target）と rpi4 の compositor、warning 0。`plan/tools/keiland-os-boundary/check.sh`。

### 11.2 T1（QEMU、virtio-gpu/Venus、1 image）

AAT の scenario を `tests/scenarios/desktop/displays/rotate.md` と `tests/scenarios/apps/notes/portrait.md` に足す（T1 の依頼は Q1 経由）:

1. Settings → Display → 90° を押す → `KWL OUTPUT rotate key= rotation=1 width=800 height=1280`（1280x800 の virtio の例）、`ZSETTINGS DISPLAY rotation …`、screendump は 1280x800 のままで system bar が物理の右端に縦に出る。
2. 注入の touch（`touchinject`、物理の座標）で App Home の tile を押し、論理の位置に写ることを log（`KWL TOUCH …` の論理の座標）で確かめる。
3. Notes を開く（縦長で page が fit、toolbar が収まる（D5））。注入の pen（`peninject`）で物理の点に書き、`KWL TABLET` の論理の座標と Notes の stroke の位置。
4. 180°・270°・0° と戻す。
5. 再起動（session の login し直し）で `displays.conf` の `rotate=` が残り、起動の最初の frame から縦長（`KWL DISPLAYS config` と `KWL OUTPUT rotate`）。
6. 回帰: 0° の既存の smoke suite が変わらない（shot の比較）。

### 11.3 実機（5330、ユーザー／T1 の SSH）

- 5330 の内蔵 panel（eDP）で 90° → 縦長の desktop、mouse・touchpad の向き、Notes。写真（ユーザー）。
- ペン LCD を HDMI の唯一の出力にして 90°（LCD を物理的に縦置き）→ pen で Notes に書く（WS079 L3 の実機と同じ機材。ユーザー）。
- 実機の証拠は QEMU と分けて記録。

### 11.4 Pi 4（T1・Q1 経由、焼き直しの後）

- 1024x600 の LCD で 90°。`KWL PERF pass` の turn の時間、App Home の開閉と窓の drag の frame の時間（回転あり/無し）。underrun の計数（WS214 の `hvs-load` の見方）。

## 12. 実装の段（Phase）と受け入れ

| Phase | 内容 | 受け入れ | 依存 |
| --- | --- | --- | --- |
| p002 | compositor の中核（anchor）: `rotate.c`、論理/物理の分離（`compose_target_size`）、`turn`（画像・pipeline・shader・pass）、damage の写像、scene/backdrop/layers/glass の大きさ、`kwl_output_rotate`（park の中）、`displays.conf` の `rotate=`（parse/format/適用）、wl_output の transform と物理の mode、scanout の `rotated` fact、試験用の `--rotate=N` の起動 option と `KWL OUTPUT rotate` の行 | host 試験 11.1 の 1・2 PASS、amd64・rpi4 の build warning 0、`output.width/height` の全件の表（§10）、T1 の 11.2 の 1・4・5・6（test-wait） | WS213 p004 の main の統合（済み） |
| p003 | 入力と cursor の plane: 絶対の 3 経路（受け箱の `kwl_cursor_box_place`、touch、tablet）の物理→論理、受け箱の rotation と物理の大きさ、C の位置の写像、M の像と hotspot の回転、pointer の warp（同じ物理の点） | host 試験 11.1 の 3 PASS、T1 の 11.2 の 2・3 | p002 |
| p004 | protocol・libkeiland・Settings: `kl_system_displays_v1` v20（`set_rotation`・`rotation`）、`struct kl_display.rotation`・`kl_system_displays_set_rotation`・`KL_VERSION`（Q1 が番号）、`kl_window_output_mode` の入れ替え、`page-display.c` の row の回転の button と card の印、結果の文字 | host 試験 11.1 の 4、Settings の named build（amd64・Linux・FreeBSD の Makefile 3 つ）、T1 の 11.2 の 1（Settings から） | p002（p003 と並行可） |
| p005 | head の回転（extended の head の `turn` と配置の再検査、mirror の元を scene の画像に、回転した head への mirror） | T1 の QEMU（2 台目の virtio の出力、WS113 の p007 の試験の image）で head の 90°・mirror の正立、host 試験に配置の再検査 | p002、p004 |
| p006 | 確かめ: T1 の scenario の全部、5330（ユーザー）、Pi 4 の計測、D5 の Notes の幅の確認（要れば WS079 へ） | 11.2〜11.4 の証拠、ws.md の完了の条件 1〜5 の判定（Pi 4 は「動く／動かない所」の記録でよい） | p002〜p005 |
| p007 | 規約の全文の見直し（`plan/coding-style.md`、`style-check.py`、boundary の checker） | 違反 0、build warning 0 | p002〜p005 |

- 1 Queue は 1 Phase。p002 が大きい（compose.c の target の抽象化と turn pass、displays.conf）ので、Q1 の判断で p002a（`rotate.c`・displays.conf・wl_output・`--rotate`、turn 無しで「論理の大きさだけ入れ替わる」所まで）と p002b（`turn` と damage の写像、scene/backdrop/layers）に割ってよい。p002a の段では回転した出力は正しく映らない（論理の絵を物理に素で出す）ので、p002a 単独では T1 に出さない。
- 変更する file の見込み: `wayland/rotate.{c,h}`（新）、`compose.{c,h}`、`backdrop.c`、`scene.c`、`layer-cache.c`、`glass.c`、`heads.c`、`displays.{c,h}`、`displays-shell.c`、`output-switch.c`、`protocol.c`、`scanout.c`・`scanout-rules.{c,h}`、`cursor-box.{c,h}`、`cursor-thread.c`、`cursor-plane.c`、`touch.c`、`tablet.c`、`input.c`、`kwl.h`、`main.c`（`--rotate`）、`shaders/turn.{vert,frag}` と `shaders.h`、`Makefile` 3 つ、`libkeiland/system/{kl-system-protocol.h,system-protocol.c,system.c}`、`libkeiland/ui/window.c`、`include/keiland/keiland.h`、`settings/page-display.c`。HAL・UAPI・driver・backend・libvulkan は変えない。

## 13. 選ばなかった事と Future Work（Q1 が future-work.md へ）

- hardware の plane の回転（案 C）: i915 の Y-tiled scanout（render の tiled の出力、GTT の rotated mapping）と UAPI の transform、libvulkan の `supportedTransforms`。再検討の契機: i915 で turn pass の費用が問題になった時、または 180° の省電力（panel self refresh と合わせて）が要る時。
- device と出力の結び付け（touch・pen を head に写す、D3）。契機: 外付けのペン LCD を 2 台目として使う指示。
- `set_buffer_transform` の 0 以外の受け入れ（compositor が解く）。契機: pre-rotate する client（GTK4 以外）の移植。
- 加速度 sensor による自動の回転（driver が無い）。
- Notes の toolbar の折り返し（D5、WS079）。

## 14. ライセンスと転記

- 新しい code は全て Zlib（`Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib`）。この設計で参照したのは Wayland の `wl_output.transform` の意味（protocol の XML、MIT）と Vulkan の `VkSurfaceTransformFlagBitsKHR`（仕様）だけで、code の転記は無い。
- i915 の `MODE_ROTATE_*`・`PLANE_CTL_ROTATE_*`（Linux の GPL の header から写した定数、出典の SHA は `intel/plane.h:77-80`）はこの設計では使わない。案 C を選ぶ時は WS141/WS214 と同じ「値は事実、名前は独自、監査」の規則の対象になるか Q1 がユーザーに確かめる（i915 は Guardrail の GPL の参照の規則の対象の WS に入っていない）。
- weston・mutter・sway の回転の実装は読んでいない（設計は自分の tree の事実から）。

## Event

2026-10-13 / ws217-p001-design-v1: P4 が第 1 版を書いた（調べは Explore の 5 つの調査と P4 の確認、source は agent/p4 28e5fe929）。design-reviewer・Q1 の確かめと D1〜D6 のユーザーの判断待ち。コードは書いていない。
