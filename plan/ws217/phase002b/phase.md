<!-- awesome-plan project=zedbsd record=ws217-p002b -->

# ws217-p002b: compositor の turn pass（90°・270° と試験の 180°）と実行中の回転の経路

Parent: [WS217](../ws.md)
Status: in-progress（2026-10-13 P1 q948: 第 1 版の設計で実装し、[review-1](review-1.md)（条件つきで進めてよい、致命的 0・重大 1・中 7・軽微 13）の指摘と Q1 の決めを §12 で反映。build 4 つと host 試験 PASS。test-wait: T1 の QEMU（§11.4 の依頼）。p002a は cleared（T1-576））
Disposition: normal
Primary Milestone: MG006（WS から継承）
Queue / attempts: q948（2026-10-13 Q1 の依頼、P1）。承認: ユーザーの選択（2026-10-13 AskUserQuestion「WS217 p002b 画面の 90° 回転」、目的は縦長で Notes を使う）と WS217 の D1〜D18 の決定（ws.md）、p001 の review-1〜4
Purpose / goal: p001 第 4 版 §13 の p002b の行。turn pass を入れ、論理の大きさの画像に desktop を描いて 1 pass で swapchain へ回す。open の経路（`--rotate`・`rotate=`）と実行中の経路（利用者の選択、review-4 S12 の 2 つの関数）の両方で 90°・270° が出る。hardware の 180°（p009・p010）が無い間も、試験の policy ANY では 180° も turn で出る
Prerequisites: p002a の出力（main 済み: `rotate.c`・`kwl_output_size_set`・`compose->opened` と `kwl_output_rotated`・`target_*`・`kwl_compose_target_pass`・glass の幅・高さの引数・`ROTATED` の規則）。p002a は T1-576 の (A)(C) が PASS、(B) の AAT は未（Q1）。review-4 の p002b の条件 C7（p002a で直した）・S12・M21（この phase.md の §3・§6 で直す）

## 0. 要点（design-reviewer・Q1 向け）

（§0〜§10 は第 1 版の設計。実装で採った形は §11.2、review-1 の反映は §12 が優先する。）

- **turn の資源** `struct kwl_turn`（anchor に 1 つ）: 論理の大きさの turn.image（OPTIMAL、`COLOR_ATTACHMENT | SAMPLED | TRANSFER_SRC | TRANSFER_DST`）、view、framebuffer（turn の pass で作る）、descriptor set（`compose->sampler` = NEAREST）。2 つの render pass `turn_clear_pass`（`UNDEFINED`→`SHADER_READ_ONLY_OPTIMAL`、CLEAR）・`turn_load_pass`（`SHADER_READ_ONLY_OPTIMAL`→同、LOAD）と pipeline は compositor に 1 つずつ（format は一生変わらない）。
- **shader**: 新しいのは頂点の `turn.vert` だけ。断片は既存の `quad.frag`（`texture()`、i915 で動いている物）を使い、pipeline layout も既存の quad の物（push constant 32 byte）。`turn.vert` は全画面の quad を描き、texcoord を回転の置き換え（90: (v, 1−u)…）で出す。`gl_FragCoord`・`gl_VertexIndex`・`texelFetch` を使わない（§2.2、第 4 版 §4.2 の 6 との違い）。
- **1 frame の列**（turn あり）: scene の pass（turn.image に論理の damage だけ、cursor を含む）→ turn pass（swapchain の画像に、buffer age の union を物理に写した矩形）→ shot → heads（mirror の元は turn.image）。CLEAR/LOAD の選び方は §2.4。
- **damage**（§2.5）: turn.image は 1 枚なので自分の「最後に描いた frame」`turn.frame` を持ち、scene の pass はその後の damage だけ。swapchain の画像は今の `image_frames` の union を `kwl_rotate_rect` で物理に。
- **実行中の回転**（review-4 S12 の直し、§3）: `kwl_output_select_rotation()`（利用者の選択、mask の検査、`rotation_selected`、保存）と `kwl_output_apply_rotation()`（実効の回転の切り替え、plan、steps、park）の 2 つ。p002b の呼び手は試験の口（shot.c の `ROTATE`）だけで、Settings（p004）と game の session（p012）は後でこの 2 つを呼ぶ。
- **能力**（p002a の「設計との違い」4 の決め、§4）: `KWL_TURN_BUILD`（code がある、全部の build で 1）と `KWL_TURN_DEFAULT`（既定で出してよい、arm64 = rpi4 は 0）に分ける。hw180 は p010 まで読まないので、**この段では既定の policy では何も回らない**（Settings も無い）。回るのは試験の policy ANY だけ。
- **試験の policy の渡し方**（§4.2、M16 の前提の訂正）: zedBSD の session は `keiland-desktop.in` でなく sessiond の `/etc/keiland/session`（`userland/desktop/sessiond/session.sh:17`）が `/bin/wayland --session` を exec する。`keiland-desktop.in` は Linux・FreeBSD の物（`keiland-linux.mk:133`、`keiland-freebsd.mk:133`）。第 4 版 §12.2 の「`keiland-desktop` に policy の file を読む行」は zedBSD の session に届かない。代わりに、試験の image だけにある shot.c の口に `POLICY any` と `ROTATE N` を足す（`--rotate-policy=any` は `--testing` の起動用に残す）。

## 1. 今の tree の事実（main `1eb79f9db` = agent/p1 の merge の後、`grep -n` で確かめた）

| 事実 | 所在 |
| --- | --- |
| `compose_targets` が `target_width/height` を swapchain の大きさで書き、swapchain の画像ごとに view・framebuffer（`compose->pass`、物理）・semaphore を作る | `compose.c:2101-2155` |
| frame の pass を選ぶのは `kwl_compose_target_pass(compose, slot, load)`（今は `compose->pass`／`pass_load`） | `compose.c:394-408`、呼び手 `compose.c:2911-2913, 2998`、`backdrop.c:127`、`heads.c:1281` |
| `compose_record` の列: scene の restore（reuse の時、pass の外）→ pass（`framebuffers[image]`、`framebuffer_now`・`clip_framebuffer`）→ 窓 → scene の capture（pass を閉じて写し、LOAD で開き直す）→ cursor → pass の終わり → `kwl_shot_record(…, output.images[image])` → `kwl_heads_record(…, output.images[image])` | `compose.c:2789-3030` |
| `compose_region` が今の frame の damage を `history[slot]` に置き、`image_frames[image]` の後の union を `target_*` に clamp、面積 70% で全体、`KWL DAMAGE` の log | `compose.c:2659-2785` |
| record の失敗は `scene.valid = 0`（`compose.c:713`）、submit の失敗は compositor の終わり（`compose.c:3626-3634`） | — |
| `kwl_scene_copy` の barrier は `PRESENT_SRC_KHR` を前提（capture と restore の両方） | `scene.c:114-228`（`:160, :213`） |
| shot と mirror の barrier も `PRESENT_SRC_KHR` を前提。mirror は `compose->readback` が無ければ黒、blit の元の大きさは `target_*`（p002a） | `shot.c:279-340`、`heads.c:1181-1248` |
| backdrop は scene を小さく描き直す（swapchain の画像を読まない）。取り直しは `framebuffer_now` と `kwl_compose_target_pass(…, 1)`。target は `target_* / BACKDROP_SCALE` で最初の使用の時に作る | `backdrop.c:58-135` |
| layers は `kwl_layers_open` が `target_*` で作り、失敗は `failed`（直に描く）。scene も任意（`KWL SCENE unavailable`） | `layer-cache.c:89-140`、`compose.c:487-504` |
| quad の NDC は `server->width/height`（論理）か head の view で計算 | `compose.c:2619-2640`、`glass.c:1920` |
| sampler: `compose->sampler` は NEAREST、`linear_sampler` は LINEAR。quad の pipeline layout の push constant は 32 byte | `compose.c:1648, 1662-1675` |
| shader は `shaders/*.vert|frag` を `shaders/regenerate.py`（glslc・spirv-val）で `shaders.h` に。build は header だけを使う | `shaders/regenerate.py:1-60` |
| `quad.vert` の注記「i915's native compiler does not take gl_VertexIndex」、`panel.vert` の注記「instead of gl_FragCoord, which i915's native compiler does not take」 | `shaders/quad.vert`、`shaders/panel.vert:21-22` |
| glass: `kwl_glass_resize` は quiesce と idle の後に古い壁紙と blur を先に捨てて作り直す。loader の結果は「今の `server->width/height` と同じ大きさ」の時だけ写し、違えば event loop で描き直す（入れ替えの前に届いた結果は自然に捨てられる） | `glass.c:565-597, 728-737` |
| `kwl_output_rotated` は `compose->opened` と今の状態を `KWL_ROTATE_MODE_OPEN` で比べ、RESIZED で `output_resized(server, 1)`・`kwl_outputs_changed`、ROTATED で dirty と log | `output-switch.c:357-389` |
| `output_resized(server, glass_rebuild)` は glass、`kwl_glass_output_resized`（refit、最後に `kwl_arrange_output_resized`）、pointer の clamp | `output-switch.c:783-813`、`shell.c:2733-2811`、`arrange-shell.c:974-977` |
| `compose_rotation_choose` は prepare の最初で config・`--rotate` を mask で検査し `kwl_output_size_set(mode, wanted)` | `compose.c:1478-1518` |
| `KWL_TURN_BUILD 0`、`kwl_rotate_mask(hw180, turn_build, policy, &why)` | `rotate.h:71, 120` |
| shot.c は試験の image だけ（`ZEDBSD_TEST_SCREEN_CAPTURE=y`、他は `shot-none.c`）。request は `PING`・`SHOT`・`DISPLAYS`・`MODE`・`PLACE` | `shot.c:8-40, 504-525`、`Makefile:10-12` |
| zedBSD の session: sessiond が `/etc/keiland/session`（`session.sh`）を sh で起こし、それが `exec /bin/wayland --session --glass --socket=… $picture "$@"` | `sessiond/session.c:668-673`、`sessiond/session.sh:17`、`sessiond/sessiond.h:22` |
| heads の保存は static の `heads_save(&compose->config)` | `heads.c:106, 1536, 649, 2089, 2185` |

## 2. turn pass

### 2.1 資源と寿命

- `struct kwl_turn {VkImage image; VkDeviceMemory memory; VkImageView view; VkFramebuffer framebuffer; VkDescriptorSet set; uint32_t width, height; unsigned rotation; uint64_t frame;}` を `compose->turn`（anchor）に。head は p005。`frame` は turn.image に最後に描いた frame の番号（0 = 中身なし、次は全体）。
- compositor に 1 つずつ（初めて要った時に作り、compositor の終わり `compose.c:934` の隣で壊す）: `turn_clear_pass`・`turn_load_pass`・`turn_pipeline`（`turn.vert`＋`quad.frag`、layout は `compose->layout`、render pass は `compose->pass`（互換: format と sample 数が同じ））。
- `turn_new(compose, width, height, rotation, &turn)`: 先に `vkGetPhysicalDeviceFormatProperties` の `optimalTilingFeatures` に `SAMPLED_IMAGE`・`COLOR_ATTACHMENT`・`TRANSFER_SRC`・`TRANSFER_DST`・`BLIT_SRC` があるかを見て、無ければ `ENOTSUP`。上の 3 つが無ければここで作る。image・memory（device local）・view・framebuffer（`turn_clear_pass`、論理の大きさ）・set（`kwl_compose_set_get`、`compose->sampler`、`SHADER_READ_ONLY_OPTIMAL`）。途中の失敗は作った物を捨てて `ENOMEM`／`EIO`。
- `turn_free(compose, &turn)`: device が idle の時だけ（§3.3 の park の中、output の close、compositor の終わり）。set は `kwl_compose_set_put`。
- output の open（`compose_output_open_parked`）: `compose_targets` の後に、prepare の choose が決めた `turn_rotation != 0` なら `turn_new(Wl, Hl)`。失敗は open を失敗させず 0° で開く（M13）: `turn_rotation = 0`、`kwl_output_size_set(mode, 0)`、`target_*` は swapchain、`rotate_why |= KWL_ROTATE_WHY_RESOURCE`、`rotation_selected` と `rotate=` は保つ、log `KWL OUTPUT rotate unsupported key=… rotation=… reason=resource errno=…`。open の最後の `kwl_output_rotated` が glass・窓を 0° の大きさに合わせる（`compose_display` が論理で作った glass は RESIZED で作り直される）。
- 成功なら `target_width/height = turn.width/height`、`turn.frame = 0`。scene と layers はこの `target_*` で作る（今の順のまま: `compose_targets` → turn → scene → layers）。
- output の close: `compose_targets_destroy` の隣で `turn_free`。

### 2.2 shader と pipeline

- `shaders/turn.vert`（新、`regenerate.py` の表に足し `shaders.h` を作り直す）:
  - push constant は quad と同じ 32 byte の範囲を `vec4 u; vec4 v;` として読む: texcoord = `(dot(u.xy, corner) + u.z, dot(v.xy, corner) + v.z)`、`gl_Position = vec4(corner * 2.0 − 1.0, 0, 1)`（全画面、物理）。
  - 回転ごとの値（corner は物理の (0,0)〜(1,1)、Vulkan の y は下向き）: 90: (u, v) = (corner.y, 1 − corner.x)、180: (1 − corner.x, 1 − corner.y)、270: (1 − corner.y, corner.x)。画素の中心 (X+½)/Wp は論理の画素の中心に正確に当たる（90: u = (Y+½)/Hp = (y+½)/Wl、v = 1 − (X+½)/Wp = (Wp−1−X+½)/Hl。§2 の `kwl_rotate_point_back` と同じ）。NEAREST の sampler で画素がそのまま写る。
  - 値は CPU の `kwl_turn_coefficients(rotation, float u[3], float v[3])`（rotate.h の inline、host 試験で `kwl_rotate_point_back` と全画素で比べる）。
- 第 4 版 §4.2 の 6（`turn.frag` が `gl_FragCoord` を `floor` して `texelFetch`）からの変更の理由: compositor の shader の注記どおり i915 の native compiler は `gl_FragCoord`・`gl_VertexIndex` を今も避けている（`panel.vert:21-22`、`quad.vert`）。`quad.frag` の `texture()` は i915・venus で毎 frame 動いている。新しい断片の shader を i915 の compiler に通す危険を負わない。
- 描くのは `kwl_list_draw` の三角形 2 つ（既存の corners の vertex buffer）。blend は無し（`KWL_DRAW_OPAQUE` の形）。

### 2.3 frame の中の列（`compose_record`、turn ありの時）

1. `reuse` の restore（`kwl_scene_copy(…, restore = 1)`）の先は turn.image（pass の外の layout は `SHADER_READ_ONLY_OPTIMAL`）。
2. scene の pass: `kwl_compose_target_pass(compose, ANCHOR, load)` は turn ありなら `turn_clear_pass`／`turn_load_pass` を返す。framebuffer は `turn.framebuffer`（`framebuffer_now`・`clip_framebuffer` も）。renderArea・viewport・scissor・`pass_area`・`scissor_now`・clips は論理（`target_*`、今と同じ式）。
3. scene の capture（pass を閉じて写す）の元も turn.image。
4. cursor、pass の終わり。
5. **turn pass**（新 `compose_turn_record(server, command, image, whole, rects, count)`、`compose.c`）: §2.4 の pass で `framebuffers[image]`（物理）を開き、viewport は物理の全体、矩形ごとに scissor を置いて `turn_pipeline` の quad を 1 回ずつ描く。push constant は §2.2 の値。descriptor は `turn.set`。
6. `kwl_shot_record(…, output.images[image])`（物理、今と同じ。turn pass の後）。
7. `kwl_heads_record(…, source, source_layout)`: mirror の元は turn ありなら turn.image（論理、`SHADER_READ_ONLY_OPTIMAL`）、無ければ swapchain の画像（`PRESENT_SRC_KHR`）。turn.image は `TRANSFER_SRC` を持つので、`compose->readback` が無くても mirror が映る（初めの 1 回 `KWL OUTPUT mirror source=turn`）。

- barrier の layout は `compose_target_layout(compose)`（`SHADER_READ_ONLY_OPTIMAL` か `PRESENT_SRC_KHR`）と `compose_target_image(compose, image)` で渡す: `kwl_scene_copy` と `heads_record_mirror` に `VkImageLayout layout` の引数を足す（source の stage は turn なら `FRAGMENT_SHADER | COLOR_ATTACHMENT_OUTPUT | TRANSFER`）。shot は swapchain のままなので変えない。
- turn の 2 つの pass の外部 dependency: 入口 `srcStage = FRAGMENT_SHADER | TRANSFER`・`srcAccess = SHADER_READ | TRANSFER_READ | TRANSFER_WRITE` → `dstStage = COLOR_ATTACHMENT_OUTPUT`・`dstAccess = COLOR_ATTACHMENT_READ | WRITE`。出口 `COLOR_ATTACHMENT_OUTPUT`・`COLOR_ATTACHMENT_WRITE` → `FRAGMENT_SHADER | TRANSFER`・`SHADER_READ | TRANSFER_READ`。同じ queue に順に submit されるので、frame N の turn pass（読み）と N+1 の scene の pass（書き）の WAR も閉じる。
- turn pass の swapchain の側の dependency は今の `compose->pass`／`pass_load` の物（acquire の semaphore の待ちは `COLOR_ATTACHMENT_OUTPUT`）。

### 2.4 turn pass の CLEAR/LOAD（C3）

| 場合 | pass | renderArea | 描く矩形 |
| --- | --- | --- | --- |
| swapchain の画像が全体（`compose_region` が 0: 初めて・history より古い・70% 超・全体の damage） | `compose->pass`（CLEAR、`UNDEFINED`→`PRESENT_SRC_KHR`） | 物理の全体 | 全体 1 つ |
| 部分 | `pass_load`（LOAD、`PRESENT_SRC_KHR`→同） | 物理の union の箱 | 論理の各矩形を `kwl_rotate_rect` で物理に写した矩形（数は同じ、最大 `KWL_DAMAGE_RECTS`） |

- scene の pass の領域（turn.image）と turn pass の領域（swapchain）は別に決める（§2.5）。turn pass が部分でも、写す矩形の中の turn.image は最新（turn.image は毎 frame その frame の damage を描く）。
- 70% は論理の面積で決める（物理と面積は同じ）。

### 2.5 damage と buffer age（第 4 版 §4.3）

- `compose_region` を「どの frame から後の damage か」を取る形に分ける: `compose_region_since(server, last, region, rects, &count)`（history の union、clamp、70%、log）。今の frame の history への記録は分けた `compose_history_note(server)` が 1 回する。
- turn 無し: 今のまま（`last = image_frames[image]`）。
- turn あり:
  - scene の pass: `last = turn.frame`（0 なら全体）。描いた後 `turn.frame = frame`（`image_frames` と同じく記録の時に書く）。
  - turn pass: `last = image_frames[image]` の union を `kwl_rotate_rect(turn.rotation, Wp, Hp, …)` で物理に。
  - 不変条件「damage を記録した frame は必ず turn.image に描く」: record が失敗した時（`compose.c:713`）は `turn.frame = 0`（scene.valid と同じ所）。submit の失敗は compositor が終わる。shot の whole（`server->dirty`）は両方を全体にする。
- log: `KWL DAMAGE … rects=N …` の後ろに ` turn=x,y,w,h …`（物理、turn ありの時だけ）。scene の pass が全体で turn pass が部分（またはその逆）の時も両方が分かるように ` scene=whole|partial`。

### 2.6 cursor の plane・直の scanout・overlay

- p002a のまま: `rotation != 0` で cursor の plane を使わない（合成の cursor は scene の pass で turn.image に描かれ、一緒に回る）。直の scanout は `KWL_SCANOUT_ROTATED`（turn の出力では game mode に入らない、p012 まで）。

## 3. 実行中の回転（review-4 S12 の直し）

### 3.1 2 つの関数（output-switch.c）

- `int kwl_output_select_rotation(struct kwl_server *server, unsigned slot, unsigned rotation, unsigned *applied, int *saved)` — 利用者の選択:
  1. `kwl_rotate_valid` でなければ `EINVAL`。slot が anchor 以外は `ENOTSUP`（head は p005）。mask に無ければ `ENOTSUP`（何も変えない、log `KWL OUTPUT rotate refused … reason=mask`）。
  2. 選択が同じで実効も同じなら何もしない（`*applied = 1`、`rotate=` の行が無ければ書く）。**選択が同じでも実効が違う**（`WHY_RESOURCE` で 0 で開いた後、review-4 L47）なら 3 へ（当て直す）。
  3. `rotation_selected = rotation`、`kwl_displays_set_rotation(&compose->config, key, rotation)`。
  4. 出力が閉じている・lost・prepared だけ: 保存（`kwl_heads_config_save(server)`、heads.c の `heads_save` を包む新しい公開の関数）だけ、`*applied = 0`（次の open の choose が当てる）。
  5. 直の scanout の最中（`server->scanout != NULL`）: 保存、`kwl_scanout_leave(server, KWL_SCANOUT_ROTATED)`、`*applied = 0`（次の pass の `enter_window_mode` の open が当てる。p002b の間は turn の出力が game mode に入らないので、ここに来るのは 0° の出力だけ）。
  6. 他は `kwl_output_apply_rotation(server, slot, rotation, KWL_ROTATE_MODE_USER)`。失敗なら `rotation_selected` と config を前の値に戻して、その errno（`*applied = 0`）。成功なら `*applied = 1`、SAVE の bit で保存。
- `int kwl_output_apply_rotation(struct kwl_server *server, unsigned slot, unsigned effective, unsigned mode)` — 実効の回転の切り替え（p012 の session も `KWL_ROTATE_MODE_SESSION_PAUSE/RESUME` でこれを呼ぶ）:
  1. `kwl_rotate_plan(effective, compose->rotations, compose->hw180, &scanout, &turn)`。`before = compose->opened`（C7: 最後に開いた・回した状態）、`after` は新しい（論理の大きさ, selected, effective, scanout, turn）。`steps = kwl_rotate_steps(&before, &after, mode)`。
  2. steps が 0（ROTATED も SAVE も無い）なら 0。
  3. `REOPEN`（scanout が変わる）: p010 の範囲。p002b では hw180 が常に 0 なので plan は scanout を返さない。来たら `ENOTSUP`（log）。
  4. `TURN`: §3.2。失敗は何も変えずに errno。
  5. `kwl_compose_unpark` の後: RESIZED なら `kwl_outputs_changed`。ROTATED なら dirty。SAVE なら保存。`compose->opened = after`。log `KWL OUTPUT rotate key=… rotation=… selected=… scanout=… turn=… width=… height=… mode=WxH steps=… by=user ms=…`（`by` は mode の名前、p002a の `by=open` と同じ形）。

### 3.2 TURN の手順（anchor）

1. **park の外で先に作る（失敗したら何も変えない）**: 新しい turn（`after.turn != 0` の時、`turn_new(Wl', Hl')`）、glass の新しい大きさの壁紙と blur（`kwl_glass_size_prepare(server, Wl', Hl', &pending)`、新: 今の `wallpaper_create` の中身を「`struct kwl_glass_size {wallpaper, blurred}` に作って今の絵を描く」に分ける。CPU の描き直しはここ、park の外）。どちらかが失敗したら作った物を捨て、`ENOMEM` などと log `KWL OUTPUT rotate refused errno=… reason=turn|glass`。
2. `kwl_compose_park`、`(void)vkDeviceWaitIdle`（回転は利用者の稀な操作。古い turn・scene・layers・壁紙・backdrop の target をすぐ捨てられる。費用は log の `ms=` で測る。WS213 の R の step 1 の破棄の列に回す形は、park が長いと分かった時の改善として §8 に）。
3. `kwl_output_size_set(server, compose->mode_width, compose->mode_height, effective)`、`compose->scanout_rotation`・`turn_rotation`。
4. 入れ替え: 古い turn を `turn_free`、新しい turn を置く（`after.turn == 0` なら turn 無し、`target_*` は swapchain）。`target_*` を書く。`kwl_glass_size_swap(server, &pending)`（古い壁紙と blur を release、新しい物を置き、`kwl_host_image_written`）。
5. 大きさに付く任意の物を作り直す: `kwl_scene_close` → `kwl_scene_open(target_*)`（失敗は `KWL SCENE unavailable`、今と同じ）、`kwl_layers_close` → `kwl_layers_open`（失敗は `failed`、今と同じ）、backdrop は新 `kwl_backdrop_resize(compose)`（2 つの target を捨て、自分の小さな pass は残し、`BACKDROP_UNTRIED`。次の記録で新しい大きさ）。`memset(image_frames, 0)`・`turn.frame = 0`・`scene.valid = 0`（次の frame は両方とも全体）。
   - scene・layers を「先に作る」にしない理由（第 4 版 §4.6 の A の 3 との違い）: どちらも作れなくても今の経路で描ける任意の cache で、失敗が回転を止める理由にならない。park の中の作成は image と memory の確保だけ（中身は次の frame が描く）。
6. **RESIZED**（`mode` が USER か OPEN）: `output_resized(server, glass_rebuild = 0)`（glass は 4 で入れ替え済み）: 窓の refit・arrange・pointer の clamp。review-4 M21: `kwl_arrange_output_resized` は refit の中（`shell.c:2811`）にあり、新しい作業領域に窓が入らない時は arrange の mode を終える（`arrange-shell.c:974-977`）。普通の回転では output switch と同じくこれを受け入れ（§7 の制限）、SESSION の mode（p012）では refit と arrange の両方を飛ばす（`output_resized` に「refit するか」の引数を p012 で足す。p002b は USER と OPEN だけ）。
7. **ROTATED**: pointer を「同じ物理の点」へ（新しい `output_pointer_turn` の warp の move: 古い論理 → `kwl_rotate_point(before.rotation, Wp, Hp)` → 物理 → `kwl_rotate_point_back(after.rotation)` → 新しい論理。anchor の上の pointer だけ、head の上は保つ）。touch・pen の contact の cancel と受け箱の geometry は p003（p003 の前は絶対の device の写像がまだ回らないので、p002b ではそれらを触らない）。Settings への tell は p004。
8. `kwl_compose_unpark`。その後 §3.1 の 5。

- open の経路（`kwl_output_rotated`、`KWL_ROTATE_MODE_OPEN`）は今のまま（turn の資源は open が作った。REOPEN・TURN の bit は落ちる、L49）。ROTATED の時に 7 の pointer の warp を足す（display が替わる switch では意味が無いので、`compose->display` が `opened` の時と同じ時だけ。他は今の clamp、review-4 L46。そのために `compose->opened_display` を持つ）。

### 3.3 並行性と失敗

| 場面 | 扱い |
| --- | --- |
| turn・壁紙の確保の失敗（§3.2 の 1） | 何も変えず errno、log。選択も config も前に戻す（§3.1 の 6） |
| park の中の scene・layers・backdrop の失敗 | 今の「unavailable」で描く。回転は成功 |
| park の時間切れ | 既存（`_exit`） |
| 回転の最中に壁紙の loader の結果が届く | `glass.c:728-737` が今の `server->width/height` と比べるので、古い大きさの結果は写さずに描き直す（追加の code は要らない。§5 の host 試験で確かめる） |
| 回転の最中の hotplug・output switch | どちらも main thread の上で順に走る（同じ thread）。switch は close → open で turn を作り直し、open の比較が `opened` と比べる |
| frame の記録の失敗 | `turn.frame = 0`（§2.5） |

## 4. 能力と試験の policy

### 4.1 mask（p002a の「設計との違い」4 の決め）

- `KWL_TURN_BUILD`（rotate.h）= 1: turn の code は全部の build にある。
- `KWL_TURN_DEFAULT`（rotate.h）: `#if defined(__aarch64__)` なら 0（rpi4、WS141 p015 まで: bcm2711 は sample する画像を UIF で持ち、raster の turn.image を毎 frame 変換するので Guardrail に反する、第 4 版 §4.9）、他は 1。
- `kwl_rotate_mask(hw180, turn_default, turn_build, policy, &why)`（引数を 1 つ足す。p002a の host 試験を直す）:
  - DEFAULT: 0 は常に。180 は hw180。90・270 は `turn_default && hw180`。why: hw180 が無ければ `WHY_SCANOUT`、hw180 があり `turn_default` が 0 なら `WHY_TURN`。
  - ANY: `turn_build` なら 4 つ全部（180 は hw180 なら scanout、無ければ turn）。rpi4 でも ANY なら 90/270 が出る（測るため。log に `KWL OUTPUT rotate policy=any turn_default=0`）。
- hw180 は p010 まで 0 のまま（`compose->hw180` を読まない）。**この段の既定の policy では、どの display も 0 しか選べない**（今と同じ見た目）。

### 4.2 試験の policy の渡し方

- `--rotate-policy=any`（main.c、usage にも）: `server->rotate_policy`。`--testing` の起動（T1 の手順、§6）で使う。
- shot.c（試験の image だけ）の request を 2 つ足す:
  - `POLICY any|default`: `server->rotate_policy` を変え、mask と why を計算し直す（`kwl_output_policy_set(server)`、output-switch.c）。今の実効の回転が新しい mask に無ければ `kwl_output_apply_rotation(…, 0, USER)`（選択は保つ）。答え `OK mask=M why=W`。
  - `ROTATE N`: `kwl_output_select_rotation(server, ANCHOR, N, &applied, &saved)`。答え `OK applied=A saved=S rotation=R width=W height=H` か `ERROR errno=E`。
  - これで zedBSD の session（`/etc/keiland/session` が起こす compositor）も、Settings（p004）の前に実行中の回転を試せる。製品の build には shot.c が無いので、policy ANY は製品に出ない（D13 の「test image だけ」を保つ）。
- 第 4 版 §12.2 の「`ZEDBSD_TEST_ROTATE_POLICY=y` で `keiland-desktop` に policy の file を読む行」はやめる（zedBSD の session に届かない、§0）。p004 の Settings の試験も同じ `POLICY any` を先に送る形で、session を作り直さずに済む。§12.2 の手順の直しは Q1 に頼む（p001 は P1 の範囲の外）。

## 5. 試験（host、`plan/ws217/tests/`）

| 試験 | 中身 |
| --- | --- |
| `host-rotate.c`（p002a の物に足す） | `kwl_rotate_mask` の新しい引数の表（hw180 × turn_default × turn_build × policy、why）。`kwl_turn_coefficients` の 3 つの角度 × 4 つの panel（5x3・3x5・1x1・8x2）の全画素で、物理の画素の中心の texcoord × 論理の大きさ を floor した値が `kwl_rotate_point_back` と一致 |
| `host-turn-damage.c`（新） | `compose_region_since` と turn の物理の矩形（compose.c を直に link できなければ、矩形の計算を `damage.c` か rotate.h の純粋な関数 `kwl_turn_rects()` に出して link）: 1 frame の damage・age 2/3 の union・history より古い・70%・全体の damage、論理 → 物理の写像の面積の一致と境界、`turn.frame = 0` で scene が全体 |
| `host-output-select.c`（新） | `kwl_output_select_rotation` の判定の表を純粋な関数 `kwl_rotate_select_decide(state, request, mask, open, lost, scanout)` に出して: mask に無い、同じ、同じ選択で実効が違う（L47）、閉じている、直の scanout、apply へ。apply の steps は p002a の `kwl_rotate_steps` の表を USER の mode で（SAVE） |
| `host-glass-size`（第 4 版 §12.1） | `glass.c` の `server->width/height` の読みを「作る時（引数）」と「描く時」に分けた grep の表を phase.md に（host の program は作らない） |
| shader | `python3 userland/desktop/wayland/shaders/regenerate.py`（glslc・spirv-val）で `shaders.h` を作り直し、差分が turn.vert の配列だけ |
| 回帰 | p002a の `host-rotate.sh`・`host-displays-conf.sh`・`host-scanout-rules.sh`、`plan/ws113/tests/host-output-switch.sh`、`plan/ws211/tests/anim-host-test.sh`、`plan/ws213/tests/cursor-due-host-test.sh` |
| build | amd64（`config/ci/config-amd64.mk` の `bin/wayland`）、rpi4（`config/ci/config-rpi4.mk` の `bin/wayland`）、Linux（`keiland-linux` の compositor）、warning 0。`plan/tools/keiland-os-boundary/check.sh`。`python3 -I plan/tools/style-check.py` の変えた file |
| grep の受け入れ | `grep -n 'compose->pass\b\|pass_load' …` の helper の外の行に turn pass の 2 行（`compose_turn_record`）が増えるだけ。`compose->output\.width\|height` の物理の行は p002a の表と同じ |

## 6. T1 の試験（QEMU、Venus、policy ANY）の案

第 4 版 §13 の束 A は p002b+p003。p003 の前に p002b だけで流すかは Q1 が決める（下は p002b の単独の形）。

- image: 依頼の tree で `plan/ws217/tests/config-amd64.mk`（新: `plan/tools/aat/config-amd64-aat.mk` を include するだけ。`ZEDBSD_TEST_SCREEN_CAPTURE=y` と `keiland-shot` はそこにある）。`--file` の複写は無い。
- 1. **起動の 90**（`--testing`）: AAT の手順の `aat --qemu run` で `/bin/wayland --testing --width=1280 --height=800 --glass --rotate-policy=any --rotate=90 > /tmp/zdesktop.log` と wltest の窓 → log `KWL OUTPUT open width=800 height=1280`、`KWL GLASS` の壁紙の大きさが 800x1280。`keiland-shot` の PNG（1280x800）で system bar が物理の右端に縦、窓の文字が縦。270・180 でも（180 は turn）。
- 2. **部分 frame**: `--log-frames` で窓を少し動かす → `KWL DAMAGE … turn=` の物理の矩形が式と合う。`keiland-shot --request "SHOT partial"`（§4.2 の request に足す: 次の frame を全体にしない）の絵に縦線・背景色の帯が無い。
- 3. **実行中の回転**（session、AAT の image）: `keiland-shot --request "POLICY any"` → `OK mask=15`。`ROTATE 90` → `OK applied=1 saved=1 rotation=90 width=800 height=1280`、log `KWL OUTPUT rotate … steps=turn|rotated|resized|save by=user`、撮影。`ROTATE 270`（steps=turn|rotated|save、大きさは同じ）、`ROTATE 180`、`ROTATE 0`。各段で窓が新しい大きさに refit、pointer が同じ物理の点。`$HOME/.config/keiland/displays.conf` に `rotate=… 90` が書かれ、logout → login で縦から始まる（その時 policy は既定に戻るので `KWL OUTPUT rotate unsupported … reason=mask why=1` で 0。policy ANY を `POLICY any` で当て直すと次の `ROTATE` から回る、を記録）。
- 4. **mirror**（WS113 の 2 画面の image の形、任意）: `MODE mirror` で 90 → head に縦の desktop が黒い帯つきで正立、`KWL OUTPUT mirror source=turn`。
- 5. **回帰**: 0° の smoke suite（AAT）、game mode の暫定（90 の出力で wltest の fullscreen・content type game が `KWL SCANOUT direct=0 reason=rotated`）。
- 合否は log の行と PNG（T1 が見る）。時間: `KWL OUTPUT rotate … ms=`、`KWL PERF pass`。

## 7. 制限（第 4 版 §14 に足す物）

- 回転で新しい作業領域に窓が入らない時は、output switch と同じく arrange の mode が終わる（review-4 M21）。
- 回転の park の中で device を idle まで待つ（§3.2 の 2）。1080p で数十 ms の見込み（未測）。
- p003 の前は、touch screen・pen・絶対の pointer の座標が回転に合わない（写像は p003）。p004 の前は Settings に回転が無く、試験の口だけ。

- （review-1 の後）extended の mode で head が開いている間は回せない（`reason=heads`、p005 まで）。open の中の turn の資源の失敗で 0° に落ちる経路は QEMU で未検証（試験の注入口を作らない）。

## 8. 段の中の順と見積

1. rotate.h の mask の引数と `KWL_TURN_DEFAULT`、`kwl_turn_coefficients`、host 試験（0.3 LW）。
2. turn の資源・pass・pipeline・`turn.vert`、open の経路（M13）（1 LW）。
3. frame の列・damage・scene/shot/mirror の layout（1 LW）。
4. select/apply、glass の size の prepare/swap、backdrop の resize、pointer の同じ物理の点（1 LW）。
5. shot.c の `POLICY`・`ROTATE`・`SHOT partial`、`--rotate-policy`（0.3 LW）。
6. build 3 つ、host 試験、grep の表、T1 の依頼（0.4 LW）。

計 約 4 LW。park の破棄を R の step 1 の列に回す改善は、T1 の `ms=` が長い時に別の Phase で。

## 9. 自分での敵対的な見直し

| 問い | 答え |
| --- | --- |
| scene の pass と turn pass の領域を別にすると、turn pass が部分の時に turn.image の写す所が古くないか | turn.image は毎 frame その frame の damage（と `turn.frame` の後の全部）を描くので、どの frame の後でも turn.image 全体が最新。turn pass が写す矩形はどれも最新 |
| render pass の互換: `compose->pass` で作った pipeline を turn の pass で使えるか | 互換は attachment の format・sample 数で決まり、layout・load/store は関係しない（Vulkan の render pass compatibility）。framebuffer も同じ |
| turn.image の最初の layout が `UNDEFINED` の時に LOAD の pass が来ないか | `turn.frame = 0` の時は scene の pass が全体（CLEAR）。reuse の restore は `scene.valid` の時だけで、valid は全体の frame の後 |
| NEAREST と texcoord の丸め | 画素の中心の texcoord は (n+½)/W の形で、texel の中心に当たる。float の誤差は ½ texel より十分小さい（4096 px で 1e-4 texel 程度）。host 試験で全画素を確かめる |
| 部分の turn pass の scissor と renderArea | renderArea は物理の箱、scissor は写した各矩形。`pass_load` は LOAD なので箱の中の矩形の外も元のまま |
| 回転の後の最初の frame | `image_frames` を 0 にするので swapchain の全画像は最初の 1 回が全体、turn も全体 |
| shot が turn の前に記録されないか | 列を §2.3 の順に変える（shot は turn pass の後） |
| 古い turn を park の中で捨ててよいか | park は in flight の frame の完了を待ち、さらに `vkDeviceWaitIdle`。render thread の held の swapchain の画像は残る（swapchain は変わらない）が、記録済みの list は無い |
| `kwl_output_select_rotation` の失敗の戻しで config を書いてしまわないか | 保存は apply が成功して SAVE の bit がある時だけ。閉じた出力の時だけ先に保存 |

## 10. 残る問い（推奨つき）

| # | 問い | 推奨 |
| --- | --- | --- |
| Q1 | §2.2 の shader の変更（`turn.frag`・`texelFetch` → `turn.vert`＋`quad.frag`・NEAREST） | 採る（i915 の compiler の危険を避ける。結果は同じ画素） |
| Q2 | §3.2 の 5（scene・layers・backdrop は park の中で作り直す、先に作らない） | 採る（どれも任意の cache） |
| Q3 | §4.2（試験の policy は shot.c の request、`keiland-desktop` の行はやめる） | 採る。第 4 版 §12.2 の手順の直しは Q1 |
| Q4 | T1 の束 A（p002b+p003）を待つか、p002b を単独で流すか | 単独で流す（turn の画のずれを p003 の入力と切り分けやすい）。判断は Q1 |

## 11. 実行の記録（2026-10-13 P1、agent/p1、base main `1eb79f9db`）

### 11.1 変えた所

| file | 内容 |
| --- | --- |
| `rotate.h`・`rotate.c` | `KWL_TURN_BUILD` = 1、`KWL_TURN_DEFAULT`（`__aarch64__` で 0）、`kwl_rotate_mask(hw180, turn_default, turn_build, policy, &why)`、`kwl_turn_coefficients()`、`kwl_rotate_select()`（inline）と `KWL_ROTATE_SELECT_*` |
| `turn.c`（新） | `kwl_turn_open/close/destroy_shared/record`、turn の 2 つの pass・pipeline（`turn.vert`＋`quad.frag`、opaque、`compose->layout`）、image（`COLOR_ATTACHMENT|SAMPLED|TRANSFER_SRC|TRANSFER_DST`、format の能力の検査）・view・framebuffer・set（NEAREST） |
| `shaders/turn.vert`（新）・`shaders/regenerate.py`・`shaders.h` | turn の頂点の shader。`shaders.h` は既存の配列が byte 同一で `kwl_turn_vert` だけ増えた |
| `output-rotate.c`（新） | `kwl_output_select_rotation`・`kwl_output_apply_rotation`・`kwl_output_policy_set`、TURN の手順（先に turn と glass、park と idle、入れ替え、scene・layers・backdrop の作り直し）、pointer の同じ物理の点 |
| `compose.c`・`compose.h` | `struct kwl_turn` と turn の欄、`kwl_compose_target_pass` が turn の pass、`kwl_compose_target_image/layout`、`kwl_compose_pipeline_opaque`、open の `compose_turn_open`（失敗は 0° で開き `WHY_RESOURCE`）、close の `kwl_turn_close`、終わりの `kwl_turn_destroy_shared`、damage を `compose_region_since` に分け `compose_turn_region`・`compose_turn_regions`（物理の矩形、`KWL DAMAGE turn …`・`KWL DAMAGE turn-scene …`）、`compose_record` が `struct kwl_frame_region` を取り、turn ありは turn.image に描いて `kwl_turn_record` → shot → heads、mask に `server->rotate_policy` |
| `scene.c`・`scene.h` | `kwl_scene_copy` に `layout`（turn.image は SRO、stage に FRAGMENT_SHADER） |
| `heads.c` | `kwl_heads_record(…, source, layout)`、mirror の元が turn.image なら readback 無しでも写す（`KWL OUTPUT mirror source=turn` を一度）、`kwl_heads_config_save()` |
| `glass.c`・`shm.c` | `kwl_glass_size_prepare/swap/drop`（壁紙と blur を先に作る）、`kwl_host_image_move()`（host image の list の繋ぎを移す。import の構造体を写すだけでは list が壊れる） |
| `output-switch.c`・`kwl.h`・`main.c` | `kwl_output_fit()`、`kwl_output_steps_name()`（public に）、`kwl_output_pointer_turn()` と `kwl_output_rotated` の pointer の順（S1）、`opened_display`、`server->rotate_policy`、main.c は試験の option を `kwl_shot_option()` に渡す |
| `shot.c`・`shot-none.c`・`userland/tests/keiland-shot/main.c` | request `POLICY any|default`・`ROTATE N`・`SHOT partial`、`kwl_shot_option()`（`--rotate-policy`、試験の image だけ。shot-none.c は 0）、keiland-shot の `--partial` |
| `Makefile`・`Makefile.linux`・`Makefile.freebsd` | `output-rotate.c`・`turn.c` |
| `plan/ws217/tests/host-rotate.c`・`.sh` | mask の新しい表（turn_default 0 を含む）、`kwl_turn_coefficients` の 4 panel × 4 角度の全画素、`kwl_rotate_select` の表（M1 の行）、`kwl_rotate_pointer_place` の 4 panel × 16 の組の全画素と端の点（S1） |
| `plan/ws217/tests/config-amd64.mk`（新） | T1 の image（AAT の config を include するだけ） |

### 11.2 設計（§0〜§10）との違い（review-1 の後の形）

1. backdrop は `kwl_backdrop_resize` を作らず、大きさの変わる回転の park の中で既存の `kwl_backdrop_destroy`（小さな pass ごと捨て、state は UNTRIED、次の使用で作り直す）を呼ぶ（review-1 M3 の (b)）。
2. `host-turn-damage.c`・`host-output-select.c` は作らず、純粋な部分（turn の係数、選択の判定 `kwl_rotate_select`、mask、pointer の移し `kwl_rotate_pointer_place`）を `host-rotate.c` に入れた。damage の union と物理の写像は compose.c の中（Vulkan と server に依る）で、写像は `kwl_rotate_rect`（p002a の host 試験で総当たり済み）を使うだけ。
3. select と apply は output-switch.c でなく新しい `output-rotate.c`。pointer の warp の move（`kwl_output_pointer_turn`）は output-switch.c に置き、計算は rotate.h の inline `kwl_rotate_pointer_place`（WS113 の `host-output-switch.sh` が output-switch.c を rotate.c 無しで link するため）。
4. turn pass の pipeline の bind は `kwl_compose_bind`（bind の cache を通す）、pass の後に `kwl_compose_bind_forget`。
5. RESIZED の fit と wl_output の tell は unpark の後（open の経路と同じ、review-1 L10）。park の中は turn・glass・cache の入れ替えだけ。
6. `--rotate-policy` は製品の binary に入れない（review-1 M6、Q1 の決め）: 解釈は試験の image だけの shot.c の `kwl_shot_option()`、製品の `shot-none.c` は何も取らない（Linux・FreeBSD の build も shot-none.c）。usage の行にも出さない。
7. 既定の turn の可否は build の `__aarch64__` でなく実行時の GPU（`VkPhysicalDeviceProperties.vendorID` が `KWL_TURN_VENDOR_OFF` = 0x14e4 なら無効、`compose->turn_default`）（review-1 L3: WS112 の Linux arm64 の package が巻き込まれない）。

### 11.3 command と結果（host、gcc 14.2.0、clang は tree の LLVM）

- `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws217-p002b/amd64 build/ws217-p002b/amd64/bin/wayland` → exit 0、warning 0。
- `make -j16 ZEDBSD_CONFIG=plan/ws217/tests/config-amd64.mk BUILD=build/ws217-p002b/amd64t build/ws217-p002b/amd64t/bin/wayland`（shot.c つき）・`… bin/keiland-shot` → exit 0、warning 0。
- `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/ws217-p002b/arm64 build/ws217-p002b/arm64/bin/wayland` → exit 0、warning 0。
- `make -j16 KEILAND_ASK=n KEILAND_LINUX_BUILD=build/ws217-p002b/linux keiland-linux` → exit 0、warning 0（install はしていない）。FreeBSD の build は未実施（Makefile.freebsd に 2 file を足しただけ）。
- `python3 userland/desktop/wayland/shaders/regenerate.py`（glslc・spirv-val）→ 差分は `kwl_turn_vert` の追加だけ。
- `sh plan/ws217/tests/host-rotate.sh` → PASS（plain・ASan/UBSan）。`host-displays-conf.sh`・`host-scanout-rules.sh` PASS。
- 回帰: `plan/ws113/tests/host-output-switch.sh`・`host-displays.sh`・`host-arrange.sh` PASS、`plan/ws211/tests/anim-host-test.sh` PASS、`plan/ws213/tests/cursor-due-host-test.sh` PASS、`plan/ws141/tests/vulkan-cursor-host.sh` PASS。
- `plan/ws213/tests/list-replay-host-test.sh`（master の試験の一覧に登録）: `kwl_scene_copy` の引数が増えたので link が壊れる。2 つの呼び出しに `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR` を足した写し（`build/tmp/ws217-lr.*`）で流すと出力が golden と byte 同一（scene の barrier の layout と stage は 0° で今のまま）。WS213 の path なので Q1 に直しを頼む（§11.5）。
- `sh plan/tools/keiland-os-boundary/check.sh` → PASS。`python3 -I plan/tools/style-check.py`（変えた 13 file）: main の時から増えた指摘は 0（新しい 2 file も 0）。`git diff --check` 空。

### 11.4 T1 の依頼（Q1 へ送った文面と同じ）

> **T1 依頼（ws217-p002b、compositor の turn pass、QEMU の Venus、p002b 単独）**
> - tree: main に P1 の q948 の merge が入った SHA。
> - image: `AAT_CONFIG=plan/ws217/tests/config-amd64.mk plan/tools/aat/build-image.sh build/<T1 の dir>`（AAT の image。`--file` は無い。`--rotate-policy` と shot.c の `POLICY`・`ROTATE`・`SHOT partial` はこの試験の image にだけある）。
> - A. 起動の turn（`--testing`、AAT の README の QEMU の手順の 2. と同じ起こし方）: `/bin/wayland --testing --width=1280 --height=800 --glass --log-frames --rotate-policy=any --rotate=90 > /tmp/zdesktop.log 2>&1 &` と wltest の窓 1 つ。合否: log に `KWL OUTPUT turn width=800 height=1280 rotation=90`、`KWL OUTPUT open width=800 height=1280`、`KWL GLASS size width=800 height=1280 by=open`。`keiland-shot` の PNG（1280x800）で system bar が物理の右端に縦、窓の字が時計回りに 90° 倒れて読める。`--rotate=270`（左端）、`--rotate=180`（policy ANY なので turn、上下が逆）、`--rotate=0`（今と同じ）でも 1 枚ずつ。
> - B. 部分 frame と全体の byte の比較（A の 90 の続き、review-1 M7）: wltest の窓を `aat drag` で 40 px ずつ 6 回動かす（swapchain の画像が一巡より多く部分の frame を描く）。止めた後、`keiland-shot --partial p.png` を要求してから pointer を 1 px 動かし（要求の後に damage が要る、L11）、続けて `keiland-shot f.png`（全体）。p.png と f.png を画素で比べ、違う画素が無い（時計の領域と pointer の周りの矩形は除く。除いた矩形を記録）。log の `KWL DAMAGE frame=… image=…` と `KWL DAMAGE turn frame=… rotation=90 rects=N x,y,w,h …`（物理）を記録。
> - C. 実行中の回転（session、AAT の image を普通に起こし、Files か Terminal を 1 つ開く）: `keiland-shot --request "POLICY any"` → `OK mask=15 why=0`。`--request "ROTATE 90"` → `OK applied=1 saved=0 rotation=90 width=800 height=1280`、log `KWL OUTPUT rotate key=… rotation=90 … steps=turn|rotated|resized|save by=user pointer=X,Y>X',Y' ms=…`（pointer の 2 つの点が「同じ物理の点」の式 90: (x, y) → (y, 1279−x) に合う。pointer が head でなく anchor の上にある時）、撮影。続けて `ROTATE 270`（steps=turn|rotated|save、`KWL GLASS size` の行が出ない = 作り直さない）、`ROTATE 180`、`ROTATE 0` を各 1 枚。各段で開いた窓が新しい大きさに収まる。`$HOME/.config/keiland/displays.conf` に `rotate=… 90`（0 では行が消える）。`--request "POLICY default"` → `OK mask=1 why=1`（90 で回っていれば `KWL OUTPUT rotate … rotation=0 … by=policy` で 0 に戻り、displays.conf は変わらない）。その後 `--request "ROTATE 90"` → `ERROR errno=…`（ENOTSUP）と `KWL OUTPUT rotate refused … reason=mask`。最後に `POLICY any`・`ROTATE 0`・`POLICY default` で `rotate=` の行を消して戻す（L11）。
> - D. 回帰: `plan/tools/aat/run-aat.sh qemu OUTDIR smoke`（0° のまま、全部 PASS）。
> - 合否は log の行と PNG（T1 が見る）と B の画素の比較。時間: `KWL OUTPUT rotate … ms=`（90・270・180・0）と `KWL GLASS size … by=turn ms=`。
> - 未検証のまま残る物: open の中で turn の資源が作れず 0° で開く経路（M13、`KWL OUTPUT rotate unsupported … reason=resource`）は試験の注入口を作らない（製品の code に試験の切り替えを置かない規約 §12 に合わせる）。mirror（2 画面の image）、extended の head がある時の `reason=heads`、game mode の暫定は p003・p005 の束で。

### 11.5 Q1 への依頼

- `plan/ws213/tests/list-replay-host-test.c` の 2 行（1110 と 1122 の `kwl_scene_copy(&server->compose->scene, target, image, …)` に `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, ` を足す）。写しで golden と一致を確かめた。WS213 の path なので P1 は変えていない（許可があれば P1 が直す）。
- `plan/ws141/tests/scene-cache-host.c` は main で既に `kwl_scene_copy` の引数が合っていない（5 つで呼ぶ）。今の master の一覧に無いので、試験の整理の基準（2026-10-06）で削除の候補。

## 12. review-1 への対応（2026-10-13、Q1 の決め）

| # | 対応 |
| --- | --- |
| S1 | 実行中の回転と open の経路（`kwl_output_rotated`）の両方で、ROTATED の時は pointer を先に「古い論理 → 古い回転で物理 → 新しい回転で論理」の 1 回の warp で動かし（`kwl_output_pointer_turn`、計算は `kwl_rotate_pointer_place`）、その後に RESIZED の fit（clamp は動かした後なので何もしない）。open の経路は display が `opened_display` と同じ時だけ（switch で別の panel に移る時は今の clamp、L46）。host 試験: 4 つの panel × 16 の角度の組 × 全画素で `kwl_rotate_point`／`_back` と一致、review の 1280x800 の (1000,400) → (400,279)、角の点、外の点の clamp。log `pointer=x,y>x',y'` |
| M1 | `kwl_rotate_select` は直の scanout の判定を「閉じている」の判定より先に（直の scanout の間は output_open = 0）。host 試験に「scanout 中で output_open = 0 → LEAVE」の行 |
| M2 | park の直後に `kwl_cursor_plane_hold`。解除は次の `kwl_cursor_plane_update`（回った出力では plane は空の像で合成に、L24 の順） |
| M3 | (b): 大きさの変わる回転では `kwl_backdrop_destroy` で全部捨てる（§11.2 の 1） |
| M4 | extended の mode で head が開いている時、`kwl_output_select_rotation` は ENOTSUP（log `reason=heads`）。p005 で head の再配置を入れる時に外す |
| M5 | 大きさが同じ回転（90↔270、turn の 180↔0）は turn.image（あれば）・glass・scene・layers・backdrop を作り直さず、turn の角度と swapchain の `image_frames` だけを替える。turn.image が無い所から（0 → turn の 180）は turn.image だけ作る |
| M6 | `--rotate-policy` は試験の image の shot.c だけ（§11.2 の 6） |
| M7 | T1 の B に部分と全体の画素の比較、C に pointer の log、A に起動時の `KWL GLASS size … by=open`。M13 の 0° への落ちは注入口を作らず「未検証」と明記（§11.4） |
| L1 | turn の 2 つの pass の入口の dependency に `COLOR_ATTACHMENT_OUTPUT`・`COLOR_ATTACHMENT_WRITE`。mirror が turn.image を戻す barrier の dst を次の frame の pass と写しの stage に |
| L2 | turn の format の検査に `SAMPLED_IMAGE_FILTER_LINEAR` |
| L3 | 実行時の GPU の vendor で（§11.2 の 7） |
| L4 | 実行中の apply が成功した時に `WHY_RESOURCE` を消す（`output_rotate_turn`） |
| L5 | select は login 画面（greeter）で ENOTSUP（D9） |
| L6 | `regenerate.py` の glslc の版で既存の shader の SPIR-V が変わったら、`kwl_turn_vert` の配列だけを `shaders.h` に差し込む（今回は既存の配列が byte 同一だった） |
| L7 | `turn.c`・`output-rotate.c` を 3 つの Makefile に。FreeBSD の build は未確認 |
| L8 | ws.md の p002b と p011 の行を直した |
| L9 | `POLICY default` で選択が mask の外になったら `rotation_selected = 0`（open の choose と同じ、displays.conf の行は保つ）、0 に戻す apply は新しい mode `KWL_ROTATE_MODE_POLICY`（保存しない、log `by=policy`） |
| L10 | §11.2 の 5 |
| L11 | T1 の手順に「要求を出してから動かす」と最後の `rotate=` の片付け |
| L12 | head の slot には swapchain の pass（`kwl_compose_target_pass`）、turn pass は clip の後、pass の後に bind を忘れる、backdrop の作り直しは `kwl_backdrop_destroy` が `backdrop_set` を NULL に |
| L13 | turn の時も scene は swapchain の写しの能力で開く条件のまま（性能だけの違い、変えない） |

## Event

- 2026-10-13 / ws217-p002b-design-1: P1 が q948 で第 1 版を書いた（source は main `1eb79f9db`）。review-4 の S12（select と apply の 2 つの関数）・M21（arrange と refit）・p002a の残し 4（turn_build と既定の分け方）を入れ、M16 の前提（zedBSD の session は `keiland-desktop` でない）を見つけて試験の policy の渡し方を変えた。コードは書いていない。design-reviewer の review 待ち。
- 2026-10-13 / ws217-p002b-impl-1: P1 が q948 で実装（turn.c・output-rotate.c・turn.vert ほか、§11）。build 4 つと host 試験 PASS。T1 の依頼を Q1 へ。
- 2026-10-13 / ws217-p002b-review-1: design-reviewer の review-1（条件つきで進めてよい）。P1 が S1・M1〜M7・L1〜L13 を実装と phase.md に反映（Q1 の決め: M6 は試験の image だけ、M4 は ENOTSUP、M3 は全部捨てる形）。
