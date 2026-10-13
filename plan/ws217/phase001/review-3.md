<!-- awesome-plan project=zedbsd record=ws217-p001-review-3 -->

# ws217-p001 第 3 版の敵対的レビュー（review-3）

- 対象: [phase.md](phase.md) 第 3 版（2026-10-13 P2）と [ws.md](../ws.md) の「ユーザーの決定」（末尾の game mode の決定を含む）。照合した source は main `c9e00c675`（設計が引く `217936c4c` は main の祖先。`217936c4c..c9e00c675` の差分は WiFi・rpi4 の SDIO・`gpu-op.h`・libvulkan の context/device/memory/wire・i915 の render で、設計が引く `userland/desktop/wayland`・`include/uapi/gpu-display.h`・`src/drivers/gpu/gpu.c`・i915 の display・bcm2711・libvulkan の wsi は同じ）。
- 文書・source は変えていない。`.internal/` と GPL の source（Linux vc4 など）は読んでいない。tree の中の MIT の i915 の参照（`plan/ws031/linux-parity/linux-reference/ubu-i915-src/display/`）は読んだ。build・QEMU は流していない。
- 重大度は review-1・review-2 と同じ: 致命的（このまま実装すると壊れる・直すまでその段に入れない）／重大（受け入れ・規則・主要な経路を外す）／中（局所の誤り・欠落）／軽微。番号は review-2 から続ける（C5〜、S10〜、M11〜、L26〜）。

## 判定（p002 と p009 を始めてよいか）

- **p002（全体・p002b）: まだ不可**。致命的 2 つ: C5（open の経路の比較が論理の大きさだけなので、0↔180 と 90↔270 の変更で入力の写像・cancel・Settings への告知が走らない）と C6（ws.md 末尾の新しい決定「90/270 の出力で game mode に入ると回転を外す」が未反映で、p002 の範囲の scanout の fact・open の再計画・`kwl_output_rotate` の形がそれで変わる。素直に入れると frame ごとの往復の回転と設定の上書きが起こる）。第 4 版で C5・C6 と、p002 に入る M11〜M13 を直せば始めてよい。
- **p002a（turn 無し、0 以外は UNSUPPORTED の refactor と `rotate.c`）: 今の第 3 版で始めてよい**。ただし open の比較は最初から「（論理の大きさ, rotation）」で書く（C5）。0 以外を出さないので C6 の影響を受けない。Q1 が p002a/p002b に割る判断をした時だけ。
- **p009: 最初の段（Intel の PRM の確認）は今始めてよい。実装（UAPI・i915・libvulkan）は第 4 版で S10・S11・M17 を直した後**。S11（`currentTransform`・`preTransform` の約束）は p009 と p010 の間の interface なので、p009 の実装の前に決める。どれも文の直しで済み、新しいユーザーの判断は要らない（S11 は Q1 の技術判断でよい）。
- **ユーザーに出す判断は 1 つ**（D18、§C6 の 7）: game mode の derotation を「video」の content type（Videoplayer の fullscreen、`videoplayer/main.c:240`）にも当てるか。

## 致命的

### C5. 180° と 90↔270 の変更が open の経路を通ると、入力の写像・指の cancel・Settings への告知が走らない（§4.6 の 1、§4.6「起動時と open の全経路」）

- 根拠: §4.6 の 1 は scanout の回転が変わる時（0↔180）に close → open だけを通し「以下 2〜6 は通らない（open の中で済む）」とする。open の全経路の規則は「open の前の論理の大きさと後の論理の大きさを比べ、**違えば** `output_resized(server, 1)` と `kwl_outputs_changed`・`kwl_input_bindings_resync`・`kwl_displays_tell` を必ず走らせる」。0 と 180、90 と 270 は論理の大きさが同じ（§2 の写像）。今の switch も大きさだけを比べる（`output-switch.c:257-259`）。
- 起こること: (1) Settings で 180 を選ぶと、画面は逆さになるが、touch screen・pen・QEMU の tablet の写像（I の受け箱の `struct kwl_cursor_geometry` の `physical[].rotation`、§6.6）は 0 のまま。内蔵の touch と縦置きでない LCD の pen が上下左右に反対の所へ行き、完了の条件 3 を外す（5330 の Settings の 180 は §12.3 の p010 の受け入れそのもの）。(2) 指・pen が触れたままの回転で contact が cancel されず、drag の途中で点が鏡の位置へ飛ぶ。(3) `kwl_displays_tell` が走らないので Settings に `rotation` の event が来ず、request の result は OK でも絵の強調が古いまま。(4) 90↔270 も同じ: game mode の中の `set_rotation(270)`（§4.6 の 0: config だけ書いて leave、次の open で当てる）や、90 を保存した display から 270 を保存した display への output switch で、写像が 90 のまま残る（左右も上下も逆）。§12.2 は 0↔180 を Venus で（policy ANY の turn の経路で）しか流さず、turn の経路は §4.6 の 3〜6 を通るので、この誤りは QEMU の試験で見えない。実機の 5330 で初めて分かる。
- 直し方: open の全経路の比較を「（論理の大きさ, `rotation`）」の組にし、(a) `rotation` が変われば、park の中で 3（その出力に結んだ device の contact の cancel、pen の up と proximity_out、grab の終了）と、受け箱の geometry の 1 区間の書き（§6.6）、`kwl_input_bindings_resync`、`kwl_displays_tell`、`heads_save`（`set_rotation` の時だけ）を必ず走らせる、(b) 論理の大きさが変われば、それに加えて `output_resized(server, 1)` と `kwl_outputs_changed`、と §4.6 に書く。pointer の「同じ物理の点」の warp も (a) に入れる（絶対の device の pointer は 180 で反対の位置になる）。host 試験に「plan の前後の組 → 走らせる手順の集合」の表（純粋な関数にしておく）を足し、§12.3 の p010 に「180 にした後、内蔵の touch で App Home の tile の位置が合う」を足す。

### C6. ws.md 末尾の新しい決定（90/270 の出力で game mode に入ると回転を外す、D6 の置き換え）が未反映で、素直に入れると回転が往復し続け、保存の値と配置を壊す（§4.5、§4.6 の 0、§9、§12.2 の 7、§13 の p002）

- 根拠: 第 3 版の §4.5 は「`rotated`（`rotation != 0`）なら合成に落とす（D6）」、§4.6 の 0 は「game mode なら config を書いて `kwl_scanout_leave(server, KWL_SCANOUT_ROTATED)`」、§12.2 の 7 は「game mode の app が `direct=0 reason=rotated`」。ws.md の最後の決定（2026-10-13 ユーザー「ハードウェアでできない回転を使っているときにゲームモードにすると、回転が無効になる、でOK」）は D6 を置き換え、90°・270° の出力では game mode の間 0° の物理の向きで直の scanout をし、出たら戻す。180° は D17 のまま。
- source の事実（入れる時に効く所）:
  - game mode の入り口は「client の buffer が `server->width/height` と同じ」（`scanout.c:186-190`）で、入ると swapchain を閉じる（`scanout.c:284`、`kwl_compose_output_close` は heads も閉じる `compose.c:524`）。claim は `server->width/height`（`scanout.c:288`）。
  - 規則は size の後に overlay・shot・pointer を見る（`scanout-rules.c:40-60`）。pointer が 2 秒動かないこと（`KWL_SCANOUT_POINTER_IDLE_MS`）が条件で、少しでも動くと leave（`scanout.c:76-84`）、次の pass の `enter_window_mode` → `kwl_compose_output_open`。第 3 版の C4 の (b) は、この open が「この display の key の config から plan を作り直す」。
  - fullscreen の configure は `server->width/height`（`protocol.c:1729-1731, 1838-1839`）。
  - Videoplayer は content type video を立てる（`userland/desktop/videoplayer/main.c:240`）。規則は video と game を同じに扱う（`scanout-rules.h:24-26`）。
- 起こること（素直に「game mode に入る時に 0、出る時に戻す」と入れた場合）:
  1. **入れない**: 90° の出力の fullscreen の game は論理 800x1280 の buffer を描く。0° の直の scanout は 1280x800 の buffer が要るので `size_matches` が永久に 0。回転を外してから client に 1280x800 の configure を送り、新しい buffer を待つ 2 段の入り口が要る。
  2. **往復**: 入った後、mouse が少し動く・edge を触る・shot の度に leave → open が config の 90 で plan を作る → 論理が 800x1280 → `output_resized`（全部の窓の refit、壁紙と blur の CPU の描き直し `glass.c:766, 843, 1159`、`kwl_outputs_changed`）→ game に 800x1280 の configure → 2 秒後にまた外す……。game の swapchain の作り直しが 1 回の mouse の揺れで 2 回、利用者には game の向きが縦横に跳ねて見える。
  3. **保存の上書き**: 外すのに `kwl_output_rotate` をそのまま使うと step 6 の `heads_save` が `rotate=` を消す（0 は書かない、§7.4）。game の最中に落ちると再起動で縦が戻らない。
  4. **配置の上書き**: anchor の論理が変わると §4.4 の配置の再検査が head を `kwl_displays_place_right` で置き直し `place=` を保存する。game を 1 回遊ぶと、利用者が並べた 2 台目の位置が失われる。
  5. **窓の位置の損失**: `output_resized` の refit（`kwl_glass_output_resized`、`output-switch.c:674`）は浮いた窓を新しい大きさに clamp する。戻した時に元の位置に戻らないので、game の度に縦の desktop の窓が寄せられる。
  6. **set_rotation の無限の往復**: §4.6 の 0 の「game mode の中の `set_rotation` は config を書いて leave」は、game の窓がまだ fullscreen・content type game のままなので、次の pass でまた入る（外す）。
  7. **video**: 縦に持った device で Videoplayer を fullscreen にすると、video も game mode なので回転が外れ、player が横の向きで描き直される（縦に持った利用者には横倒し）。ユーザーの決定の文は「Game Mode」で、video を含むかは決まっていない（D18 としてユーザーに出す）。
  8. **resize しない client**: configure に従わない client（固定の大きさの game、configure を待たない client）は size が合わず、回転だけ外れて合成のまま残る。
- 直し方（第 4 版に書く形の提案）:
  - **game の session と直の scanout を分ける**: 「session」は top の窓が fullscreen かつ content type が game（D18 の答えで video も）で、他に何も条件にしない状態。「直の scanout」は今の規則（overlay・shot・pointer・refused）。回転は session の間だけ外し（`compose->rotation_paused = 1`、論理は物理の向き）、session の中で直の scanout と合成を行き来しても戻さない（合成の frame は 0° で描く、turn は外したまま）。session が終わる時（窓が fullscreen でなくなる・去る・別の窓が上に来る・content type が変わる・lock・backend が claim を断った `scanout_refused`・size が合わないまま期限、の時）に戻す。
  - **規則の関数に答えを足す**: `kwl_scanout_decide` に「size 以外は direct で、出力が turn の回転（`turn_rotation != 0`）」→ `KWL_SCANOUT_UNROTATE` を足し、host 試験（`host-scanout-rules`）に表を足す。180°（scanout）は `KWL_SCANOUT_ROTATED`（D17、合成のまま）。
  - **外す・戻すは保存しない**: `kwl_output_rotate` の内側の「turn の回転を替える park の手順」（§4.6 の 1〜5）を `persist = 0` で呼び、`heads_save`・`place=` の保存・`kwl_displays_place_right` の配置の置き直しを session の間はしない（配置の検査で無効になる head は保存せず一時の位置に、戻した後に元の `place=` を当て直す）。
  - **窓は動かさない**: session の間、game 以外の窓は refit しない（game の下に隠れている）。game の窓だけに物理の大きさの configure を送る。戻した時は元の論理に戻るので refit は要らない。`kwl_outputs_changed`（wl_output の mode）を送るかは決める（送ると GTK などの最大化の窓が自分で描き直す。送らなければ wl_output と `server->width/height` が session の間だけ食い違う。推奨は送らない、fullscreen の configure だけ）。
  - **戻しを失敗させない**: 外す時に turn・framebuffer・scene・layers・壁紙・blur の 90° の資源を捨てずに保ち、戻す時は入れ替えだけにする（memory は §8 の 2 倍のまま session の間）。戻しが失敗すると保存は 90 なのに 0 で残る。
  - **入力**: 外す・戻す時に §4.6 の 3（cancel）と C5 の (a)（geometry の書き、pointer の同じ物理の点）。session の間は写像も 0°。
  - **期限**: 外した後 N ms（例 1000）以内に size が合わなければ戻し、その窓を `scanout_refused` にする（8 の対策）。log `KWL SCANOUT unrotate surface=… rotation=90 → 0`、`KWL SCANOUT rerotate reason=…`。
  - **set_rotation の扱い**: session の間の `set_rotation` は保存だけ（applied 1）で、session が終わった時に当てる。leave はしない。§4.6 の 0 と §9 の表を直す。
  - **Settings の表示**: `rotation` の event の値の意味を決める（推奨: 保存の値＝利用者の選択を出し、`why` に `KL_DISPLAY_ROTATE_WHY_GAME 4`「Rotation is paused while a game is shown」の bit）。§7.4 の「保存の角度が mask に無い時は実際の 0 を出す」とどちらの値を出すかが食い違うので、どちらも同じ規則に揃える（例: event `rotation(key, rotation, saved)` に 1 つ引数を足す）。**どちらを採っても版 34 の wire は p004 で凍るので、p004 の前に決める**。
  - **段**: 外す・戻すは turn の park の手順（p002b）と Settings の why（p004）を使うので、新しい段（例 p012「game の session の回転の停止」、依存 p002・p004）にし、p002 の中では「turn の回転の出力では game mode に入らない（`KWL_SCANOUT_ROTATED` で合成）」を **暫定**として明記する（p012 で置き換えると書く）。§12.2 の 7 の期待を p012 の後は `KWL SCANOUT unrotate` → `direct=1` に。D6 の文は ws.md で置き換え済みなので、§4.5 の「（D6）」の参照も直す。

## 重大

### S10. i915 の 180° の経路が実際の present の経路と合わない（i915 に commit は無い、回転は worker の frame に乗せる、release・hold・stop で 0 に戻す）（§5.2）

- 根拠: §5.2 は「`gpu_display_present.flags` の `ROTATE_180` と commit の primary の `PLANE_ROTATE_180` → `head->cfg.fb_rotation`・`ms->fb_rotation` → `drv_i915_lcd_ms_plane_prepare(..., rotation)`」。しかし (1) i915 の display の op の表は commit・overlay_info が NULL（`i915/display/display.c:228-245`、capture の build も `capture.c:171-188`）。commit の経路は bcm2711 だけ。(2) present は worker に queue して待つ（`present.c:20-30` の注記、`drv_i915_present` `present.c:143-170`）: shared は GPU が back buffer に写し（whole factor で拡大・中央 `present.c:15-19`）、その後 flip（`modeset.c:2748-2752` で `ms->fb_*` から plane を作り直す）。回転は frame（worker の item → back buffer → flip）と一緒に運ぶ必要があり、`head->cfg`（modeset の構成、`head.c:876-881`）や `ms->fb_*`（lease を越えて残る）に置くと frame ごとの意味（§5.1「frame ごとに変えてよい」）にならない。(3) release は panel を止めずに「最後の絵を次の lease まで保つ」（`present.c:24-30`、`I915_PRESENT_HOLD_MS 10000` `present.c:83-88`）。logout で session（180°）→ greeter（回らない、D9）の順に lease が替わる。
- 起こること: 回転を `ms->fb_rotation` に持たせると、logout の後の greeter の最初の frame（flag 無し）を flip した時に、実装によっては 180 のまま残る（flag の無い present で 0 に戻す規則が無い）。flip が断られた時の戻し（`modeset.c:2752`）が古い回転を渡さないと 1 frame 逆になる。head の present（`i915_present_head` `present.c:1772`）は別の経路で、§5.2 は anchor の経路しか書いていない。
- 直し方: §5.2 を「`ROTATE_180` は `struct i915_worker_present` の item に `rotation` として入れ、back buffer の記録と flip の `drv_i915_lcd_ms_plane_prepare(..., rotation)` に渡す。flag の無い present は 0。flip の断りの戻し（`modeset.c:2752`）は今の frame の回転を渡す。head は `i915_present_head` の同じ形。commit の経路は i915 に無い（bcm2711 だけ）」に直す。release・hold の期限・shutdown の stop・takeover/retained の経路・resume の再 modeset が plane を作る時は 0（または最後の frame の値）と決めて書く。受け入れに「180 の session から logout → greeter が正立」を足す（§12.3）。

### S11. `currentTransform = surface の transform`・`preTransform == currentTransform` の約束は Vulkan の意味の逆で、もっと小さい spec どおりの形がある（§5.3、§11 の 1 行目）

- 根拠: §5.3 は surface の capabilities を `supportedTransforms` = display の集合、`currentTransform` = surface の transform（ROTATE_180）にし、swapchain は `preTransform != currentTransform` を断る（`wsi-swapchain.c:1213-1215` を直す）。spec の `preTransform` は「presentation の前に application が content に当てた transform」で、`preTransform == currentTransform` は「application が自分で回して描いた」を意味する。第 3 版の約束では application は回さずに論理の絵を描くので、spec の読みでは content は回っていないのに「回した」と言うことになる。一方 `VkDisplaySurfaceCreateInfoKHR.transform` は「scanout の一部として画像に当てる transform」で、surface を作った時点で表示の自然な向き自体が回っている、と読むのが素直。
- 起こること: tree の中だけなら動くが、(1) libvulkan の swapchain の検査を緩め、vkdemo と compositor の 2 か所に `preTransform = currentTransform` を入れる変更が要る。(2) spec どおりに書いた application（`currentTransform` を見て自分で 180 回して描き、`preTransform` に渡す）は二重に回る。(3) `supportedTransforms` に IDENTITY を出しながら IDENTITY の `preTransform` を断るのは capabilities と挙動の食い違い（spec は supported な bit を `preTransform` に渡すことを許す）。
- 直し方（推奨、Q1 の技術判断でよい）: display の `supportedTransforms`（`wsi.c:125`）に ROTATE_180 を足し、surface の作成（`wsi.c:620-624`）で受けて `surface->display_info.transform` に残し、present/commit の primary の flag に写す。**surface の capabilities は今のまま IDENTITY（`wsi-display.c:584-585`）、swapchain の検査も今のまま（`wsi-swapchain.c:1213-1215`）**、vkdemo と compositor の swapchain は IDENTITY のまま。約束は「display の surface の transform は scanout が当てる。surface の座標はその後の向き」。これで `wsi-swapchain.c`・`vkdemo_display_create_swapchain*` の変更が消える。hw180 は `VkDisplayPropertiesKHR.supportedTransforms` だけで決める（§4.10 の「と surface の capabilities」を消す。`compose_display` の時点では surface はまだ無い、L30）。今の約束を保つなら §11 の 1 行目に spec の文との食い違いを残し、README に「spec の意味の逆」と書く。

## 中

### M11. swapchain の大きさ（物理）を論理として読む所の一覧と受け入れの grep が無い（§4.1、§4.2 の 3〜5、§13 の p002）

- 根拠: §13 の p002 の grep は `server->width =` の書き手と `compose->pass\b`・`pass_load` だけ。`compose->output.width/height`（物理）を論理として読む所が多い: scene の画像の作成（`compose.c:455-459`）、framebuffer（`compose.c:2016-2017`）、damage の clamp と 70%（`compose.c:2604-2607, 2637`）、frame の renderArea・viewport・scissor（`compose.c:2796-2818`）、layers の画像（`layer-cache.c:101-102`）、backdrop（`backdrop.c` の大きさの読み）、mirror の fit（`heads.c:1200` ほか。`heads.c`・`backdrop.c` で `compose->output.width/height` の読みは 5 行）。§4.2 の 3 の helper の使う所の列にこのうち scene の作成・framebuffer・layers が無い。
- 起こること: 1 か所でも物理のまま残ると、90° で scene や layers が 1280x800 のまま論理 800x1280 の絵を受け、下の 480 行が切れるか、viewport の外になる。Venus の T1 では「一部が描かれない」で見えるが、どこが原因かは分からない。
- 直し方: `compose->output.width/height` の読みを全部表にし、「物理（mode の照合 `cursor-plane.c:509-510`、turn pass、swapchain の作成）」と「論理（`compose_target_size()` に置き換える）」に分ける。p002 の受け入れに `grep -n 'compose->output\.width\|compose->output\.height' userland/desktop/wayland/*.c` が表の物理の行だけ、を足す。

### M12. 壁紙と blur を「先に作る」関数は今の `server->width/height` を読むので、新しい論理の大きさで作れない（§4.6 の 1、M2 の直し）

- 根拠: §4.6 の 1 は step 4 で `server->width/height` を替える**前**に「壁紙と blur の host image（`wallpaper_create` の形で新しい論理の大きさに描く）」を作る。`wallpaper_create` は `server->width/height` で画像を作り（`glass.c:766`）、`blur_create` も（`glass.c:1159`）、描き `wallpaper_render` も（`glass.c:843`）。
- 起こること: そのまま呼ぶと古い大きさの画像ができ、入れ替えた後の uv（`glass.c:2761-2764`、新しい `server->width`）と食い違って C4 と同じ引き伸ばしと、壁紙の変更での mapped memory の越え書きが戻ってくる。
- 直し方: `wallpaper_create`・`blur_create`・`wallpaper_draw`・`wallpaper_render` の呼び方を「幅・高さを引数で取る」に直し、§4.6 の 1 にそう書く。loader の大きさの一致（`glass.c:730-731`）は入れ替えの後の `server->width` と比べるので、入れ替えの前に届いた loader の結果は捨てて描き直す、も書く。

### M13. 保存の回転の turn の資源が open で作れないと、起動で desktop が出ない（§4.6「起動時と open」、§7.4、§9）

- 根拠: open の全経路で「turn の資源は `turn_rotation` で作る（`compose_targets` の前）」。§7.4 の「0 で開いて保持」は「保存の角度が mask に無い時」だけ。`compose_output_open_parked` の資源の失敗は EIO で open を失敗させる（`compose.c:436-447` の形）。
- 起こること: `rotate=KEY 90` を保存した後に、memory が足りない・format の能力が無い（`turn_new` の検査、§4.2 の 1）と、起動の `enter_window_mode` が失敗し desktop が出ない（再起動しても同じ）。output switch・reopen では戻しの経路で古い display に戻ろうとして同じ失敗を繰り返す。
- 直し方: open の中の turn の資源の失敗は「0 で開き、`rotate=` は保持、log、Settings の why」に落とす（§7.4 の規則を mask の外と資源の失敗の両方に）。§9 の表に 1 行。

### M14. 180° の reopen は heads と cursor の plane も閉じる。利用者に見える影響と回転の対象を書いていない（§4.6 の 1、§8、§11）

- 根拠: `kwl_compose_output_close` は heads を全部閉じ（`compose.c:524`）、cursor の plane を閉じ（`compose.c:518`）、backdrop を壊し（`compose.c:529`）、display を返す（`compose.c:534`。返す間 kernel は最後の絵を保つ: i915 `present.c:24-30`）。§8 は「180° への変更は reopen（swapchain の作り直し、数十〜数百 ms）」、§11 は「稀な操作」。
- 起こること: anchor を 180 にすると 2 台目の display も一度消えて開き直す（`heads_stale` の次の look）。head の上の窓は残るが、mirror・extended の head の turn・scanout の資源も作り直しになる。0↔90 の turn の経路（park だけ）と体験が違う。
- 直し方: §4.6 の 1・§8 に「heads も閉じて開き直す（全 display が一瞬消える）」と書き、受け入れ（§12.3 の p010）に「2 台目を繋いだまま anchor を 180 → 2 台目が戻る」を足す。head だけの 180 は `heads_open` の経路で、その head だけ閉じる（§4.11）を明記する。

### M15. 90↔180・270↔180 の変更が §4.6 の 1 の場合分けに無い（§4.6 の 1）

- 根拠: §4.6 の 1 の場合分けは「scanout の回転が変わる時（0 ↔ 180）」と「turn の回転だけが変わる時（0 ↔ 90/270、90 ↔ 270）」。90 → 180 は scanout 0→180 と turn 90→0 が同時に変わり、論理の大きさも変わる。
- 直し方: 「`scanout_rotation` が変わる全部の変更は reopen（turn の資源は open が plan で作る・捨てる）」と書き、host 試験の plan の表（§12.1）に 16 の組（前 4 × 後 4）と、それぞれが reopen か park か、resize が走るか（C5 の表と同じ物）を入れる。

### M16. T1 の 1 つの image で「policy ANY」と「既定」を両方流す方法と、test image の compositor に `--rotate-policy=any` を渡す方法が無い（§12.2、D13）

- 根拠: §12.2 は「compositor の起動に `--rotate-policy=any`」と「4. policy 無し（既定）: Settings の 4 つの絵が無効」を同じ image で書く。compositor は session の起動の script（`keiland-desktop.in`）が起こすので、試験の option を渡す所（build の印、環境変数、`$HOME/.config` の file）が要る。
- 直し方: 例「test image の config.mk の印で `keiland-desktop` が `$HOME/.config/keiland/rotate-policy` が `any` の時だけ `--rotate-policy=any` を足す。既定の場面はその file を消して logout/login」。どちらの場面を先に流すかと、file を消すのは T1 の手順の中（Q1 の rm の pipeline の規則に合わせて `--file` の差し替えで）と書く。製品の build では印が無く option を読まない（§11 の行）。

### M17. 核が `ROTATE_180` を全部の driver に通すが、driver は一様に EOPNOTSUPP を答えない（§5.1、§11「UAPI の `ROTATE_180` の bit を古い driver に送る client」）

- 根拠: §5.1 は核（`gpu.c:3526-3532`、`gpu.c:4057`）の検査に bit を足し「driver が能力を出していない時の EOPNOTSUPP は driver が答える」とする。実際は: venus は present の flags が `FIFO` と `FIFO|BLOB` ちょうどでなければ **EINVAL**（`venus/display.c:1947-1948, 2072-2073`）。bcm2711 の copy の経路は `!= FIFO` で ENOTSUP（`display-device.c:1124-1125`）、blob の経路は BLOB の bit だけ見る（`display-device.c:915`）。i915 は BLOB の bit だけ見る（`present.c:422`）ので、p009 の前の kernel や capture の build（`capture.c` の present）は bit を黙って無視して**回さずに出す**。
- 起こること: libvulkan は能力の印が無い display に transform を受けないので tree の中では起こらないが、UAPI の約束（「能力が無ければ EOPNOTSUPP、何も変わらない」）が driver によって EINVAL・ENOTSUP・黙って無視に分かれ、host 試験 `host-gpu-display-flags` が核だけを見ても検出できない。
- 直し方: どちらか。(a) `drv_gpu_display_ops.flags` に `DRV_GPU_DISPLAY_ROTATE_180`（`include/drivers/gpu/gpu-display.h:95-96` の形、HAL ではない）を足し、核が印の無い driver の bit を EOPNOTSUPP にする（i915 の capture のように output ごとに違う時は driver も答える）。(b) 全部の display の driver の present と commit に「知らない bit は EOPNOTSUPP」を足す（venus・bcm2711・i915・capture）。§5.1 と §11 の行をどちらかに直す。推奨は (a)。

### M18. Pi の HVS の反転の実験が、読み出しの範囲の外を DMA で読むかもしれない（§5.4 の (a)）

- 根拠: §5.4 は (a)「上下の反転の時に pointer を最後の行に置く必要があるか」を Pi の実機で確かめるとし、最初は今の pointer（frame の先頭、`display-flip.c:1702` の `words[5]`）のまま 180 の flag で出す。tree の値は readout の推測（`list.c:330-331` は 2 bit の**どちらか**が立てば flipped と読むだけで、どちらが上下かも、反転の時の読みの向きも言わない）。
- 起こること: HVS が上下の反転で pointer から上（低い番地）へ読むなら、最初の実験は frame の前の memory を height 行ぶん読む。読みだけなので書き壊しは無いが、別の process の memory を画面に出す（情報の漏れ）か、割り当ての外の番地で AXI の error・underrun になる（推測）。
- 直し方: 最初の実験の frame を「上と下に height 行ずつの余白（既知の色）を持つ 3 倍の高さの buffer の真ん中」に置き、pointer を先頭に置いた時と最後の行に置いた時の両方を同じ実験で見る、と §5.4 と p011 に書く。

## 軽微

- L26. §1.2 の「`pass_load`: backdrop が一度だけ作り」は不正確。作るのは公開の `kwl_compose_load_pass`（`backdrop.c:304-358`）で、呼び手は scene の open（`compose.c:452`）、部分 frame（`compose.c:2641`）、backdrop（`backdrop.c:215`）。§4.2 の 3 の「`backdrop.c:305-335` の作成をここへ移す」は不要（もう helper）。`kwl_backdrop_destroy` が `pass_load` を壊さないように変えるなら（M2 の直し）、壊す所（compositor の終わり）を書く。format は一生変わらない（`compose.c:323-334` の「made once」）ので、output を越えて使い回すのは正しい。
- L27. §1.3 の venus の display_query の flags は `VIRTUAL_CLOCK | FIFO | CONNECTED | ACTIVE | POWER_CONTROL | REFRESH_COUNTER`（`venus/display.c:790-802`）。`venus/display.c:154-167` は op の表。
- L28. libvulkan の commit の primary の flag は `display_overlay_state()` の中でなく呼び手（`wsi-display.c:2401`）で立てる。helper は overlay（`2417, 2728, 3097, 3175`）も使うので、中で立てると overlay が EINVAL になる。
- L29. §5.2 の能力の印は i915 の anchor（`display.c:2750`）と他の出力（`display.c:3360`）の 2 か所で、capture の build（`capture.c` の `i915_capture_query`）には付けない、と書く。
- L30. §4.10 の hw180 の読み「display の property と surface の capabilities」: `compose_display`（起動の plan）の時点では surface が無い（`compose.c:1300-1395`）。display の property だけで決める（S11 の推奨なら surface の capabilities は IDENTITY のまま）。
- L31. UAPI の意味「画像の (x, y) を出力の (W−1−x, H−1−y) に」（§5.1）は、i915 が出力より小さい frame を拡大・中央に置く（`present.c:15-19, 405`）時は「driver が置いた後の出力の上で 180」と書く。
- L32. §13 の依存の graph の 1 行（`p001 → {p002, p009} → p010 → p003 → p004 → p005 → {p011, p006}`）は表と合わない: 表の p003 は p002 だけ、p011 は p009・p010・WS214。graph を表に合わせて直す（p003 は p010 と並行に置ける）。
- L33. p011 の前提「WS214 p003a/p003b の main への統合」は、ws214 の記録では main `114e7c68c` に実装済み（`plan/ws214/ws.md:11`、clear は Pi と T1 待ち）。条件は「統合」でなく「p003a/b の clear（Pi の実機の確認の後）」に直すか、統合済みと書く。
- L34. §6.3: `kwl_output_absolute` が plane の位置（`compose->output_x/output_y`、L12）を足すようになると、今 `[0, server->width)` で比べている touch の判定（`touch.c:1020-1021, 1608, 1613, 1758-1761, 1848, 2082-2085`、`corner.c:634`）は anchor の左上を引いた相対の座標で比べないと、anchor が原点でない時に edge・zone がずれる。「判定の関数は矩形を取る」の矩形が x・y を含むと明記する。
- L35. PSR2 の selective fetch は Linux の参照では回転した plane で使えない（`intel_psr.c:2179-2188`、MIT）。zedBSD は `enable_psr2_sel_fetch` を立てていない（grep で代入が無い）ので今は影響なし。p009 の注記と §14 に「selective fetch を入れる時は 180 で切る」を 1 行。
- L36. §6.4 の `rotate=KEY N`・`touch=IDENTITY KEY`: KEY は空白を含み得る（`displays.h:31`「key may hold spaces; X and Y are the last」）。`rotate=` は N が最後、`touch=` は IDENTITY が最初（空白無し）で KEY が残り全部、と parse の規則を書く。
- L37. §4.9・§4.10 の `KWL_TURN_DISABLED` と C6 の game の session: rpi4 では turn が無いので session の derotation も起こらない（Pi の 180 は D17 で合成）。p012 の範囲に「rpi4 は対象外」を 1 行。

## game mode の新しい決定を入れる時の危険（要求された観点の整理）

C6 の直し方の詳細。第 4 版でこれを決めて書く。

| 観点 | 危険 | 推奨 |
| --- | --- | --- |
| park | 外す・戻すは turn の park の手順（§4.6 の 1〜6）で、合成の間（swapchain を閉じる前）にしかできない。`scanout_enter` の後（swapchain が閉じた後）に外すと configure が先に出せない。open の再計画（C4 の (b)）が config から 90 を当てるので、session の印を open が見る必要がある | 外すのは `KWL_SCANOUT_UNROTATE` の pass で、合成の間に park。open の plan は `rotation_paused` なら 0。戻すのは session の終わりの pass（窓が去る時は `kwl_scanout_surface_gone` の中では park しない、次の pass で） |
| 資源 | 戻しの資源（turn・scene・layers・壁紙・blur）を捨てると戻しが失敗し得る、壁紙の CPU の描き直しが session ごとに 2 回 | 90° の資源を session の間保つ（入れ替えるだけ）。memory は §8 の 2 倍が session の間続く、と §8 に |
| 入力の写像 | 外した瞬間に touch・pen が古い写像で続く。利用者は縦に持ったまま横の絵に触る | 外す・戻す時に cancel と geometry の書き（C5 の (a)）。session の間の写像は 0°（絵と一致するので正しい）。pointer は同じ物理の点 |
| 窓の配置 | refit が浮いた窓を clamp して戻らない。head の配置の置き直しと `place=` の保存 | game 以外は refit しない。配置の検査は session の間は保存せず、戻した後に保存の `place=` を当て直す |
| 保存 | `heads_save` が `rotate=` を消す | 外す・戻すは保存しない（`persist = 0`） |
| Settings | 0 を出すと利用者が 90 を押して往復が起こる。保存の値を出すと絵と画面が食い違う | `rotation` は保存の値、`why` に `WHY_GAME`（または event に `saved` の引数）。p004 の wire の前に決める。session の間の `set_rotation` は保存だけ |
| client | resize しない client で回転だけ外れたまま | 期限で戻して `scanout_refused` |
| video | Videoplayer の fullscreen が横倒しになる | D18 としてユーザーに出す（推奨: game だけ。video は縦の画面でも 0° の直の scanout にしないで合成のまま） |
| lock・hotplug・switch | lock は overlay で直の scanout を外すが session は残る（lock 画面が横で出る）。output switch は `!server->windowed` で断る（`output-switch.c:185`） | lock は session を終える（戻す）。hotplug で anchor が替わる時も終える |
| 試験 | §12.2 の 7 の `reason=rotated` は 90/270 で成り立たなくなる。Venus の policy ANY で 90 の session を流せる | host の規則の表に UNROTATE、T1 に「90 の出力で game の client（wltest の fullscreen と content type game）→ `KWL SCANOUT unrotate` → `direct=1` → mouse を動かす → 合成でも 0° のまま → 窓を閉じる → 90 に戻る・`rotate=` が残る・head の `place=` が残る」 |

## review-2 の指摘への対応の確認（第 3 版の対応の表を source で確かめた）

| ID | 確認 | 残り |
| --- | --- | --- |
| C3 | 解決。§4.2 の 6 の CLEAR（全体）／`pass_load`（部分、renderArea は物理の箱）は `compose_region` の返し方（初めての画像・age 切れ・70% で 0 を返す `compose.c:2577-2578, 2637-2638`）と合う。初めての swapchain の画像は必ず全体なので `PRESENT_SRC_KHR` の前提も成り立つ | — |
| C4 | (a)・(c) は解決（`compose_display` の `compose.c:1358` の後で当てられる。`kwl_heads_config_load` は parse だけ `heads.c:126-168`）。(b) は比較が大きさだけ | C5、M12（壁紙の先作り）、M13 |
| S7 | 解決（gesture は anchor だけと決めた） | L34 |
| S8 | 解決 | — |
| S9 | 文は解決。head の mode が変わる既存の経路の有無は p005 の最初に確かめる、のまま | — |
| M1 | 解決（書き手は `main.c:104-105, 419, 433`、`compose.c:1384-1385`、`output-switch.c:218-219, 230-231` の 6 か所だけ、grep で確かめた） | — |
| M2 | 方針は解決 | M12、L26 |
| M3 | 解決（3 か所 `compose.c:2791, 2876`・`backdrop.c:127`） | M11（大きさの読み） |
| M4 | 解決。requests 7（0〜6）・events 6（0〜5）は今の表（`system-protocol.c:506-518`、requests 4・events 3、interface の版 `KL_SYSTEM_SINCE_SHOWN` `:521-528`）と合う。34 は main と全 branch で空き、`KL_VERSION` は 92（grep） | C6 の Settings の値の意味は p004 の前に |
| M5・M6・M7・M9・M10 | 解決 | — |
| M8 | 解決（`KWL_TURN_DISABLED`） | L37 |
| L11〜L23・L25 | 解決 | — |
| L24 | 解決。`plane_publish` は box に書いて broadcast するだけで待たない（`cursor-plane.c:782-794`）ので、park の解除（`cursor-plane.c:314-317`）の前に移しても止まらない | — |

## 確かめた点（source と照らして正しかった所）

- UAPI: `gpu_display_info.flags` は 1〜2048（`gpu-display.h:23-36`）、4096 は空き。present の flags は 1・2（`:41-42`）、plane_state の入力の flags は 1・2（`:54-55`、結果の 1・2 は別の field `result`）。核の検査は `gpu.c:3530-3531`、`gpu.c:4057`（commit の全体の flags は `COMMIT_WAIT` だけ `gpu.c:3998`）。struct の大きさを変えない追加で ABI は保てる。`GPU_DISPLAY_PRESENT_ROTATE_180 4`・`PLANE_ROTATE_180 4`・`VULKAN_WSI_OUTPUT_ROTATE_180 64` は衝突しない（`wsi-internal.h:20-27`）。
- i915: `hw.rotation = MODE_ROTATE_0` は `plane.c:293, 378`。`i915_skl_plane_ctl_rotate()`（`plane.c:777-803`）は ROTATE_180 → `PLANE_CTL_ROTATE_180`、`i915_skl_plane_ctl()` が `rotation & MODE_ROTATE_MASK` で呼ぶ（`plane.c:926`）。flip のたびに `drv_i915_lcd_ms_plane_prepare` で PLANE_CTL を作り直す（`modeset.c:2750`）。能力の 2 か所 `display.c:2750, 3360`。
- MIT の参照: linear で断るのは REFLECT_X（`skl_universal_plane.c:1431-1435`）と 90/270（`:1438-1442`）だけ、180 は断られない。`DISPLAY_VER >= 13` は 0・180（`:2402-2407`）。main surface の offset の計算（`:1696-1760`）は src の矩形から x・y を取るだけで、180 の特別な補正は見当たらない（全画面の src は 0,0。ただし `intel_atomic_plane.c` 側の src の回転は読んでいない）。FBC は skl 以降で 180 を拒まない（`intel_fbc.c:996-1003`）。
- libvulkan: `wsi.c:125`（IDENTITY）、`wsi.c:620-624`（IDENTITY 以外を断る）、`wsi.c:662`（`*create_info` を写す）、`wsi-display.c:584-585`、`wsi-swapchain.c:1213-1215`、present の flags `wsi-display.c:1412, 1850`、commit の primary `wsi-display.c:2401`。
- vkdemo: `display.c:133`（IDENTITY 固定）、`:285-287`（surface の IDENTITY の検査）、`:322`（`preTransform = IDENTITY`）。呼び手は compositor（`compose.c:307`、`heads.c:837`）と vkdemo の renderer（`renderer.c:622`）。
- bcm2711: flags `display-device.c:698`、frame の words `display-flip.c:1696-1705`（`words[1] = 0`）、primary の位置 `display-flip.c:2234`（§5.4 は `encode_plane`／`prepare_frame` と書くが、primary の位置の word を書くのはこの 2234 も。実装でここにも反転の bit を入れる）、readout の flipped `list.c:330-331`、console の探索 `list.c:263-267`。X の mask は 14 bit（`list.c:51`）なので bit 15 は位置と重ならない。
- compositor: `kwl_compose_open` の順（`compose.c:114` の `compose_display` → `:158-165` の glass）、`compose_display`（`compose.c:1300-1395`、display の選び方は IDENTITY を持つ最初の display）、`kwl_compose_output_prepare`（`compose.c:292-343`、`server->width/height` で surface）、`kwl_compose_output_close`（`compose.c:493-547`）、`output_resized`（`output-switch.c:658-688`）、switch の比較（`output-switch.c:257-259`）、game mode の入り口と claim（`scanout.c:186-190, 284, 288`）。
- touch.c の anchor の大きさの読み（S7 の行）は全部ある。`ROUTE_IGNORED 5`（`touch.c:116`）。
- 試験の元: `plan/ws113/tests/host-displays.c`、`config-amd64-p006.mk`、`plan/ws213/tests/cursor-due-host-test.c`、`plan/tools/settings/host-kl-settings.c` はある。`tests/scenarios/desktop/displays/` はまだ無い（新しく作る）。

## 確かめていない点（推測を含む）

- Intel の PRM の 180 と linear の記述（p009 の最初の段）。PSR1・DMC・DC 状態と frame ごとの回転の切り替えの相互作用（実機）。
- i915 の hold の期限・takeover・resume の経路が plane を作り直す時に、どの回転を使うのが正しいか（S10 で「0 か最後の frame の値」と書いたが、どちらが console と greeter に安全かは実装を読んで決める）。i915 の lease が切れた後に text の console が同じ plane に戻るかどうか（読んでいない）。
- HVS の 2 bit のどちらが上下か、上下の反転の時の読みの向き（M18、Pi の実機）。
- spec の `currentTransform` と display の surface の transform の関係について、Vulkan の spec の本文を tree の中で確かめていない（S11 は spec の field の説明の文の読み。Mesa など他の実装は読んでいない）。
- game の session の間に wl_output の mode を送らない案（C6）で、xserver・GTK4・libkeiland の client が fullscreen の configure と wl_output の食い違いをどう扱うか。
- `kwl_glass_output_resized` が浮いた窓を clamp して位置を失うか（C6 の 5 は名前と呼び方からの推測。中身は読んでいない）。
- 第 3 版の §4.4 の head の refit の既存の経路（review-2 と同じく未確認）、OSK・Wiseview・App Home の縦の配置、lock 画面と sleep・resume・lid（review-1 から未確認のまま）。
