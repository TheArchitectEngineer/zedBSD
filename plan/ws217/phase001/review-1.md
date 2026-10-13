<!-- awesome-plan project=zedbsd record=ws217-p001-review-1 -->

# ws217-p001 第 1 版の敵対的レビュー（review-1）

- 対象: [phase.md](phase.md) 第 1 版（2026-10-13 P4）。照合した source は main `f50beb833`（設計が引く agent/p4 `28e5fe929` は main の祖先。`userland/desktop/wayland` の差分は keyboard 系と `kwl.h:2380` 以降だけで、設計の file:行 はほぼ ±1 行で一致した）。
- 文書・source は変えていない。`.internal/` は読んでいない。build・QEMU は流していない（設計 review のため）。
- 重大度: 致命的（このまま実装すると壊れる・直すまで p002 に入れない）／重大（受け入れ・規則・主要な経路を外す）／中（局所の誤り・欠落）／軽微。

## 致命的

### C1. `kl_system_displays_v1` の「version 20」は既に使われた manager の version で、displays の object は manager の version を継ぐ（§6.1・§6.2）

- 根拠: `KL_SYSTEM_SINCE_*` は全部 manager の版の通し番号（`libkeiland/system/kl-system-protocol.h:366-422`、`KL_SYSTEM_SINCE_SHOWN 19U`・`KL_SYSTEM_SINCE_MAIL_ALLOWED 20U`、`KL_SYSTEM_MANAGER_VERSION 33U` は `:324`）。displays の object は「made at the manager's version」（`system-protocol.c:521-528`）、compositor は `object->version >= KL_SYSTEM_SINCE_SHOWN` で判定（`wayland/displays-shell.c:161`）、libkeiland は `system->manager_version < KL_SYSTEM_SINCE_SHOWN` で ENOTSUP（`libkeiland/system/system.c:2289`）。
- 起こること: 「version 20 以上の client に `rotation` event」は今の全 client（v33 で bind）に当てはまり、`rotation` を知らない古い libkeiland（events 3 個の表）に opcode 3 の event が届いて接続が protocol error になる。逆に新しい libkeiland の「20 未満なら ENOTSUP」は回転を知らない v33 の compositor にも `set_rotation`（opcode 4）を送り、compositor は `EPROTO`（`displays-shell.c:168`）で client を切る。「`KL_SYSTEM_MANAGER_VERSION` は 33 のまま」は成り立たない。
- 直し方: `KL_SYSTEM_SINCE_ROTATION 34U`（番号は Q1 が KL_VERSION と同じく割り当て）、`KL_SYSTEM_MANAGER_VERSION` を 34 に上げ、`kl_system_displays_v1_interface` の version を `KL_SYSTEM_SINCE_ROTATION`・requests 5・events 4 に、両側の判定を manager の版で行う。§6.1 の表と ws.md の「v20」を全部直す。

### C2. `server->width/height` は「論理」にできない: 今は swapchain と mode を開く要求の大きさを兼ねている（§0・§4.1・§4.6）

- 根拠: anchor の surface は `vkdemo_display_open_on(..., server->width, server->height, ...)`（`compose.c:307`）、mode の refresh も `compose_refresh(server, display, server->width, server->height, ...)`（`compose.c:1389`）。`kwl_output_switch` は失敗の時に `old_width = server->width`（論理になる）で開き直す（`output-switch.c:200-235`）、成功後の `output_resized` の判定も `width != old_width`（物理と論理の比較、`:259`）。game mode は `kl_backend_scanout_open(server->backend, server->width, server->height, ...)`（`scanout.c:287`）、抜けた後は次の pass の `enter_window_mode` → `kwl_compose_output_open` → `prepare`（`display.c:607-613`、`compose.c:385`）で同じ `server->width` から開き直す。
- 起こること: 回転した anchor の開き直し（切り替えの失敗の戻り、game mode の中で回転した後の復帰、`--width/--height` の再 open）で、幅と高さを入れ替えた mode を探して `vkdemo_display_open_on` か `compose_refresh` が失敗し、`KWL OUTPUT none`（黒い画面、`output-switch.c:235-238`）になる。設計は §1.1 で「anchor の swapchain は `server->width/height` で開く」と書きながら、その書き手と読み手を分けていない。
- 直し方: 物理の mode の大きさを別の field（例 `compose->mode_width/height`、head は `head->mode_width/height`）に持ち、`compose.c:307, 1384-1389`、`output-switch.c:200-235, 259`、`scanout.c:287`、`heads.c:837`（head も open の後に `head->width` を論理に変えるなら同じ）を mode の field に移す。`server->width/height` を書く所（`compose.c:1384`、`output-switch.c:218, 230`、`main.c` の `--width`）を全部表にし、「物理を書いてから回転を適用して論理にする」1 つの関数に通す。p002 の受け入れの grep の表（§10）に `server->width` の**書き手**も足す。

## 重大

### S1. Pi 4 では turn pass の sample の前に raster→UIF の変換が要り、「GPU だけ」「1 pass」が成り立たない（§0・§7、Guardrail 2026-10-12）

- 根拠: bcm2711 は texture を strict UIF で sample し（`src/drivers/gpu/bcm2711/native-texture.c:8, 119`）、raster の画像は snapshot に変換する。変わった箱が 256 KiB 以下なら **CPU** が変換し、それより大きければ TFU（`native-storage.c:25-26, 753-808`）。Guardrail は毎 frame の経路で CPU が「tiling の並べ替え（raster→UIF など）」をすることを禁じ、glass は「WS141 の案 (A)（GPU の描画の出力を UIF で持つ）がその前提」とする（`plan/guardrail.md:230-234`）。その WS141 p015 は planning（`plan/ws141/ws.md:79`）。
- 起こること: Pi 4 で回転すると、cursor や文字の小さな damage ごとに kernel の CPU が turn.image を UIF に並べ替え（規則違反、Pi の CPU 負荷）、大きな damage では TFU がもう 1 回全体を読み書きする。§7 の 0.3 GB/s（1024x600）は少なくとも約 2 倍。p006 の Pi の計測は turn pass ではなく変換を測ることになる。
- 直し方: §0・§7 を「Pi 4 では WS141 p015 の後に限り GPU だけ」と直し、WS141 p015 を Pi 4 の受け入れ（完了の条件 4・5）の依存として §12 に書く。p015 の前は Pi 4 を「動く（CPU の変換あり）と記録」に留める判断を D7 としてユーザーに出す。§7 の見積もりに変換の分を足す。

### S2. turn.image の layout・usage・同期が設計に無く、既存の pass と barrier は全部 swapchain の画像を前提にしている（§4.1・§4.2・§4.4）

- 根拠: 主の render pass は `finalLayout = PRESENT_SRC_KHR`（`compose.c:1723-1724`）、外部 dependency は `COLOR_ATTACHMENT_OUTPUT` → `COLOR_ATTACHMENT_OUTPUT` だけ（`compose.c:1734-1739`）、load pass は `PRESENT_SRC_KHR` から（`backdrop.c:326-327`）。scene の写しと戻し（`scene.c:160, 213`）、shot（`shot.c:298, 334`）、mirror の元の barrier（`heads.c:1196`）も `PRESENT_SRC_KHR` を前提にする。scene の戻しは buffer→image の copy（`compose.c:2779`）なので turn.image に `TRANSFER_DST` が要るが、§4.1 の usage は COLOR_ATTACHMENT・SAMPLED・TRANSFER_SRC だけ。
- 起こること: turn.image は PRESENT_SRC にならないので、そのまま使うと layout の不一致（未定義の動作）。frame N の turn pass（fragment で読む）と frame N+1 の scene pass（書く）の間の WAR の依存が無く、frame が重なると読みかけの絵を上書きする。scene の戻しは usage 違反。
- 直し方: turn.image 用の render pass 2 つ（clear と load、`initialLayout`/`finalLayout` = `SHADER_READ_ONLY_OPTIMAL`、外部 dependency の src に `FRAGMENT_SHADER`・`TRANSFER`）を作り（format が同じなので pipeline は互換のまま）、`scene.c`・`backdrop.c`・`shot.c`・`heads.c` の barrier の old/new layout を「target の layout」を返す helper から取る。usage に `TRANSFER_DST` を足す（head の mirror の宛先にもなる）。format は `compose->format` で、`vkGetPhysicalDeviceFormatProperties` の optimal の SAMPLED・COLOR_ATTACHMENT・BLIT_SRC/DST を確かめ、無ければ UNSUPPORTED。sampler は nearest か `texelFetch`（90° で画素を混ぜない）。

### S3. cursor の plane の設計（§4.5）は既存の code と 5 点で食い違い、試験でも検出できない

- 根拠と起こること:
  1. 矢印と shape（`plane_fixed`）は copy を経ずにそのまま box に載る（`cursor-plane.c:345-354`）。「copy の時に回す」では一番よく出る cursor が回らない。
  2. copy と publish の同一性の判定（`cursor-plane.c:712-716` の source・generation・version、`:771-778`）に回転が無いので、像が同じまま回転だけ変わると publish されない。
  3. M が回した hotspot を publish し、C が `snapshot.x - hotspot_x`（`cursor-thread.c:221-225`）を論理の左上として写すと、回転を 2 回数える。式は「論理の矩形（回していない hotspot）を物理へ写す」か「物理の点 − 回した hotspot」のどちらか一方。
  4. 「回転の変更は park の中（C は parked）」は誤り: `kwl_compose_park` は R だけを止め（`compose.c:1002-1027`）、`kwl_cursor_plane_hold` は次の `kwl_cursor_plane_update` で解ける（`cursor-plane.c:314-317`）。
  5. plane の extent は `min(maxSrcExtent, 64)` で正方形とは限らない（`cursor-plane.c:480-481`）。
  6. cursor の plane を使うのは Pi 4 だけ（§4.5 の自分の注記）で、§11.4 は Pi の速さしか測らず、QEMU（venus は cursor 無し）と 5330（plane 未使用）では何も検出できない。
- 直し方（推奨）: この WS では**回転した出力で cursor の plane を使わない**（`plane_state` が `rotation != 0` で -1、合成の cursor に落とす）。CPU の例外も hotspot の式も要らなくなる。Pi 4 は「動くか記録」なので、plane の回転は Future Work（WS214 の C の持ち主の移しと一緒）に送る。残すなら 1〜5 を設計に書き、Pi の試験に「4 つの向きで cursor の先端が押した所に合う」を足す。

### S4. 段の依存と受け入れが矛盾する（§12）

- 根拠と起こること:
  - p002 の受け入れ「T1 の 11.2 の 1・4・5・6」の 1 は「Settings → Display → 90°」、5 は Settings で保存した `rotate=` の再起動後の保持で、どちらも p004 の成果。p002 単独では満たせない。
  - p004 を「p003 と並行可」にすると、p004 が先に main に入った時点で利用者が Settings から回せ、touch・pen（p003 の前は anchor の物理に scale のまま）・Pi の cursor の plane が回転に合わない状態が main に出る（main は UAT の source）。p002 と p003 の間の Pi の cursor の plane も同じ。
- 直し方: p002 の T1 の確かめは `--rotate=N` と、`plan/ws217/tests/` に置いた `displays.conf` の `--file` 複写（AGENTS の image の作り方）で行う形に書き直す。p004 の依存に p003 を足す（または p004 の button を p003 の merge まで出さない）。p002 で S3 の「回転時は plane を使わない」を入れる。

### S5. game mode の判断の根拠が誤りで、回転を game mode の中で受けた時の扱いが無い（§4.5・§9 D6）

- 根拠: `size_matches` は client の buffer を `server->width/height` と比べる（`scanout.c:187-189`）。設計の後は `server->width/height` が論理なので、論理の大きさで描く正しい fullscreen の client は **一致する**（§4.5 の「今でも偽になる」は逆）。180° は回転前から一致する。game mode の間は swapchain が閉じている（`scanout.c:284-285`）。
- 起こること: `rotated` の fact が「log のため」に格下げされると、90/270/180 のどれでも client の buffer が回さずに scanout される。game mode の間に `set_rotation` が届くと、閉じた出力の上で turn を作り直そうとする（§4.6 は output が開いている前提）。
- 直し方: `rotated` を正しさのための必須の fact と書き直し、`scanout-rules.c` の host 試験（`kwl_scanout_decide` の 4 つの向き）を §11.1 に足す。`kwl_output_rotate` は game mode なら先に `kwl_scanout_leave` し、出力が閉じている・lost の時は config だけ変えて開く時に適用する、と §4.6・§8 に書く。

### S6. 矩形の写像の式が半開区間で 1 px ずれ、T1 の shot ではそれを検出できない（§2・§4.3・§11）

- 根拠: §2 は点の式 `(Wp−1−y, x)` で「4 隅を写して正規化」とする。damage・scissor・clip（`compose.c:2600-2627`）は半開の矩形 `[x0,x1)×[y0,y1)`。90° の正しい像は `X ∈ [Wp−y1, Wp−y0)`、点の式で端を写すと `[Wp−1−y1, Wp−1−y0)` で 1 列ずれる。§2 の逆写像「90 と 270 を入れ替えた式」は Wp/Hp をそのまま使うと 90 の逆が `(Y, Hp−1−X)` になり、Wp≠Hp で誤る（正しくは 90 の逆 `(Y, Wp−1−X)`、270 の逆 `(Hp−1−Y, X)`）。SHOT は「次の frame を全体で描く」（`shot.c:22-24`）ので、T1 の撮影は部分の turn pass を一度も通らない。
- 起こること: 部分の frame で毎回 1 列古い画素が残る（cursor の跡や文字の端の縦線）。host 試験が同じ点の式を oracle にすると通ってしまい、QEMU の shot でも見えない。
- 直し方: `rotate.h` に点（画素）と矩形（半開、`Wp − y1` の形）を別の関数として式で書き、host 試験は「矩形の像が矩形の各画素の点の像の集合と一致する」を全向き・非正方形で総当たりにする。T1 には部分の frame を撮る手段（例 SHOT に「全体を描かない」形を test image だけに足す、または `KWL DAMAGE` の物理の矩形の行を log に出して scenario で照合）を足す。

## 中

### M1. §4.3（age の union での描き過ぎ）の評価

- 正しさ: 正しい。turn.image は毎 frame その frame の damage を必ず含む矩形で描かれるので常に最新で、swapchain の画像の age の union を回して写せば画像は揃う。ただし前提は「anchor の damage を記録した frame は必ず turn.image に描く」で、描かずに `image_frames` を進める経路（`compose_region` は描く前に `image_frames[image] = frame` を書く、`compose.c:2571-2574`）で present が落ちた時も同じ前提が要る。設計にこの不変条件を書く。
- 費用: scene の pass の描き過ぎは 0° の今と同じ（今も union を描く）で退行ではない。ただ Pi では scene の pass が一番重く、turn.image が残る今回は「scene は今の frame の damage（`history[slot]` にもうある）、turn pass は union」と 2 つに分けるのは小さな変更で、S1 の UIF 変換の箱も小さくなる。第 1 版で後回しにする理由は弱い。分けるなら scene の写しと戻し（`compose.c:2779, 2872`）の rects も今の frame の分になる。
- 70% の判定（`compose.c:2637`）と clamp（`:2600-2607`）は論理の target の大きさで（面積は同じだが clamp は違う）。

### M2. I の受け箱の「1 区間」の主張が崩れる（§5・§4.6 の 4〜5）

- 根拠: `output_resized` の `kwl_pointer_warp` は**新しい矩形を warp の区間で先に書く**（`output-switch.c:682-683`、`cursor-box.c:171-201`）。rotation と物理の大きさは §4.6 の 5 の `kwl_input_sync` → `kwl_cursor_box_outputs`（`input.c:505-507`）で後から書く。
- 起こること: 4 と 5 の間に I が QEMU の tablet の報告を処理すると、新しい論理の矩形と古い rotation で写し、warp の番号はもう新しいので捨てられない（`input.c:1470-1471`）。pen を動かしている最中なら 1 点飛ぶ。
- 直し方: 矩形・rotation・物理の大きさを 1 つの struct（例 `struct kwl_cursor_geometry`）にし、`kwl_cursor_box_warp` と `kwl_cursor_box_outputs` の両方がそれを丸ごと書く。warp の move の callback（`output_pointer_clamp`）に古い回転と新しい回転を context で渡し、「同じ物理の点」を warp の中で計算する。

### M3. pen の傾き（tilt）を回していない（§5）

- 根拠: `ABS_TILT_X/Y` は device の軸のまま client へ送られる（`tablet.c:592-598, 713`）。Notes は傾きを stroke に記録する（`notes/document.c:1653-1656`、`notes/encode.c:22, 434`）。
- 直し方: 位置と同じ回転を傾きの vector に掛ける（90: `(tx,ty) → (ty, −tx)` の向きは §2 の式から導いて host 試験で確かめる）。peninject は傾きを注入できるので（`userland/tests/peninject/main.c:1178`）11.2 の 3 に入れる。

### M4. 画面の無い pen tablet（机の上の板）まで回すと向きが狂う（§5）

- 根拠: `tablet.c` は device の property を読まず、全部を anchor に写す（`tablet.c:1006-1015`）。
- 起こること: 縦にした LCD と、机に置いた普通のペンタブレットを使うと、ペンを右へ動かすと cursor が上へ動く。
- 直し方: 回転を写すのは `INPUT_PROP_DIRECT` の device（touch screen・液晶ペンタブレット）だけ、と §5 に書く（backend の absinfo と同じ所で property を読む）。読めない backend では今の挙動を D に出す。

### M5. config の適用の経路が既存の code と合わない（§4.6 の「起動時」）

- 根拠: `kwl_heads_config_load` は「compositor の一生に一度」で 2 回目は何もしない（`heads.c:132-136`）。起動時は `compose_targets`・`kwl_scene_open` の後に呼ばれる（`compose.c:440-459` の後の `:472`）。
- 起こること: 「切り替えと hotplug の再 open で同じ経路で `rotate=` を適用」は 2 回目に読まないので適用されない。起動時は物理の大きさで targets・scene を作ってから作り直す（無駄で、失敗の経路も 2 倍）。
- 直し方: `compose->config`（memory）から key の回転を引く関数を分け、`compose_output_open_parked` の `compose_targets` の**前**（swapchain の後）で引いて論理の大きさを決める。config の読み込みはその前（`kwl_compose_output_open` の入口）へ移す。

### M6. 回転の失敗の戻し方が「先に書き換えてから作る」順（§4.6 の 3・8）

- 起こること: `server->width/height` を書き換え、turn・framebuffer・scene・backdrop・layers・壁紙を作る途中で memory が尽きると、戻すために古い大きさで全部を作り直すことになり、それも失敗し得る。
- 直し方: 新しい大きさの資源を全部先に作り（失敗なら新しい物だけ捨てて UNSUPPORTED）、揃ってから入れ替えて古い物を R の step 1 で捨てる。backdrop は一度だけ作る状態（`backdrop.c:68-71` の `BACKDROP_UNTRIED`）なので、作り直しの状態の戻しも書く。1080p で turn 8 MB・scene 8 MB・layers・壁紙と、入れ替えの間は 2 倍持つ見積もりを §7 に。

### M7. mirror の「正立」（§4.4）

- 意味: 「各 display が自分の回転に対して desktop を正立で見せる」は筋が通り、横倒しを写すより正しい。ただし GNOME 等の mirror は「同じ論理の monitor を共有し回転も 1 つ」で、意味が違う。mirror の中で head ごとの回転を許すかを Settings の挙動（mirror の時に row の回転を出すか）と合わせて D に出す。
- 欠落: fit は物理を使う（`heads.c:1200` の `kwl_displays_fit(compose->output.width, ...)`）ので論理の大きさに変える。元の barrier は `PRESENT_SRC_KHR` 固定（`heads.c:1196-1197, 1218-1219`、S2）。「元を turn.image にする」と mirror は readback の無い swapchain でも動くようになり、0° と回転時で mirror の可否が変わる（log の `mirror unsupported`、`heads.c:1220-1223`）。

### M8. libkeiland の公開 API の変え方（§6.2・§4.7）

- `struct kl_display` の末尾に足すと `kl_system_displays_get` は新しい大きさで `memcpy` する（`libkeiland/system/system.c:2161-2163`）。libkeiland は共有 library（`libkeiland.so`、`libkeiland/Makefile:71`）で、古い大きさの配列で build した app は溢れる。今の利用者は Settings だけ（`settings/page-display.c:745`）だが、Linux・FreeBSD の package で libkeiland と app の版がずれると壊れる。別の getter（`kl_system_displays_rotation(system, key, &rotation)`）なら struct を変えずに済む。
- `kl_window_output_mode()` は「first screen's current mode」と文書化された API（`libkeiland/ui/window.c:1010-1015`）。中身を入れ替えると mode の意味が変わる。Settings は About だけでなく Display の頁の plain の表示（`settings/page-display.c:401-426`）でも使う。`kl_window_output_transform()` を足すか、入れ替えを Settings の側で行う。

### M9. 出力が transform を告げると、`set_buffer_transform` の EPROTO が実際に起こり得る（§4.7・§3 D）

- 根拠: 0 以外は client を切る（`protocol.c:1036-1047`）。今までは transform が常に 0 なので合わせて pre-rotate する client はいなかった。
- 推測: 移植の package（Qt6・SDL・mpv など）に、出力の transform を見て buffer を回し `set_buffer_transform` を送る物があれば、回転した途端に切断される。確かめていない。
- 直し方: 対象の package を調べて結果を §1.5 に書く。最低でも EPROTO にせず log を出して受け、合成で解く（D 案）を Future Work でなくこの WS の判断に出す。

### M10. 回転の瞬間の入力の途中の状態（§4.6・§5）

- 指が触れたまま、pen が下りたまま、窓の move/resize の grab の最中に回転すると、`start_x/start_y`（`touch.c:1000-1004`）など古い論理の座標と新しい座標が混ざり、gesture が誤って働く。回転の時に touch を cancel（wl_touch.cancel）し、grab を終える、と書く。

### M11. 帯域の見積もり（§7）

- S1 の UIF 変換が抜けている。extended の head は毎回全体を描く（`heads.c:1262-1265`）ので、回転した head は毎回全画面の turn pass。WS214 の 1.5 GB/s は HVS の scanout の予算（`plan/ws214/phase001/phase.md:51`）で、v3d の描画の DRAM の帯域とは別物。並べず別に書く。
- 推測: 線形の画像を 90° で sample すると texture cache の当たりが悪く、実効の読みが 1 回より多くなり得る（v3d の TMU の cache の大きさは確かめていない）。p006 の計測の項目に入れる。

### M12. WS213 p004 は cleared ではない（Prerequisites・§12）

- code は main に入っている（`f63ea685d` は main の祖先）が、Phase は in-progress（`plan/ws213/phase004/phase.md:6`）で、kernel の K-D 等の判断待ち。park と受け箱の契約が変わり得る。「WS213 p004 の main の統合（済み）」を「scoped な出力（park・`kwl_cursor_box`・I の warp の番号）に依存、WS213 が契約を変えたら追従」と書き直す。

### M13. 向きの約束の伝え方（§2・D1）

- 内部の数学は一貫している（論理の上辺 → 物理の右辺、Wayland の `TRANSFORM_270`。Wayland の `set_buffer_transform` の定義から導いて一致を確かめた）。ただし「90°」は**絵**を panel の上で時計回りに回すことで、panel を**反時計回り**に倒した利用者に合う。利用者は LCD を物理的に倒すので、D1 は「どちら向きに倒した時にどれを押すか」の絵で決める。11.3 の「LCD を物理的に縦置き」にも倒す向きを書く。
- 表し方が 3 つある（file は度数 0/90/180/270、protocol と内部は 0..3、Wayland は反時計回りの 0..3）。§2 の「内部もこの度数（= 0,1,2,3）」と §6.4 の「file は度数」が食い違う。内部の 1 と Wayland の 1 は逆の向きなので、取り違えても型で分からない。内部は度数そのもので持つか、名前に `_CW` を入れる。

### M14. 試験の計画の欠落（§11）

- 完了の条件 2 の App Home 以外（スクリーンキーボード、Wiseview、窓の refit）と、条件 4 の 5320 が無い。
- 回転と出力の切り替え・hotplug・lid（C2 の経路）、game mode の拒否（S5、特に 180°）、logout と login（greeter は利用者の `~/.config/keiland/displays.conf` を読まないので login 画面は回らない、`heads.c:57-61, 145-151`。製品の制限として記録するか D に出す）、lock 画面、pen の傾き（M3）が無い。
- test image の config.mk（`CONFIG_INPUT_TEST_INJECT=y`（touchinject・peninject の前提、`userland/tests/touchinject/main.c:9-12`）、`ZEDBSD_TEST_SCREEN_CAPTURE=y`）を `plan/ws217/tests/` に置くことを書く（AGENTS の image の作り方）。
- §11.1 は `plan/ws113/tests/host-displays.c` と `plan/ws213/tests/` を拡張するが、他の WS の plan を変えるのは担当の範囲外（AGENTS の subagent の修正可能範囲）。回帰に残すなら `plan/tools/` へ移して master.md の Tools に登録する（Q1 の作業）か、WS217 の tests/ に複写する。

## 軽微

- L1. §10 の「(§1.1 3.16)」は参照が壊れている。
- L2. Settings の snapshot で「どこにも出ていない display」は物理の mode を送る（`displays-shell.c:434-441`）。保存した回転を当てて論理で送る（Settings の図と on にした時の配置が合う）。off・limited の display への `set_rotation` の扱い（保存して出す時に適用）も書く。
- L3. displays.conf: 古い build は `rotate=` を skip するが、その build が保存すると行が消える（往復で失われる）。読みは 4096 byte まで（`heads.c:60, 143-147`）、切断した display の `rotate=` を残すなら上限（`KWL_DISPLAYS_PLACES`）を書く。値は度数以外（例 `rotate=KEY 1`）を無効として host 試験に。
- L4. 配置の再検査の「その出力だけ置き直す」は、anchor が回った時（anchor は原点）は head を動かす、と明記する。
- L5. 180° は `vkCmdBlitImage` の offset を逆にすれば shader 無しでできる（任意）。
- L6. §11.2 の「screendump」は zedBSD の image では keiland-shot（`shot.c`）。QMP の screendump の例外は Linux・FreeBSD の guest だけ。D5 の `--width=600 --height=1024` は物理が縦の出力を試すだけで回転の経路を通らない。1024x600 に `--rotate=1` で測る。
- L7. shot の `SHOT` の答えと `DISPLAYS` の行に `rotation=` を足すと、試験の道具が論理の PNG に戻せる。
- L8. `kwl_output_view` に物理の大きさと回転の field が要る（mode の event は物理、geometry は回転）。head の mode は `head->output.width/height`。
- L9. `set_rotation` の key が無い時の答えは、既存の `set_shown` の「not found」（libkeiland の ENODEV）と揃える（§6.1 は INVALID）。BUG-266 の「move は最初の frame で確かめる」と同じく、result を最初の frame の present の後に返すかも決める。
- L10. p007（規約の全文）は p006 の直しの後にも当たるよう、依存に p006 を足すか「p006 で code が変われば再確認」と書く。

## 確かめた点（source と照らして正しかった所）

- §1.2: 投影の行列は無く、`compose_quad_part`（`compose.c:2496-2516`）と `glass_shape_draw`（`glass.c:1910-1962`）は `server->width/height`（head では view）で NDC を作り、`panel` の `screen` も同じ値（`glass.c:1961-1962`、`batch.c:156-168, 387-398`）。viewport を論理の target にすれば shader に触れずに済む、という案 A の核は成り立つ。
- `compose->output.width/height` を読む所は §1.2・§4.2 の列挙で全部（grep で確かめた: `compose.c:458-459, 2016-2017, 2604-2607, 2637, 2796-2818`、`backdrop.c:71, 132`、`layer-cache.c:101-102`、`heads.c:1204-1205`、`shot.c:432-438`）。`cursor-plane.c:509-510`（mode の照合）は物理のままが正しい。
- §1.3: libvulkan は IDENTITY だけ（`wsi.c:125, 620-624`、`wsi-display.c:584-585`、`wsi-swapchain.c:1213-1215`）。i915 の `hw.rotation` は `MODE_ROTATE_0` の 2 か所（`display/plane.c:293, 378`）、`plane_count = 1`（`display/display.c:2761, 3377`）、linear だけ（`display/internal.h:2525-2530`）。HAL・UAPI の変更は要らない（案 A）。
- §1.4: touch（`touch.c:964-969`）・pen（`tablet.c:1014-1015`）・QEMU の tablet（`cursor-box.c:131-159`）は anchor へ scale。I は park しない（受け箱の lock だけ）。
- §1.5: wl_output の geometry の transform は byte 44 で 0（`protocol.c:669-687`）。fullscreen の configure と bounds は `server->width/height`（`protocol.c:1729-1731, 1838-1839`）。`wl_compositor` は v4（`protocol.c:56`）。
- §2: 90 → `WL_OUTPUT_TRANSFORM_270` の変換は Wayland の定義（buffer_transform の逆を当てると surface、direct scanout の buffer = 物理）と一致する。
- §4.4: extended の head は毎回全体を描き（`heads.c:1262-1279`）、view を立てて同じ quad の関数で描く（`heads.c:1296-1300`）。
- §6.4: 知らない key の skip（`displays.c:654-656`）と version の照合（`:595-603`）。

## 確かめていない点（推測を含む）

- libvulkan（zedBSD の wire と kernel の driver）が subpass の外部 dependency と layout の遷移を実際にどこまで守るか（S2 が実害になるかは driver 次第）。
- bcm2711 の change ring（`bcm2711_v3d_memory_changes`）が GPU の描画で書いた箱を知るか（知らなければ毎 frame TFU で全体を変換、S1 の悪い方）。v3d の TMU の cache の大きさ（M11）。
- Qt6・SDL・mpv などの移植が output の transform を見て `set_buffer_transform` を送るか（M9）。GTK4 の monitor の geometry の入れ替えは設計の記述を信じ、package の source は読んでいない。
- スクリーンキーボード・Wiseview・App Home が `output_resized` だけで縦長に配置し直すか（完了の条件 2）。sleep と resume、lid の経路で出力が開き直るか（C2 の経路が他にもあるか）。
- `shot.c` の `DISPLAYS` が `server->width` を返すか（L7 の前提）。
- WS214 p005 の設計（未投入）の中身。Linux・FreeBSD の libvulkan-compat で turn pass の sample が動くか。
- HVS の flip の bit（`bcm2711/list.c:329-332`）と Venus の op の NULL（`venus/display.c`）は設計の表を信じ、読んでいない。
