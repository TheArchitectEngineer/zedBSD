<!-- awesome-plan project=zedbsd record=ws217-p001-review-4 -->

# ws217-p001 第 4 版の敵対的レビュー（review-4）

- 対象: [phase.md](phase.md) 第 4 版（2026-10-13 P4）と [ws.md](../ws.md) の「ユーザーの決定」（末尾の 2026-10-13「Video Player は Game Mode をやめます…」と [WS223](../../ws223/ws.md) を含む）。照合した source は main `a4a2cbada`。設計が引く `278aa9088` からの差分に `userland/desktop/wayland`・`include/uapi/gpu-display.h`・`include/drivers/gpu`・`src/drivers/gpu/gpu.c`・i915 の display と `worker.h`・bcm2711・libvulkan・vkdemo は無い（`git diff --stat 278aa9088 a4a2cbada -- …` が空）。
- 文書・source は変えていない。`.internal/` と GPL の source は読んでいない。tree の MIT の i915 の参照（`plan/ws031/linux-parity/linux-reference/ubu-i915-src/display/`）は読んだ。build・QEMU・host 試験は流していない。
- 重大度は review-1〜3 と同じ: 致命的（このまま実装すると壊れる・直すまでその段に入れない）／重大（受け入れ・主要な経路を外す）／中（局所の誤り・欠落）／軽微。番号は review-3 から続ける（C7〜、S12〜、M19〜、L38〜）。
- 件数: 致命的 1（C7）、重大 5（S12〜S16）、中 9（M19〜M27）、軽微 12（L38〜L49）。

## 判定（p002b・p009・p012 を始めてよいか）

- **p002a（既に開始可とされた段）: 条件つき**。C7 は 0° のままでも WS113 の output switch（大きさの違う display への移動と hotplug）を壊す。p002a の phase.md に C7 の直し（before は「最後に開いた・回した状態」を compose が持つ）と L49（`kwl_rotate_state` に選択の値、open の経路では REOPEN・TURN の bit を落とす）を入れてから進める。既に実装を始めていればその直しを入れる。S16 の `kwl_rotate_steps` の mode の引数も p002a で入れておくと p012 で関数の形を変えずに済む（推奨）。
- **p002b: C7 と S12 と M21 を直した後なら始めてよい**。S12（`kwl_output_rotate` が「選択」と「実効の切り替え」を兼ねている）は p002b が作る関数の形を決めるので p002b の前に直す。S13〜S16・M19・M20・M22 は p010・p012 の範囲で、p002b を止めない。どれも文の直しで、新しいユーザーの判断は要らない。
- **p009（実装）: 始めてよい**。ただし M23（`pend_rotation`／`cur_rotation` と flip しない経路）・M24（核の登録の検査 `gpu.c:1096` が新しい ops の印を断る）・M25（vkdemo の関数の形の変更が compositor の build と p002a に触る）・M27（i915 の host 試験 `host-lcd-modeset-test.c` がある）を p009 の phase.md に書いてから。M24 を落とすと i915 の device の登録が EINVAL で失敗し、画面が出ない。
- **p010: S13 を直す前は不可**（直の scanout の中の REOPEN と、`enter_window_mode` が open の前に surface を作る順）。
- **p012: まだ不可**。重大 4（S13〜S16）と M19・M20・M22・M26 が §4.12 に集まる。加えて ws.md 末尾の新しいユーザーの決定（「Game Mode は Wayland 拡張で要求があった時のみ」「Game Mode を利用したアプリは現段階では存在しない」、WS223）で D18 は不要になり、**session の定義（今の `KWL_SCANOUT_CONTENT_GAME`）は WS223 p001 の「どの拡張を求めとするか」に依存する**。p012 は §4.12 を第 5 版で直し、WS223 p001 の決定の後に始める。それまでの暫定（turn の出力では game mode に入らない、`KWL_SCANOUT_ROTATED`）は安全なので、p012 を p007 の後ろに回しても完了の条件 1〜6 を外さない（Q1 の判断、ユーザーへの報告を推奨）。

## 致命的

### C7. open の前後の比較の `before` を open の入口で取ると、output switch と hotplug の resize が落ちる（§4.6 の B、§4.1、§13 の p002a）

- 根拠: §4.6 の B は「`kwl_compose_output_open`（… output switch（`output-switch.c:221, 233`）、hotplug の reopen）は入口で `before = {server->width/height, rotation, scanout, turn}` を取り」「`output-switch.c:257-259` の `width != old_width` はこの比較に置き換わる」。§4.1 は `kwl_output_size_set` の呼び手に `output-switch.c:218, 230`（新しい display と戻し）を挙げる。今の switch は close（`output-switch.c:212`）の後、open（`:221`）の**前**に `server->width = width`（`:218`）で新しい display の大きさを書く。古い大きさは `old_width = server->width`（`:201`）で先に取ってあり、今の比較（`:257-258`）はそれと比べている。hotplug も同じ `kwl_output_switch` を通る（`output-switch.c:113-133`）。
- 起こること: 書いたとおり実装すると open の入口の `before` は新しい display の大きさで、`after` と同じになり、RESIZED が立たない。**p002a（回転は全部 0）の時点で**、1920x1080 の内蔵から 2560x1440 の外付けへ switch すると `output_resized`（`kwl_glass_resize` の壁紙と blur の作り直し `glass.c:565-597`、窓の refit `shell.c:2733-2812`、pointer の clamp）が走らない。壁紙と blur は古い大きさのまま、uv（`glass.c:2761-2764`）と loader の一致（`glass.c:730-731`）は新しい `server->width` を読むので、C4 と同じ引き伸ばしと、壁紙の変更の時の mapped memory の越え書きが WS113 の既存の機能に戻る。回転があれば C5 の「90 を保存した display から 270 を保存した display へ」も ROTATED が落ちる。host 試験の 16 の組は純粋な関数だけを見るので、これを見つけられない。
- 直し方: `before` は呼び手の時点の値ではなく、compose が持つ「最後に成功した open・回転の状態」（例 `compose->opened`、`struct kwl_rotate_state`）にする。open・`kwl_output_rotate`・switch の戻しの成功の最後にだけ書き、open の入口ではなく open の最後に `kwl_rotate_steps(compose->opened, after)` で比べて書き換える。switch の失敗の戻しはこの値で比べるので何もしない。§12.2 の 7 に「p002a で大きさの違う 2 つの display の switch（WS113 p007 の image）→ `KWL OUTPUT resized` と `KWL GLASS` の壁紙の大きさ」「90 を保存した display から 270 を保存した display への switch → `steps=…rotated…`」を足す。

## 重大

### S12. `kwl_output_rotate` が「利用者の選択」と「実効の回転の切り替え」を 1 つの関数で兼ね、§4.12 の呼び方では session が戻らない（§4.6 の A の 0、§4.12）

- 根拠: §4.6 の A の 0 は「`rotation == rotation_selected` なら、`persist` なら config を書いて 0」（何もせず返る）。§4.12 は session の終わりで「合成の中なら `kwl_output_rotate(server, ANCHOR, rotation_selected, persist = 0)` の TURN の経路で戻す」、入り口で「`kwl_output_rotate(server, ANCHOR, 0, persist = 0)` … `rotation_selected` はそのまま、実効は 0」と呼ぶ。SAVE の条件は「persist で `rotation_selected` が変わった」なので、関数が `rotation_selected` を書くことが前提になっている。
- 起こること: (1) 終わりの呼び出しは `rotation`（90）== `rotation_selected`（90）なので step 0 で返り、**回転が戻らない**（0° のまま、Settings は 90 と `WHY_GAME` を出し続ける）。(2) 入り口の呼び出しで関数が `rotation_selected = 0` を書けば、Settings の `rotation` の event が 0 になり（§7.1 の「選択の値」の約束に反する）、終わりは 0 に「戻す」。`rotate=` は config に残るので、再起動で初めて 90 に戻る（review-3 の C6 の 3 と同じ形の壊れ方）。書かなければ (1)。どちらでも壊れる。
- 直し方: 2 つに分ける。`kwl_output_select_rotation(server, slot, rotation)`（mask の検査、`rotation_selected`、config、SAVE、session の間は保存だけ）と、内部の `kwl_output_apply_rotation(server, slot, effective, flags)`（plan、steps、REOPEN／TURN、park。`flags` に `PERSIST`・`SESSION_PAUSE`・`SESSION_RESUME`）。step 0 の「同じなら何もしない」は前者の「選択が同じ」と、後者の「実効が同じ」で別々に判定する。§4.6 の A・§4.12・§9 をこの 2 つの名前で書き直す。p002b がこの形を作る。

### S13. 直の scanout の最中の REOPEN（180）が display の取り合いになり、`enter_window_mode` が open の前に古い transform で surface を作る（§4.6 の A の 0・2、§4.6 の B、§4.12 の set_rotation）

- 根拠: 直の scanout に入る時、swapchain を閉じてから backend が display を claim する（`scanout.c:284-288`、注記 `scanout.c:17-19`「the swapchain is closed … (libvulkan gives the display back) and the backend claims the display」）。§4.6 の A の 0 と §4.12 は「180 は session に関係なく当てる（reopen で swapchain が開き直り、直の scanout は終わる）」とし、A の 2 の REOPEN は `kwl_compose_output_close` → `kwl_compose_output_open`。close は `output_open` が 0 なら surface だけ閉じて返る（`compose.c:504-508`）ので、直の scanout の最中は backend の claim を離さないまま swapchain の claim に進む。また `enter_window_mode` は open の前に `kwl_compose_output_prepare` を呼び（`display.c:607` → `:613`）、prepare は今の大きさと transform で surface を作り（`compose.c:307`）、作った後は `output_prepared` で何もしない（`compose.c:302`）。§4.6 の B の「open が config から plan を作り直し、surface を plan の transform で作る」は、この順では間に合わない。
- 起こること: (a) p010 以降、0° の出力で game が直の scanout の最中に `set_rotation(180)`（Settings を head に出している時、`KWL OUTPUT rotate` の試験の命令）が来ると、REOPEN の swapchain の作成が backend の claim と衝突して失敗し、§9 の「元の向きでもう一度 open」も同じ理由で失敗し、`output_lost`（hotplug 待ち）になる。p012 の session の中の 180 も同じ。(b) 代わりに「config を書いて leave」にしても、次の pass の `enter_window_mode` の prepare が IDENTITY の surface を作り、open の再計画が 180 を選んでも surface は作り直されない。`rotation` は 180 なので入力の写像と Settings は 180、画面は回っていない（touch が上下左右に逆）。
- 直し方: (1) `scanout_rotation` が変わる変更は `server->scanout != NULL` の間は REOPEN しない: 選択と config を書き、session を終え（p012）、`kwl_scanout_leave(server, KWL_SCANOUT_ROTATED)`、次の pass の open で当てる、と §4.6 の A の 0・§4.12・§9 に書く。(2) plan は prepare の前に作る: plan の再計画を `kwl_compose_output_prepare` の入口に移すか `display.c:607` の前で呼び、open は prepared の surface の transform が plan と違えば surface を閉じて作り直す。(3) §12.3 の p010 に「vkdemo 以外の直の scanout は無いので、試験の命令で 0° の出力の直の scanout 中に 180」を入れる（T1 は Venus に hw180 が無いので、policy ANY の turn では (b) の順の誤りしか見えない。実機で見る）。

### S14. game の session の入り口と終わりの条件が非対称で、lock の間 2 秒ごとに縦横が往復し、head の窓でも anchor の回転が外れる（§4.5、§4.12）

- 根拠: §4.5 は `KWL_SCANOUT_UNROTATE` を「window・fullscreen・content・gpu_buffer の後、size の前」に置く。lock は overlay の fact（`scanout.c:244` の `server->locked`）で、規則の順では size の後（`scanout-rules.c:46-47`）なので UNROTATE は lock を見ない。`kwl_top_window` は lock も出力も見ず、どの出力の窓でも map の順で一番上を返す（`display.c:311-340`。窓の出力は `surface->output`、`shell.c:2763`）。§4.12 は lock で session を終え、同じ窓の再開を 2 秒だけ止める（`KWL_SCANOUT_REROTATE_HOLD_MS`）。入り口は「合成の間」とだけ書き、出力が開いているか・lost か・`server->windowed` かの条件が無い。
- 起こること: (1) game の上で lock すると、終わり（rerotate、縦）→ 2 秒後に hold が切れ、top はまだ game の窓なので UNROTATE（横）→ 次の pass で lock を見て終わり（縦）……と lock の間ずっと 2 秒ごとに park・turn の切り替え 2 回、game への configure 2 回、lock 画面の縦横の跳ねが続く。(2) extended で fullscreen の game を head に置くと、`kwl_top_window` がそれを返し、anchor（90°）の回転が外れ、head の上の game に anchor の物理の大きさの configure が行く（§4.12 の「head は対象外」と逆）。(3) `output_lost` や switch の待ちの間に UNROTATE が出ると、閉じた出力に TURN の経路を走らせる（§4.6 の A の 0 は「閉じている時は config だけ」で、`persist = 0` なので何もせず、`rotation_paused = 1` だけ立つ）。
- 直し方: 入り口の条件を終わりの条件の否定にそろえる: `!server->locked`、`top->output == KWL_PLANE_ANCHOR`、`compose->output_open && !compose->output_lost && server->windowed`、refused でない、hold の中でない。lock・出力・head の fact を `kwl_scanout_facts` に足し、host-scanout-rules の表に「lock の間は UNROTATE を返さない」「head の窓は返さない」を入れる。

### S15. 期限 1000 ms は `waiting` を DIRECT でしか解かないので、pointer の 2 秒の条件で正しい client も期限切れで refused になる（§4.12 の「直の scanout へ」「期限」）

- 根拠: §4.12 は「client が物理の大きさの buffer を出したら size が合い、pointer が 2 秒止まっていれば DIRECT → `scanout_enter`。`waiting = 0`」「`waiting` のまま `KWL_SCANOUT_UNROTATE_MS`（1000）が過ぎたら session を終えて戻し、`scanout_refused = window`」。DIRECT には pointer が 2000 ms 止まっていることが要る（`KWL_SCANOUT_POINTER_IDLE_MS 2000U` `scanout-rules.h:22`、`scanout-rules.c:52-53`）。overlay（menu、App Home、popup `scanout.c:206-266`）も DIRECT を遅らせる。refused は窓が去るまで消えない（`scanout.c:150-152`）。
- 起こること: 利用者が mouse や指で game を始める（click の直後は pointer が動いて 2 秒経っていない）と、client が 50 ms で 1280x800 に描き直しても DIRECT にならず、1000 ms で期限切れ → 回転が戻り（game にもう一度 800x1280 の configure）、窓は refused になってこの窓の間は二度と game mode に入らない。普通の起動の仕方でほぼ毎回起こる。§12.2 の 9 の T1 は注入の操作で pointer を動かさないので通り、試験で見えない。
- 直し方: `waiting` は「size が合った（物理の大きさの buffer が届いた）」で解く（`facts->size_matches` を見る。DIRECT かどうかは問わない）。期限は size の待ちだけに使う、と書く。host 試験は時間を持たないので、session の進み（waiting・期限・hold）を純粋な関数（入力: facts と経過時間と session の状態、出力: 次の状態と動作）にして host 試験に入れる。T1 に「mouse を動かしながら game を始める → 期限切れにならない」を足す。

### S16. session の終わりが「出力が閉じた状態」で起こる経路が無く、open の経路は保った資源を使わず glass を作り直す（§4.12 の「終わり」「資源」、§4.6 の B、§8）

- 根拠: 窓が去ると `kwl_scanout_surface_gone` が自分で `kwl_scanout_leave` を呼ぶ（`scanout.c:147-148`、`objects.c:357`）。claim の失敗（`scanout.c:284-292`）と present の失敗（`scanout.c:329-334`）も swapchain を閉じたまま返す。§4.12 の終わりの手順は「直の scanout の中なら leave」「合成の中なら TURN の経路」の 2 つだけで、次の pass の判定の時の「scanout が NULL、`windowed` が 0、出力は閉じている」状態を扱わない。また `kwl_compose_output_close` は scene・layers・backdrop・targets を壊す（`compose.c:527-530`）が、§4.12 は `turn_kept` をこの破棄から外すと書いていない。open は scene（`compose.c:452-459`）と layers（`compose.c:479`）を作り直し、§4.6 の B の open の後の RESIZED は「open・switch の経路は今どおり `kwl_glass_resize`」（§4.6 の A の 7）で壁紙を CPU で描き直す。`kwl_rotate_steps` の入力に session の区別が無いので、入り口・終わりの RESIZED も refit・`kwl_outputs_changed`・配置の再検査を含む（§4.12 はこれを飛ばすと書くが、関数と 16 の組の表には無い）。
- 起こること: game を閉じた（直の scanout の最中の、いちばん普通の終わり方）時、(a) 判定が `enter_window_mode` の後なら、open は `rotation_paused = 1` のまま 0° で開き、次の pass で TURN の経路で 90 に戻す: 0° の desktop が 1 frame 見えてから縦になり、park の切り替えが余分に 1 回。(b) 判定が前なら open が 90 で開くが、保っていた 90° の scene・layers・壁紙・blur は使われず（close で壊れているか、使われずに残って leak）、壁紙は CPU で描き直し（§8 の「描き直しは無い」に反する）、全部の窓が refit され、`kwl_outputs_changed` が送られる（入り口では送らなかった）。`turn_kept` の寿命（switch・hotplug・logout・session の中の `set_rotation(0/180)` で捨てる）も決まっていない。
- 直し方: (1) session の終わりの判定を `kwl_scanout_pass` の最初（直の scanout と leave の判断の前）に置き、状態を 3 つ（直の scanout、合成、閉じている）に分けて手順を書く。閉じている時は `rotation_paused = 0` だけにして、同じ pass の `enter_window_mode` の open が 90 で開く。(2) `turn_kept` は compose の output の資源の外に置き、close では壊さず、open は plan が kept と合えば kept を使う（scene・layers・turn・壁紙・blur）。(3) `kwl_rotate_steps` に `mode`（USER、OPEN、SESSION_PAUSE、SESSION_RESUME）を足し、SESSION_* では RESIZED を「refit 無し・outputs_changed 無し・配置は保存しない・glass は入れ替え」にする。16 の組の表に session の 2 行を足す。(4) `turn_kept` を捨てる時を列挙する（session の中の 0・180 の選択、switch、hotplug、logout、compositor の終わり）。

## 中

### M19. session の中の `set_rotation` の後の `turn_kept` と、直の scanout の中の 90 で `rotation_paused` だけ立てる形（§4.6 の A の 0、§4.12、§9）

- 根拠: §4.12 は「session の間の 90/270 は保存だけ、0 は `rotation_selected = 0` と config、180 は普通に当てる（reopen）」。§4.6 の A の 0 は「直の scanout の最中に 0° の出力へ 90 が届いた時: p012 からは session の規則（保存だけ、`rotation_paused = 1`、tell）」。
- 起こること: 180 を当てると turn は 0 なので session の意味が無くなるが、`rotation_paused = 1` と 90° の `turn_kept` が残り、終わりの「TURN の経路で戻す」は 180（REOPEN）と食い違う。0 を選ぶと kept は session の終わりまで memory を 2 倍持つ。270 を選ぶと kept の turn は 90 の印のまま。0° の出力の直の scanout で 90 が来た時に `rotation_paused` だけ立てると、`struct kwl_game_session` の `active`・`window`・`started_ms` が無いまま「session」になり、終わりの判定が働かない。
- 直し方: 180・0 の選択は session を終えて kept を捨てる、270 は kept の turn の `rotation` を書き換える（大きさは同じ）、0° の出力の直の scanout の 90 は session の記録（`active = 1`、`window = top`、`waiting = 0`）を作る、と書く。

### M20. session の入り口の資源の失敗で毎 pass 作り直しを繰り返す（§4.12 の入り口、§4.6 の A の 3）

- 根拠: 入り口は TURN の経路で 0° の scene・layers・壁紙・blur を先に作り、「どれかが失敗したら新しい物だけ捨てて UNSUPPORTED」。失敗は窓に何の印も残さない。
- 起こること: memory が足りない時、次の pass でまた UNROTATE が返り、毎 pass 大きな画像の作成・破棄と壁紙の CPU の描画を繰り返す（frame が止まる）。
- 直し方: 入り口の失敗は `scanout_refused = window`（または session の失敗の印）にして、その窓では二度と UNROTATE を返さない。log `KWL SCANOUT unrotate failed errno=`。

### M21. `kwl_arrange_output_resized` は refit の中にあり、窓が入らないと arrange の mode を終える（§4.6 の A の 7、§4.12 の「窓」）

- 根拠: §4.6 の A の 7 は「窓の refit（§4.12 の session では飛ばす）、`kwl_arrange_output_resized`、pointer の clamp」と別々の手順に書く。実際は `kwl_arrange_output_resized` は `kwl_glass_output_resized` の中の最後（`shell.c:2811`）から呼ばれ、新しい作業領域に窓が入らないと `arrange_end(..., "output")` で mode を終える（`arrange-shell.c:974-977`）。
- 起こること: 「refit を飛ばして arrange は走らせる」は今の関数の形では書けない。走らせれば session の入り口で arrange の mode が終わり、戻しても戻らない。普通の回転（0↔90）でも arrange の mode が終わることがあるが、利用者に見える影響として書かれていない。
- 直し方: session では arrange も飛ばす、と書く。普通の回転では「入らない時は arrange を終える（今の output switch と同じ）」を §4.4 と §14 に 1 行。

### M22. session の間に開いた窓の扱いが §11 と §4.12 で逆（§4.12 の「窓」、§11 の行）

- 根拠: §4.12 は「戻した時は元の論理に戻るので refit は要らない」、§11 は「session の間に新しく開いた窓（game の後ろ）は横の論理で置かれ、戻した時の refit（RESIZED の手順）で中に寄せられる」。
- 起こること: どちらで実装するかで、session の間に開いた窓（通知、game の launcher の dialog）が縦の desktop の外（x が 800 以上）に残るか、戻しで全部の窓が refit されるかが変わる。
- 直し方: 戻しでは refit を走らせる、と決めるのが簡単（`kwl_glass_fit` は中にある floating の窓を動かさず、fullscreen と docked は同じ大きさの configure になる、`shell.c:2763-2800`）。glass の作り直しと `kwl_outputs_changed` だけを飛ばす。S16 の mode の表にそう書く。

### M23. i915 の `cur_rotation` の更新の所と、flip しない経路（§5.2、S10 の直し）

- 根拠: §5.2 は「flip が成功した時に `cur_rotation = rotation`」。`cur_surf` は arm では変えず（`pend_surf = new_surf`、`modeset.c:2758`）、latch を確かめた所で変える: 同期の flip（`modeset.c:1215`）、完了の待ち（`modeset.c:1486`）、retire（`modeset.c:2798`）、点灯の modeset（`modeset.c:3096`）。また `i915_present_target`（`present.c:1552-1575`）と `i915_head_target`（`head.c:1186-1208`）は、armed の flip が latch していない時に `*flip = 0` で armed の buffer に描いて flip しない（FIFO では先に latch を待つので `I915_PRESENT_NO_VSYNC` の build と、待ちを外した経路だけ。`present.c:415`、`present.c` の head の待ち）。copied の経路は公開の `drv_i915_present(device, pixels, width, height, stride, bgra)`（`present.h:42`、`present.c:145-170`）が item を作る。
- 起こること: `cur_rotation` を arm で書くと、flip が stuck・NOT_LATCHED（`modeset.c:1217-1225`）の時に live の plane と食い違い、次の断りの戻しが 1 frame 逆さを出す。flip しない経路では、回転の変わった最初の frame が armed の buffer に入り、armed の回転で出る（NO_VSYNC の build で 1 frame 逆）。
- 直し方: `pend_rotation`（arm で書く）と `cur_rotation`（`cur_surf` を書く 4 か所で `pend` から写す、点灯は 0）の組にする。flip しない経路は「item の回転が armed の回転と違えば latch を待って flip する」。`drv_i915_present` の引数に回転を足す（呼び手は `present.c:427` だけ）。§5.2 にこの 3 つを書く。

### M24. 新しい ops の印を核の登録の検査が断る。commit の plane の検査は ops を持たない（§5.1、M17 の直し）

- 根拠: 核は device の登録で `drv_gpu_display_ops.flags` に `DRV_GPU_DISPLAY_UNGATED` 以外の bit があれば EINVAL を返す（`gpu.c:1095-1097`「An unknown flag cannot be honored by this core」）。§5.1 は `DRV_GPU_DISPLAY_ROTATE_180 2U` を i915 の resident の ops（`display.c:228-245`、今の flags は 0）に足すとだけ書く。`gpu_commit_plane_check`（`gpu.c:4050` 付近）は plane の state だけを取り、ops を見ない。
- 起こること: 書いたとおりに i915 の ops に印を足すと、i915 の device の登録が EINVAL で失敗し、画面が出ない（実機の 5330・5320 で初めて分かる）。commit（bcm2711、p011）の印の検査は今の関数の形では書けない。
- 直し方: §5.1 に「`gpu.c:1096` の既知の bit に `DRV_GPU_DISPLAY_ROTATE_180` を足す」「commit の検査は `gpu_commit_ioctl` で ops を見て、primary の `PLANE_ROTATE_180` を印の無い ops に EOPNOTSUPP」を書く。§12.1 の `host-gpu-display-flags` は、`gpu.c` に host 試験の枠が無い（`src/drivers/gpu/` に host 試験が無い）ので、flags の検査を純粋な関数に切り出して host 試験にする、と書く（「形があれば」では作られない）。

### M25. p009 の「compositor は触らない・p002 と並行可（file が重ならない）」は誤り（§5.3、§13 の p009）

- 根拠: §5.3 は `vkdemo_display_open_on()` に transform の引数を足す。compositor は `userland/tests/vkdemo/display.c` を自分の build に入れ（`wayland/Makefile:28`、`Makefile.linux:95`、`Makefile.freebsd:93`）、`compose.c:307` と `heads.c:837` で呼ぶ。p002a は `compose.c:307` の大きさの引数を `mode_*` に直す。
- 起こること: p009 だけを merge すると compositor の build が壊れる（3 つの OS）。直すと p002a と同じ行の衝突。
- 直し方: p009 は新しい関数 `vkdemo_display_open_on_transform(...)` を足し、今の `vkdemo_display_open_on` はそれを IDENTITY で呼ぶ形で残す（compositor は p010 で新しい関数に移る）。§13 の p009 の「変更する file」と「並行可」の理由をそう直す。

### M26. p012 の依存に WS223 p001 と p005 が無い。D18 は不要になった（§4.12、§10、§13、ws.md の p012 の行）

- 根拠: ws.md 末尾（2026-10-13 ユーザー）「Game Mode は別途、Wayland 拡張で要求があったときのみ使用」「Game Mode を利用したアプリは現段階では存在しない」→ D18 は不要、p012 の対象は game mode を拡張で求めた client だけ。WS223 の完了の条件 2 は「どの拡張を『求め』とするかは p001 で決める」。§4.12 の session の定義は `KWL_SCANOUT_CONTENT_GAME` に固定している。§4.12 の「配置の再検査の保存の抑え」は、回転の後の配置の再検査（§4.4）を前提にするが、それを入れるのは p005（§13 の p005「配置の再検査と `heads_windows_follow`」）。p012 の依存は p002b・p003・p004 だけ。
- 起こること: WS223 が `wp_content_type_v1` 以外の拡張を選ぶと、p012 の入り口の fact と試験（wltest の content type game）が変わる。p005 の前に p012 を実装すると、抑える対象の code がまだ無いか、p012 が配置の再検査を自分で作る。
- 直し方: §10 の D18 を「不要（2026-10-13 ユーザー、WS223）」に、§4.12 の session の定義を「WS223 p001 が決めた game mode の求めの拡張」に、§13 の p012 の依存に「WS223 p001、p005」を足す。T1 の束 E（p006+p012）は p012 の遅れで p006 を待たせないように分ける。

### M27. 試験の計画: i915 には host 試験があり、p012 の時間の規則は host 試験に入らない（§5.2、§12.1、§12.2 の 9）

- 根拠: §5.2 は「i915 の host の試験は無い（実機だけ）」。実際は `src/drivers/gpu/i915/tests/display/host-lcd-modeset-test.c` が `drv_i915_lcd_modeset_flip` を流し、arm の時の PLANE_CTL を見ている（`host-lcd-modeset-test.c:638-641` の `plane_ctl_at_arm == 0x94000000U`）。capture の経路も `host-capture-test.c`。§12.1 の host-scanout-rules は `kwl_scanout_decide`（時間を持たない純粋な関数）に UNROTATE を足すだけで、期限・hold・waiting は「定数」しか見ない。
- 起こること: M23 の pend／cur の誤りと断りの戻しの回転は host 試験で見つけられるのに、実機の目視だけになる。S15（期限と pointer の 2 秒）と S14（lock の往復）は T1 の注入の手順では起こらず、どの試験でも見えない。
- 直し方: p009 の受け入れに `host-lcd-modeset-test` の追加（rotation 180 の flip で PLANE_CTL の rotate の bit、flag の無い flip で 0、arm の後の断りで live の回転、stuck の後）。p012 は S15 の純粋な関数の host 試験（lock の間、pointer が動いている間、size が合った後、期限、hold）と T1 の「mouse を動かしながら始める」「game の上で lock して 10 秒待つ → `KWL SCANOUT unrotate` が 1 回だけ」。

## 軽微

- L38. §4.11「既存の呼び手 vkdemo の renderer・cursor-plane は IDENTITY」: cursor-plane は `vkdemo_display_open_on` を呼ばず自分で surface を作る（`cursor-plane.c:122-128`）。呼び手は `vkdemo_display_open`（`display.c:77`、renderer `renderer.c:622`）、`compose.c:307`、`heads.c:837`。§11 の「display の surface を作るのは compositor と vkdemo だけ」も、`libegl/vulkan.c:704`、`tests/display-events/main.c:526`、`cursor-plane.c:122` がある（全部 IDENTITY なので README の約束は保てる。文だけ直す）。
- L39. L27 の対応の venus の flags は今も不正確: `VIRTUAL_CLOCK | FIFO | ATOMIC_MODE_PRESENT | BLOB`、接続で `CONNECTED`、`ACTIVE`、`POWER_CONTROL | REFRESH_COUNTER`、`POWERED_OFF`（`venus/display.c:790-802`）。
- L40. L35 の `enable_psr2_sel_fetch` の読みは `plane.c:1210, 1361` のほかに `plane.c:1383`。
- L41. §6.3・L34 の直す所の列に `corner.c:428-430`（zone の log）と `corner.c:556`（角の hint の位置 `corner_x = (float)server->width`）が無い。plane の位置を足すと同じずれが出る。
- L42. §8「壁紙の CPU の描き直しは無い: 0° の壁紙は入る時に描き」は自分で矛盾する。session の入り口ごとに物理の大きさの壁紙を M の上で CPU で描く（`glass.c:828-866` の経路）ので、configure の前に数十 ms 止まる。0° の組も保つか、費用として書く。
- L43. `compose_display` は display の選択の後に `properties` を free する（`compose.c:1370`）。`hw180` は free の前に `properties[index].supportedTransforms` から取る、と §4.10 に 1 行。
- L44. 今の switch は大きさに関係なく `kwl_outputs_changed` を送る（`output-switch.c:259`、refresh と名前が変わる）。§4.6 の B で「比較に置き換わる」のは `output_resized` だけで、`kwl_outputs_changed` は switch では常に送る、と書く（RESIZED だけにすると同じ大きさで refresh の違う display への switch で wl_output の mode が古いまま）。
- L45. ws.md の「設計の要点（p001 第 3 版 §0）」は古い: 「swapchain の `preTransform` に ROTATE_180 を受ける」（S11 で不採用）、「回転した出力では game mode を合成に落とし」（C6 で p012 に置き換え）。Resume point と Phase の表の「p012 は D18 の後」も新しい決定で不要。Q1 が直す。
- L46. ROTATED の「pointer の同じ物理の点への warp」は、switch で display が替わる時は意味が無い（別の panel）。display が替わる時は今の clamp、と書く。
- L47. 資源の失敗（`WHY_RESOURCE`）で 0 で開いた後、利用者が同じ 90 の絵を押しても step 0 で何もしない（§7.3「選択と同じ絵は何もしない」）。選択と実効が違う時は押すと当て直す、とする。
- L48. session の間の一時の head の配置: Settings の Apply は今の head の位置を `compose->config` に書いて保存する（`heads.c:630-645`）。一時の位置を `compose->config` に入れないこと、session の間の Apply は保存の `place=` を使うことを書く。`struct kwl_game_session` の `window`・`ended_window` は窓が去る時（`kwl_scanout_surface_gone`）に NULL にする（ポインタの比べの取り違えを防ぐ）。
- L49. `struct kwl_rotate_state` は実効の値だけで `rotation_selected` を持たないので、純粋な関数が SAVE（「選択が変わった」）を決められない。open の経路で REOPEN・TURN の bit を落とすこと、open は自分で park しているので A の 8 の `kwl_compose_unpark` を open の経路では呼ばないことも §4.6 の B に書く。

## review-3 の指摘への対応の確認（第 4 版の対応の表を source で確かめた）

| ID | 確認 | 残り |
| --- | --- | --- |
| C5 | 組の比較と `kwl_rotate_steps` の 16 の組は正しい（12 の組の bit を §2 の写像で確かめた: 大きさは 0↔90・90↔180・180↔270・270↔0 で変わり、REOPEN は scanout が変わる 6 つ）。ただし before の取り方が switch で壊れる | **C7**、L44、L46、L49 |
| C6 | 方針（session と直の scanout を分ける、保存しない、配置と窓を動かさない、資源を保つ）は review-3 の提案どおり | **S12〜S16**、M19〜M22、M26 |
| S10 | 解決（item の `rotation`、flag の無い present は 0、点灯・relight・retained は 0、head も同じ item）。worker の item `worker.h:73-89`、flip `present.c:1586-1640`、arm `modeset.c:2700-2770`、点灯 `modeset.c:3090-3097`、PLANE_CTL は flip ごとに noarm → arm で書く（`plane.c:458-490` の `drv_i915_lcd_ms_plane_update_flip`）| M23（pend／cur と flip しない経路）、M27 |
| S11 | 解決。capabilities `wsi-display.c:584-585`、swapchain `wsi-swapchain.c:1213-1215` は変えない、surface の作成 `wsi.c:620-624`、写し `wsi.c:661-662`。present の 2 つ `wsi-display.c:1412, 1850`（後者は copied の `display_present_native`）と commit の primary `wsi-display.c:2401` だけに flag | L38 |
| M11 | 解決。`grep` は 11 の組（`backdrop.c:71, 132`、`cursor-plane.c:509-510`、`compose.c:458-459, 2016-2017, 2604-2607, 2637, 2796-2797, 2812-2813, 2817-2818`、`layer-cache.c:101-102`、`heads.c:1200, 1204-1205`、`shot.c:432, 437-438`）で表と合う | — |
| M12 | 解決（`wallpaper_create`・`blur_create`・`wallpaper_draw` は `server` と `glass` だけを取り `glass.c:758, 828, 1152`、`wallpaper_render` は既に幅・高さ `glass.c:867-870`、loader `glass.c:730-731, 1716-1717`） | L42 |
| M13 | 解決（open で 0 に落ちて保持、`WHY_RESOURCE`） | L47 |
| M14・M15 | 解決（close は cursor の plane `compose.c:518`、heads `:524`、layers・scene・backdrop・targets `:527-530`） | S16 |
| M16 | 解決（`keiland-desktop.in:40-41` の exec に試験の時だけ足す） | — |
| M17 | 方針は解決 | **M24** |
| M18 | 解決 | — |
| L26〜L37 | 解決（L26 `compose.c:891`・`backdrop.c:180-184, 300-306`、L28、L29、L30、L31、L32、L33 `plan/ws214/ws.md:11`、L34 の方針、L36、L37）。L27・L35・L34 の列に漏れ | L39、L40、L41 |

## 確かめた点（source と照らして正しかった所）

- game mode: 規則の順（`scanout-rules.c:30-60`、content は video と game `:37`）、facts（`scanout.c:158-197`、size は `server->width/height` と比べる `:188-190`、refused `:195-196`）、入り口 `scanout.c:274-303`、leave `:108-135`、窓が去る `:141-155`、pass の構造（`display.c:178-201`: `kwl_scanout_pass` の後に `!server->windowed` なら `enter_window_mode`）、fullscreen の configure `protocol.c:1729-1731`、bounds `protocol.c:1832-1839`、fullscreen で system bar が隠れる `shell.c:7738-7764`。
- refit `kwl_glass_output_resized`（`shell.c:2733-2812`、floating は `kwl_glass_fit` で中に寄せるだけ、arrange は `:2811`）、`output_resized`（`output-switch.c:658-688`）、switch の順（`output-switch.c:201-261`）、`kwl_compose_output_prepare`（`compose.c:292-343`）、`compose_output_open_parked`（`compose.c:373-485`）、close（`compose.c:493-547`）。
- `heads_save` は `compose->config` を書くだけ（`heads.c:1517-1532`）なので、`persist = 0` で config を触らなければ `rotate=` は残る。
- UAPI: `gpu_display_info.flags` は 1〜2048 を使い 4096 は空き（`gpu-display.h:23-36`）、present の flags 1・2（`:41-42`）、plane の flags 1・2（`:54-55`）。核の present の検査 `gpu.c:3525-3531`（PRESENT_SYNC も同じ `gpu_display_ioctl` の present を通る `gpu.c:6649` 付近）、commit `gpu.c:3990-4040`。struct は変わらず ABI は保てる。HAL（`include/hal/hal.h`、`src/hal/`）は変わらない。
- i915: `hw.rotation = MODE_ROTATE_0` は `plane.c:293, 378`、`i915_skl_plane_ctl()` が `rotation & MODE_ROTATE_MASK` を読む `plane.c:903, 926`、stride の mult は回転を見る `plane.c:604-612`。i915 の ops の commit・overlay_info は NULL（`display.c:228-245`）、capture の ops（`capture.c:171-188`）。display_query の 2 か所（`display.c:2750-2757, 3360`）。head の flip も `drv_i915_lcd_modeset_flip_nowait`（`head.c:1213-1231`）。
- MIT の参照: linear で断るのは REFLECT_X（`skl_universal_plane.c:1431-1435`）と 90/270（`:1437-1442`）、CCS の 90/270（`:1423-1428`）。src の回転は `intel_atomic_plane.c:918, 936` で回して戻す（全画面では同じ矩形）、GTT の view の回転は 90/270 だけ（`intel_fb.c:1719, 1886`）。
- libvulkan の output の flag は 1〜32 で 64 は空き（`wsi-internal.h:20-27`）。
- bcm2711: frame の words（`display-flip.c:1697-1704`、`words[1] = 0`）、primary の位置（`display-flip.c:2234`）、flipped の読み（`list.c:330-332`）、flags（`display-device.c:698`）、present の検査（`:915, 1124-1125`）、ops の flags は UNGATED（`display-device.c:1508`、p011 で M24 の登録の検査を通る）。
- protocol: displays の表は requests 4・events 3、interface の版 `KL_SYSTEM_SINCE_SHOWN`（`system-protocol.c:505-528`）、`KL_SYSTEM_SINCE_PHONE_CALLS 33U` が最大で 34 は main と全 branch で空き、`KL_VERSION 92U`（branch には 83〜89 の古い値だけ）。result の FAILED は 6（`kl-system-protocol.h:950`）。

## 確かめていない点（推測を含む）

- Intel の PRM の 180 と linear の記述（p009-1）。skl 以降の 180 で PLANE_OFFSET／PLANE_SURF の補正が要らないこと（参照の `skl_check_main_surface` 相当は読んでいない。gen 9 未満の `i9xx_plane.c:297` は 180 で offset を補正するが対象外）。PSR・FBC・DMC と frame ごとの回転の切り替え（実機）。
- `kl_backend_scanout_open` と Vulkan の swapchain の claim が同時に取れないこと（S13）は `scanout.c` の注記と順からの推測で、kernel の lease の code は読んでいない。
- S15 の「普通の起動で毎回起こる」は、touch・mouse の click が `server->pointer_x/y` を動かすことを前提にした推測（touch が pointer を warp するかは読んでいない。mouse の click の前の移動では確実に起こる）。
- WS223 の後の game mode の求めの拡張が何になるか（p012 の入り口の fact が変わる）。
- game の client（SDL・GLFW）が fullscreen の大きさを configure から取るか wl_output の mode から取るか（session の間 wl_output を送らない案、review-3 から未確認のまま）。
- review-1〜3 から未確認のまま: OSK・Wiseview・App Home の縦の配置、lock 画面と sleep・resume・lid、head の refit の既存の経路、HVS の反転の向き（M18、Pi の実機）。
