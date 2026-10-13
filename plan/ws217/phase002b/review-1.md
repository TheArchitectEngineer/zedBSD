# ws217-p002b review-1（phase.md b8464d80c の設計の敵対的 review、2026-10-13、Q1 が起動した design-reviewer）

行番号は b8464d80c の tree。P1 の worktree の未 commit の実装（output-rotate.c・turn.c・rotate.h の diff）も参考に読んだ。build・試験なし。

**判定: 条件つきで進めてよい。** 致命的 0・重大 1・中 7・軽微 13。turn pass の芯は正しい（4 つの角度の座標の式、NEAREST で画素の中心、半開の矩形、render pass の互換、bcm2711 は SHADER_READ_ONLY_OPTIMAL を受ける vulkan-target.c:537-545・vulkan-barrier-validate.c:197-215、PRESENT_SRC_KHR は libvulkan が wire で GENERAL に wire.c:695-703、書式の能力 i915 instance.c:652-659・bcm2711 vulkan-query.c:387-390、0° は mask で選ばれず pass と pipeline は遅延で作るので Pi 4 の既定では完全に無効）。段 1〜3（rotate.h、turn の資源と pass、frame の列）は今進めてよい。段 4（select/apply・pointer）は S1・M1・M2・M4 を phase.md に入れてから。M6 は D13 の読み方の確認。p002b の clearance は p002a の clearance を待つ。

## 重大

**S1. 回転した時に pointer が同じ物理の点に来ない（RESIZED の clamp が先に走る）。** §3.2 の 6（RESIZED → output_resized）が 7（ROTATED → output_pointer_turn）より先（phase.md:134-135、open の経路 :138 も同じ）。今の kwl_output_rotated も RESIZED（output-switch.c:374）を ROTATED（:379）の前に走らせ、output_resized は output_pointer_clamp で warp する（:806、:815-829）。1280x800 で 0°→90°、論理 (1000,400) → clamp で (799,400) → 回して (400,480)、正しくは (400,279)。直し: ROTATED で display が変わらない時は clamp を使わず 1 回の warp で「古い論理 → 古い回転で物理 → 新しい回転で論理」、output_resized に「pointer は呼び手が動かした」の引数、switch で display が替わる時（L46）は今の clamp だけ。worktree の output-rotate.c:214-223 は warp を fit の前に置き実行中の経路はもう正しい順、phase.md をこの順に直し open の経路（kwl_output_rotated）にも同じ順を。move の関数を host 試験に入れ端の点 (Wl−1, y) を確かめる。

## 中

**M1. §3.1 の手順の順で直の scanout の最中の 5 に永久に届かない。** 4「出力が閉じている → 保存だけ」が 5「server->scanout != NULL → leave」より先（phase.md:116-117）。直の scanout に入る時 scanout_enter が kwl_compose_output_close を呼び（scanout.c:281-291）output_open = 0（compose.c:577）。worktree の kwl_rotate_select と output-rotate.c:92-99 も open = output_open && !lost。game の直の scanout の最中に ROTATE 90 が来ると保存だけで leave せず game が終わるまで回らない（p001 §4.5・review-4 S13(1) に反する）、host-output-select の表がこの誤りを固定する。直し: scanout の判定を closed の判定より先に、表に「scanout 中で output_open = 0」の行。

**M2. park の中の vkDeviceWaitIdle が cursor の thread を止めていない。** §3.2 の 2 は kwl_compose_park と vkDeviceWaitIdle だけ（phase.md:129）。cursor の thread は同じ compose->queue に submit と present（cursor-plane.c:108、cursor-thread.c:505・555）。既存の close は cursor thread を止めてから device を待つ（compose.c:550-560）。p001 §4.6 A の 4 にも kwl_cursor_plane_hold があった。worktree の output-rotate.c:333-334 にも無い。Pi 4 で policy ANY のまま cursor plane を開いて 0→90 で vkDeviceWaitIdle と cursor thread の queue の操作が外部同期なしで重なる（未定義）、解除の直後に古い位置の cursor が 1 回出ることも。直し: park の直後に kwl_cursor_plane_hold(server)、解除は次の kwl_cursor_plane_update（L24 の順で空の像を出してから）。

**M3. kwl_backdrop_resize（pass を残して UNTRIED に戻す）は VkRenderPass を毎回漏らす。** backdrop_create は毎回 backdrop_pass を呼び（backdrop.c:213）、backdrop_pass に作り済みの確認が無い（backdrop.c:237-）。直し: (a) backdrop_pass の頭に `if (backdrop.pass != VK_NULL_HANDLE) return VK_SUCCESS;` か (b) kwl_backdrop_destroy で全部捨てる（worktree の output-rotate.c:390 は (b)）。phase.md を採った形に。

**M4. extended の head があるまま実行中に回すと配置が無効なまま残る。** head の位置の検査は head を開く時の heads_place だけ（heads.c:1074-1105）、実行中の TURN は head を閉じず再検査は p005。anchor が 1280x800 → 800x1280 で右隣の head との間に 480px の隙間か重なり、pointer が渡れず窓も届かない。直し: p002b では extended で head が開いている時の select を ENOTSUP（log の理由 heads）か、head を heads_stale で開き直して再配置。§7 と T1 の案に。

**M5. 90↔270 と 0↔180（turn）で turn・壁紙・scene・layers を不要に作り直す。** §3.2 の 1 は TURN の steps で常に turn_new と kwl_glass_size_prepare（壁紙の file を main thread で decode し直す）。論理の大きさが同じでも大きな画像が一時に 2 倍（4K で turn 2 枚・壁紙の image と staging 2 組で約 200MB、Venus の host-visible 256MiB を圧迫）、decode の待ち、確保の失敗で回せないことも。直し: RESIZED が無い時は glass の prepare/swap を飛ばす、大きさが同じなら turn.image を使い回し turn.rotation と push constant だけ替える（turn.image の中身は論理なので有効、turn.frame も保てる）、swapchain の側だけ image_frames を 0、scene と layers も大きさが同じなら保つ。

**M6. --rotate-policy=any が製品の binary に入る（D13・規約 §12）。** §4.2 は main.c に option（phase.md:164）、そのうえで「policy ANY は製品に出ない」（:168）。shot.c は test image だけだが CLI の option は製品の /bin/wayland に残る。D13 は「test image だけ --rotate-policy=any」（ws.md:66）。coding-style.md:788-799 は test-only switches を production code に置かない。直し: option の解釈を test image の印（ZEDBSD_TEST_SCREEN_CAPTURE の shot.c 側の hook など）の下だけに置く、か Q1 が D13 の解釈として確かめユーザーに報告。

**M7. 試験で検出できない誤り。** SHOT partial 1 枚の目視では age 2/3 の union や turn の矩形のずれを確かめられない → 部分の frame を swapchain の画像が一巡する以上流し、動きを止めて SHOT partial と SHOT（全体）を続けて撮り byte ごとに比べる（時計の領域は除く）。「pointer が同じ物理の点」を見る log が無い → `KWL OUTPUT rotate … pointer=x,y>x',y'`。QEMU の pointer は絶対の device（推測）で p003 までは写像が回らないので注入の座標がずれる前提で手順を。M13 の失敗（turn を作れず 0° に落ちる）を流す手段が無い → test image だけの注入口（shot.c で次の turn_new を失敗させる）か「未検証」と明記。T1 の 1 の「KWL GLASS の壁紙の大きさが 800x1280」に当たる log が起動時に無い（b8464d80c の glass.c は resize の時の KWL GLASS resize だけ、worktree の KWL GLASS size は prepare の時だけ）→ 起動時の作成にも大きさの行か判定の行を変える。

## 軽微

- L1. turn の 2 つの pass の入口の dependency の srcStage に COLOR_ATTACHMENT_OUTPUT・COLOR_ATTACHMENT_WRITE（pass_load backdrop.c:341-344 と kwl_scene_copy の終わりの barrier scene.c:213-226 と鎖に）。mirror と scene が turn.image を戻す barrier（dst = BOTTOM_OF_PIPE、heads.c の mirror）は次の frame と鎖にならず fence の直列で安全なだけ、実際の dst の stage を書く。
- L2. mirror の blit は VK_FILTER_LINEAR（heads.c:1233）、turn の format の検査に SAMPLED_IMAGE_FILTER_LINEAR を足す。
- L3. KWL_TURN_DEFAULT を __aarch64__ で決めると WS112 の Linux arm64 package も 0（main.c:1275-1282）、worktree の注記「the only arm64 build」も誤り。device（bcm2711）を見て実行時に決めるか build の設定で。
- L4. WHY_RESOURCE を実行中の apply が成功した時に消すことを phase.md に（実装は output-rotate.c:340 で消している）。
- L5. select は greeter（D9）を拒むと書く（実装は server->greeter で ENOTSUP）。
- L6. regenerate.py は全部の shader を compile し直し、glslc の版が違うと quad と panel の SPIR-V も変わる。差分が turn.vert 以外にも出たら turn.vert の配列だけを差し込む手順を phase.md に（受け入れの行 phase.md:179 は失敗を見つけるだけ）。
- L7. 新しい file（turn.c・output-rotate.c）を Makefile・Makefile.linux・Makefile.freebsd と host 試験に足すことを §5 に。FreeBSD の build は p002a から未確認。
- L8. ws.md の p002b の行（ws.md:33）が古い（turn.frag、kwl_output_rotate、cancel、persist、backdrop の retire）。p011 の行の KWL_TURN_DISABLED も名前が違う。
- L9. POLICY default で 0 に戻した後も rotation_selected は mask の外の値のまま（§4.1 と食い違う）、log の by=user も誤解を招く → by=policy。
- L10. §3.2 は output_resized（refit・arrange・configure）を park の中で走らせ park を長くする。実装（unpark の後）の方がよいので phase.md を直す。
- L11. SHOT partial は要求の後に damage が無いと 3 秒で timeout（shot.c の SHOT_WAIT_MS）、手順は「要求を出してから動かす」。T1 は 1 つの QEMU で束ねるので最後に POLICY any と ROTATE 0 で rotate= の行を消す。
- L12. 実装の注意: kwl_compose_target_pass は head の slot に対しては常に compose->pass を返す、turn pass は clip_count = 0 の後に記録、turn pass の後で kwl_compose_bind_forget、backdrop を作り直す時は backdrop_set を NULL に。
- L13. scene は swapchain が transfer の両方向を持つ時だけ開く（compose.c の scene_supported）、turn の時は写しの元が turn.image なので条件は要らない（性能だけ）。

## Q1〜Q4 への意見

Q1（turn.vert と quad.frag の NEAREST）賛成: 式は 4 つの角度とも kwl_rotate_point_back と一致、quad.vert の mix() では軸を入れ替えられないので新しい頂点の shader が要る、i915 の compile の危険は M13 の 0° への落ちが受けるがその経路を試験で通す手段が要る（M7）。Q2（park の中で作り直す）賛成、ただし大きさが同じ時は作り直さない（M5）、backdrop は M3 の形で。Q3（shot.c の POLICY・ROTATE）賛成、zedBSD の session は /etc/keiland/session → exec /bin/wayland --session（session.sh:17、session.c:668-673）で M16 の訂正は正しい、ただし --rotate-policy の CLI は M6。Q4 p002b を単独で賛成、p002a の残りを同じ QEMU に束ね M7 の部分と全体の比較を入れる。

## 確かめていない（推測）

Venus の host が turn.image に要る書式の能力を返すか、i915 の native compiler が turn.vert を通るか、4K で Venus の aperture が足りるか、os_paused の間（VT の切り替え）に output_open が 1 のまま select が来た時（閉じた扱いが安全と推測）、QEMU の pointer device が絶対か。
