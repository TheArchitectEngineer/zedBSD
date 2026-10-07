# ws113-p011 設計: i915 の 2 つ目の出力（head）

Revision: 2026-10-07 / q856-i01 / P2 / source baseline agent/p2 `bd1e567c6`（main `4be6ac8d4` を merge 済み）。
Status: 設計（design-reviewer の敵対的 review を反映、§9。§3〜§7 の本文で review と食い違う所は §9 が優先する）。実装は agent/p2 の head.c ほか（phase.md の「実装」）。

入力: [phase.md](phase.md)、[契約の確定](../phase001/contracts-beta2.md) の D-GOP・D-RELEASE・D-LIMIT、[p011a](../phase011a/phase.md)（resident の付け替え、connector ごとの ID と generation）、2026-10-07 ユーザーの DBUF の決定「2 つ目の画面を足す時に、1 つ目の画面を点け直す」（1 出力の時の DBUF・既存の eDP の run は変えず、足す時に resident を点け直す・一瞬消える）、P1（compositor の ws113-p004b、`userland/desktop/wayland/heads.c`）の口の要件 1〜6（下の §2）、2026-09-20 の DUAL の診断（eDP pipe A/DPLL0 + HDMI pipe B/DPLL1、`src/drivers/gpu/i915/tests/display/hdmi-output.c` の DUAL）。

## 1. 今の事実（読んだ source）

- resident の run（`modeset.c` の `drv_i915_lcd_kernel_resident_run`）は 1 つの出力を modeset の **screen 0** で show の道（`drv_i915_lcd_show_prepared`）で点け、その window の中で worker が全ての要求を serve する（`present.c` の `i915_present_window_serve` → `drv_i915_worker_serve_window`）。window を出ると stop の道で止め、2 枚の resident の buffer を返す。
- modeset の world は screen を 2 つ持つ（`I915_LCD_MS_SCREENS` 2、`ms_pool[]`・`ms_ops_pool[]`）。`drv_i915_lcd_modeset_*` は全て `world->ms_sel`（選んだ screen）に働く。DUAL の試験は screen 0 に panel、screen 1 に HDMI を `select` → `prepare` → `scanout_begin` → `commit_enable` で点け、`plane_disable` → `commit_disable` → `plane_released` → `scanout_end` で止める（show の道を使わない）。両方の cfg に `also_active_pipes`（相手の pipe）を入れ、DDB を 2 pipe の集合で計算する（`watermark.c` の `new_dbuf->active_pipes`）。
- 1 出力の resident の run は `also_active_pipes` 0 なので、DDB を 1 pipe で全部使う。2 つ目の pipe を足すには resident の DDB を計算し直す必要がある → ユーザーの決定で **resident を点け直す**。
- run の hook の文脈 `struct i915_lcd_kernel`（`display->lk`）は 1 つ: vblank の event（`event_armed`・`event_pipe`・`event_frame`）と frame counter の pipe（`vblank.c` の `i915_lcd_kernel_frame` は `k->p->pipe`）、power の参照の数（`power_refs`）を 1 つの run の分だけ持つ。`modeset.c` の `i915_kernel_display()` は `container_of(k, struct i915_display, lk)`。
- `drv_i915_lcd_modeset_flip_wait`（presenting thread が latch を待つ）も `ms_sel` を読む。worker が別の screen を選んでいる間に読むと別の pipe を待つ（競合）。
- lease は 1 つ（`display->rd`、`rd->mutex`）。claim（`display.c` の `i915_display_claim`）は resident でない connector を、lease が無ければ resident の付け替え（`i915_display_move`）、在れば `ENOSPC`（D-LIMIT の暫定）。
- 他の connector の query は `GPU_DISPLAY_CONNECTED | GPU_DISPLAY_LIMITED`（ACTIVE 無し）。libvulkan は LIMITED を compositor に渡していない。
- worker（`worker.c` の `i915_worker_loop`）の display の item: PRESENT・PRESENT_BLOB（window の外なら ENTER_DISPLAY で入る）、RELEASE（window の中なら hold を始めて残る）。

## 2. P1（compositor）の口と、この設計の答え

| # | P1 の要件 | この設計 |
| --- | --- | --- |
| 1 | anchor を先に開き、head は anchor が開いた後だけ。display ごとに swapchain の作成 = CLAIM（anchor の lease を持ったまま）、最初の present で点灯 | claim は lease が在る時の別の connector を head にする（§4.1）。点けるのは head の最初の present（§4.3） |
| 2 | head の swapchain の作成の失敗（ENOSPC → VK_ERROR_INITIALIZATION_FAILED を含む全て）は致命的でない。次の hotplug の fence の signal まで再試行しない | claim の拒否は全て `ENOSPC`・`ENXIO`・`ESTALE`・`EOPNOTSUPP`（§4.1）。head の点灯の失敗は present の `ENXIO` と「limited の latch」（§4.5）で、同じ topology の sequence の間の再 claim は `ENOSPC`。`EIO`（libvulkan で DEVICE_LOST）は head の失敗に使わない |
| 3 | hotplug の fence の signal で列挙し直す。一覧から消えた display、acquire・present が OUT_OF_DATE / SURFACE_LOST の head は閉じる（RELEASE）→ まだ一覧にあれば開き直す（新しい generation で CLAIM）。RELEASE 直後の同じ connector の再 CLAIM を受けてほしい | head の release は worker で同期に止めて消し（§4.4）、戻った時には次の claim を受ける。切断は `ENXIO`（SURFACE_LOST）、generation 違いは `ESTALE`（OUT_OF_DATE） |
| 4 | 閉じる順は head を全部閉じてから anchor。anchor の移動の時は head は開いていない | resident の release・move の前提に head の無いことは要らない（head が在れば window を出る時に先に止める、§4.6）が、通常の順はこの通り |
| 5 | 同じ queue・submit で anchor を描き、head へ blit。present は anchor の後に head ごとに 1 回。拡張の head は開いた時と wallpaper の変化の時だけ、mirror は毎 frame | head の present は resident と同じ FIFO の道（GPU copy・flip、§4.3）。head の flip の wait は head の screen を明示する（§3.4） |
| 6 | RELEASE の後の head の出力は消灯（hold 無し） | D-RELEASE: head の release で pipe を止める（§4.4） |

## 3. 決定

### 3.1 範囲（v1）

- head は **1 つ**（modeset の screen 1）。resident は常に screen 0。
- 組み合わせは **resident が panel（pipe A）、head が HDMI（DDI B、pipe B）か外部 DP（Type-C、pipe B）**だけ。それ以外（resident が HDMI・DP で head が panel、head が resident と同じ pipe）は claim で `ENOSPC`（同時に出せる数の制限として、D-LIMIT）。通常の流れ（蓋を閉じた時は panel を kept_off、開けた時は anchor を panel に戻してから外部を head に）はこの範囲で足りる（P1 の p004b）。逆の組み合わせは後（§8）。
- mode は head の connector の native の 1 つ（今の other の query・mode と同じ、retiming しない）。

### 3.2 head の状態（`internal.h` の新しい `struct i915_display_head`、`display->head`）

| field | 意味 | 守る物 |
| --- | --- | --- |
| `mutex`、`inited` | head の lease の mutex（resident の `rd->mutex` と別: head の latch の待ちが anchor の present を塞がない） | — |
| `owner`、`lease`、`sequence`、`present_tick` | head の lease（番号は `rd->next_lease` から取り、resident と重ならない） | `head.mutex`（`next_lease` は `rd->mutex`） |
| `claimed`、`connector`、`generation`、`pipe`、`output` | claim した connector と、claim の時に準備した output（`drv_i915_display_output_prepare`） | 書くのは claim・release（`head.mutex`）と worker、worker が読む `claimed`・`pipe` は device の IRQ lock の下で写す |
| `up` | head が点いている（worker だけが書く） | worker |
| `buf[2]`、`front` | head の 2 枚の buffer（head の mode の大きさ、XRGB8888 linear） | worker |
| `map_vm`、`map_va[2]`、`map_pages[2]` | head の buffer を presenting session の空間に写した所（GPU copy 用） | worker |
| `k` | head の run の文脈（`struct i915_lcd_kernel`）: 自分の hook・vblank の event・power の参照・`p`（head の pipe） | worker |
| `params` | `k.p` が指す run の parameter（port・pipe・transcoder・PLL） | worker |
| `limited_sequence`、`limited` | 点灯に失敗した connector の latch（§4.5） | IRQ lock |
| `broken` | head の停止が確かめられず buffer を捨てた（以後 head は点けない、§4.4） | worker |

### 3.3 run の文脈の分離

- `struct i915_lcd_kernel` に `struct i915_display *display` を足し、`i915_kernel_display()` は `k->display` が在ればそれ、無ければ今の `container_of(lk)`（試験の lk はそのまま）。resident の run は `lk.display = display` を入れる。
- head の文脈 `head.k` は `drv_i915_lcd_kernel_bind_ops(&head.k)` で hook を張り、`locks` は `display->lcdb_locks`、`d` は resident と同じ deps、`p` は `&head.params`（pipe B）。vblank の event と frame counter は head の pipe を見る。

### 3.4 screen を明示する flip の wait

- `modeset.c` に `drv_i915_lcd_modeset_flip_wait_screen(display, screen)` を足す（`ms_pool[screen]`・`ms_ops_pool[screen]` を使い、`ms_sel` を読まない）。resident の latch（`present.c` の `i915_present_latch`）は screen 0 を、head の latch は screen 1 を明示する。今の `drv_i915_lcd_modeset_flip_wait` は選んだ screen の版として残す（試験が使う）。
- worker の上の操作（prepare・commit・flip・poll・settle・status）は `select(1)` → 操作 → `select(0)` で包み、worker が head の操作から戻る時は必ず screen 0 を選んだ状態にする（resident の flip・backlight は screen 0 を前提にする）。

### 3.5 DBUF と点け直し（ユーザーの決定）

- resident の run は、始まる時に head が claim 済みなら（`head.claimed`、latch で limited でない）cfg の `also_active_pipes` に head の pipe を入れる。その run の pipe の集合を `window.run_pipes` に覚える。
- head の present が window の中で来た時、head が点いておらず `window.run_pipes` に head の pipe が無ければ、worker は **点け直し**を求めて window を出る（`window.relight = 1`、item は queue の先頭に残る）。`drv_i915_present_window` は relight の時、hold・戻し・失敗の判定をせずに resident の run をもう 1 度回す（今度は 2 pipe）。その window の中で head の item が点灯から行われる。
- 点け直しの間、resident（anchor）は一瞬消える（ユーザーの決定）。**resident の buffer は点け直しをまたいで保つ**: 点け直しの run は終わりに buffer を返さず（pin のまま）、次の run はそれを作り直さずに使う。window の終わりの「buffer A へ戻す flip」の前に、front が B なら B の中身を A へ CPU で写す（1 回、約 8 MB）。こうして点け直しの後の anchor は最後の絵のまま（次の anchor の present を待たない。拡張の anchor は damage が無ければ present しない）。
- 1 出力の時（head が claim されていない）の run は今と同じ（`also_active_pipes` 0、点け直し無し）。head の release の後も resident の DDB は 2 pipe の割り当てのまま（次の run で 1 pipe に戻る）。
- 2 pipe の run が失敗した時（CDCLK・帯域が足りない等）: hardware が retained でなく buffer が返っていれば、head を limited に latch し（§4.5）、1 pipe で run をもう 1 度回す。resident（GOP の出力）を `display_failed` にしない。retained なら今の失敗の扱い（`display_failed`）のまま。

## 4. 流れ

### 4.1 claim（`display.c` の `i915_display_claim`、呼んだ thread）

1. `drv_i915_display_which` で resident か他か（今のまま）。resident なら今のまま（resident の lease）。
2. 他の connector で、resident の lease が無い → 今の `i915_display_move`（p011a）。
3. 他の connector で、resident の lease が在る → **head の claim**:
   - 切断 → `ENXIO`。generation 違い → `ESTALE`（which が返す）。
   - head が既に claim されている（別の lease）→ `ENOSPC`。同じ connector の limited の latch が今の topology の sequence で立っている → `ENOSPC`。
   - `head.broken` → `ENOSPC`（log に理由）。
   - resident が panel でない、または head の kind が HDMI・DP_EXT でない → `ENOSPC`（v1 の組み合わせの制限、§3.1）。
   - `drv_i915_display_output_prepare`（hardware に書かない。DP は sink の probe、HDMI は EDID の mode と PLL）の失敗はその errno（`ENXIO`・`EOPNOTSUPP` ほか）。
   - head の pipe（`drv_i915_display_output_pipe`）が resident の pipe と同じ → `ENOSPC`。
   - `head.mutex` の下で owner・lease（`rd->next_lease` から）・`connector`・`generation`・`output`・`sequence = 0`、IRQ lock の下で `claimed = 1`・`pipe`。log `i915: display head: lease N claimed (connector C, KIND, pipe P)`。hardware には書かない。

### 4.2 query・mode・power・refresh

- query: head の connector は `CONNECTED | FIFO | BLOB`、点いていれば `ACTIVE`、claim 済みなら `LIMITED` を付けない。他の connector の `LIMITED` は「claim が今 `ENOSPC` になる」時だけ（resident の lease が在り、head が埋まっているか、pipe・組み合わせ・latch で断る時）。resident の lease が無い時は付けない（claim は付け替えになる）。
- mode: 今の other の mode のまま。
- power: head は `EOPNOTSUPP`（HDMI・DP に我々の light は無い。今の「他の connector は EBUSY」を head の時だけ変える）。refresh: head の query は `REFRESH_COUNTER` を出さず、refresh の ioctl は今の「点いていない connector」の扱い（境界は来ない）のまま。

### 4.3 head の present（`present.c`、呼んだ thread → worker）

1. `head.mutex` の下で lease の持ち主と番号、connector の generation（変われば `ESTALE`）、接続（切れていれば `ENXIO`）、frame の大きさ（head の mode より大きければ `EINVAL`）を確かめる。
2. latch: `drv_i915_lcd_modeset_flip_wait_screen(display, 1)`（head が点いている時だけ）。
3. worker へ PRESENT・PRESENT_BLOB の item（`struct i915_worker_present` に `head` の印を足す）。window の外なら worker は window に入る（resident の run は head の pipe を含む 2 pipe で、§3.5）。window の中で head の pipe が run に無ければ点け直し（§3.5）。
4. worker（window の中）: head が点いていなければ点ける（§4.3.1）。点灯に失敗したら item は `ENXIO`。点いていれば head の back buffer へ copy（CPU、または head の buffer を session の空間に写して GPU copy）し、screen 1 で flip を arm（`flip_nowait`）、`select(0)` に戻す。
5. 成功なら `head.sequence++`、`present_tick`。最初の frame は log。

#### 4.3.1 head の点灯（worker、resident が点いた window の中）

1. `head.k` を初期化（§3.3）、`head.params` を output から（HDMI: port B・pipe B・transcoder B・PLL は rule が決める、`reset_dplls` しない。DP_EXT: Type-C の port・pipe B・TC PLL）。
2. preflight: head の pipe と port の DDI が止まっている（今の `i915_kernel_preflight_hdmi`・`_dp_ext` を output を引数に取る形にして使う）。
3. cfg: `drv_i915_lcd_kernel_fill_cfg(&head.k, &cfg)` → kind の cfg（今の `i915_resident_hdmi_cfg`・`_dp_ext_cfg` を output を引数に取る形に）→ `cfg.also_active_pipes = BIT(resident の pipe)`。
4. buffer: 2 枚を head の mode の大きさで create・pin・黒・publish。cfg の fb を buffer A に。
5. `select(1)` → `prepare(display, &head.output.state, &cfg, &head.k.ops)` → `scanout_begin(A)` → `commit_enable` → `select(0)`。DP_EXT の link が train しない（`LINK_NOT_TRAINED`）時は、止めて（§4.4 の道）、`i915_resident_dp_ext_fallback` を output を引数に取る形で下げ、もう 1 度（下がらなくなるまで、有限）。
6. 成功: `head.up = 1`、`front = 0`、log `i915: display head: lit (connector C, WxH, pipe P)`。失敗: 点いた所まで止め、buffer を返し、`select(0)`、head を limited に latch（§4.5）。

### 4.4 head の release と停止

- release の ioctl（head の lease）: `head.mutex` の下で、点いていれば worker へ RELEASE の item（`head` の印）を出して待つ → worker は window の中なら head を止める（hold しない、D-RELEASE）。window の外なら何もしない（点いていない）。その後 owner・lease・`claimed` を消す。log `i915: display head: lease N released after F frame(s)`。
- 停止（worker）: `select(1)` → `flip_settle` → `plane_disable` → `commit_disable` → 成功なら `plane_released`・両方の buffer の `scanout_end` → unmap → unpin・destroy → `select(0)` → `head.k.power_refs` が全て 0 か確かめる。停止が確かめられない（`stop_unconfirmed`、`STILL_OWNED`）なら buffer を abandon し `head.broken = 1`（以後の head の claim は `ENOSPC`、log）。resident は止めない。
- session の close（`drv_i915_present_lease_close`）: head の lease も同じく release。

### 4.5 limited の latch（D-LIMIT）

- head の点灯の失敗、2 pipe の run の失敗（§3.5）で、その connector と今の topology の sequence（`drv_i915_hpd_topology_sequence`）を latch する。sequence が変わる（hotplug）まで、その connector の head の claim は `ENOSPC`、query は `LIMITED`。compositor（P1）は claim の失敗で limited にし、次の hotplug の fence の signal まで再試行しないので、点け直しの繰り返し（anchor の点滅）にならない。
- present の点灯の失敗は `ENXIO`（libvulkan: SURFACE_LOST → compositor は head を閉じ、次の look で claim → `ENOSPC` → limited）。

### 4.6 window を出る時

- `i915_present_window_serve` が serve から戻った後（resident はまだ点いている）、head が点いていれば先に止める（§4.4 の停止）。hold の終わり・park（sleep）・stop・点け直し（点け直しは head が点いていない時だけ起こる）のどれでも。head の lease は残り、次の head の present で（window に入り直して）点け直す。
- shutdown（`drv_i915_present_shutdown`）: window を出る時に head も止まる。

### 4.7 切断

- head の connector が抜かれた: present・wait は `ENXIO`、release は通る（停止して消す）。driver は自分で止めない（release を待つ、H07・H08）。resident は続ける。

## 5. file と変更

| file | 変更 |
| --- | --- |
| `display/internal.h` | `struct i915_display_head`、`display->head`、`struct i915_lcd_kernel` の `display`、`window.relight`・`run_pipes`・`keep_buffers` |
| 新しい `display/head.c`・`head.h` | head の claim の判断（純粋な関数に出して host 試験）、点灯・停止・frame（CPU・GPU copy）・flip・release・lease の close・query の flag |
| `display/display.c` | claim の分岐（§4.1）、query の flag（§4.2）、power の head（§4.2） |
| `display/present.c` | present・wait・release の lease の振り分け、latch を screen 明示に、window の serve の後の head の停止、`drv_i915_present_window` の点け直しと 2 pipe の失敗の扱い |
| `display/modeset.c`・`modeset.h` | `i915_kernel_display`、`flip_wait_screen`、resident の run の `also_active_pipes` と buffer の保持、kind の params・cfg・preflight・fallback を output を引数に取る形に |
| `worker.c`・`worker.h`（display/ の外） | `struct i915_worker_present` の `head`、loop の判断（head の present の点け直し、head の release は hold しない）。**Q1 の許可が要る**（i915 の worker、他の WS と衝突しないか） |

UAPI（`gpu-display.h`）・HAL・libvulkan・compositor は変えない。

## 6. 試験

- host: `plan/ws113/tests/` に head の claim の判断の表（lease の有無・head の有無・組み合わせ・pipe・latch と sequence・broken）の試験（head.c の判断の関数を compile して）。
- build: `make ZEDBSD_CONFIG=config/current-uat.mk BUILD=build/ws113-p011 vmunix`（warning 0、kernel include check・vmunix check）、`I915_TEST_CAPTURE=y` も。
- 実機（5330、eDP + HDMI、`flock /tmp/i915-hw.lock`、T1 の passthrough か UAT）: (1) compositor（p004b）で拡張: HDMI を挿す → anchor が一瞬消えて戻り、HDMI に wallpaper（`i915: display head: lit`）。(2) mirror に切り替え → HDMI に desktop の複製。(3) HDMI を抜く → eDP は続き、head は release（`released`）。挿し直す → 再び点く。(4) 1 出力（HDMI 無し）の起動と sleep・resume が今と同じ。(5) DP-alt（TC）の monitor があれば HDMI の代わりに。QEMU（Venus）はこの driver を通らないので対象外。

## 7. 危険と確かめ

- 2 pipe の DDB・CDCLK・帯域: DUAL の試験（2026-09-20、eDP 1920x1080 + HDMI 1280x720）で eDP + HDMI の 2 pipe は動いた。HDMI 1920x1280（5330 の monitor）で CDCLK が足りないと prepare が断る → §3.5 の 1 pipe への戻しと latch。
- PLL: resident の panel が DPLL0、head の HDMI は rule が DPLL1 を選ぶ（DUAL と同じ）。head は pool を reset しない。
- resident の buffer を点け直しで保つ道は新しい（今の run は毎回作る）。release の道（`i915_resident_release`）が保持の時に pin のまま返すことを確かめる。
- presenting thread の latch が `ms_sel` を読まないこと（§3.4）。worker 以外で modeset の world を触る所は他に無い（backlight・control は worker の上か、frame counter を直に読む）。

## 8. 範囲の外（後）

- resident が HDMI・DP で head が panel（boot の時の GOP が HDMI で、後に蓋を開ける等）。head が 2 つ以上（pipe C・D）。head の power・refresh の境界。head の mode の選択。

## 9. review の反映（2026-10-07、design-reviewer、F1〜F19）

| # | 指摘 | 反映 |
| --- | --- | --- |
| F1 | 点け直しの条件に limited・broken の除外が無い（無限の点け直し） | 点け直し・run の DBUF の予約・head の点灯は同じ述語 `i915_head_may_light_locked`（claim 済み・broken でない・latch でない・resident が panel・pipe が別）で決める。点けられない head の frame は点け直さず ENXIO |
| F2 | DBUF の device の状態が 2 screen で壊れる（幻の pipe、head の停止・点灯で pipe A の下で MBUS の join が変わる） | `drv_i915_lcd_ms_wm_compute_off` に keep_pipes を足した: 止める screen の old は device の今の状態（`drv_i915_lcd_dbuf_current`）、新しい active_pipes は「他の走っている screen の pipe とそれが予約した pipe」との積。head の停止は pipe B を予約のまま（slice・join は変わらない）、resident（最後の pipe）の停止で予約も消える（1 出力に戻る）。head の点灯・停止の後に `DBUF lit/stopped: pipes slices joined` を log（実機で確かめる）。DUAL の試験の HDMI の停止も pipe B を残すようになる（panel が B を予約している、より正しい） |
| F3 | head の失敗が EIO（libvulkan で DEVICE_LOST） | head の present の EIO・ETIMEDOUT は ENXIO に。latch の失敗も ENXIO。window の外の head の item は ENXIO。head の release は常に 0（停止の失敗は broken と log） |
| F4 | resident の付け替え・戻しが head を見ていない | head が claim されている間は resident の move を ENOSPC（head-rules）、`drv_i915_display_output_back` は head が claim されていれば戻さない。head の点灯の直前に F1 の述語で resident が panel・pipe が別かを確かめ直す |
| F5 | mutex の ABBA | 入れ子にしない: claim は rd→（離して）→head、lease の close は head を先に（rd を持たずに）、head の present は rd の owner を読んで離してから head。入れ子が要る時は head→rd の順だけ |
| F6 | window を出ると head が消えたまま（拡張の compositor は present しない） | window の終わり（sleep・hold の終わり）では head の buffer と最後の絵を保ち（dormant）、次の window の初め（`i915_present_window_serve`）に `drv_i915_head_resume` が点け直す（connector が繋がり、claim の generation のままの時）。release は dormant の buffer も返す |
| F7 | broken を teardown が知らない、停止の証拠が commit の戻り値だけ | `drv_i915_lcd_kernel_abandoned` が `head.broken` を見る。head の停止は commit の戻り値に加えて TRANSCONF（head の pipe）の enable と state の bit が 0 であることを求める |
| F8 | 点け直しの失敗の道と buffer の持ち主 | 点け直しは前の run が 0 で終わった時だけ。保持の buffer は「保持の印が在り両方が PINNED、大きさが同じ」時だけ次の run が使う（違えば返して作り直す）。2 回目の run が show の前で失敗したら buffer は保持のまま次の run へ。保持の release は成功に数える |
| F9 | 2 pipe の失敗の検出の時期（CDCLK・帯域は resident の prepare が見ない） | 2 pipe の run が失敗して何も保持していなければ、head を latch し、display を失敗にしない（spared）。もう 1 度回すのは display に何も渡す前の失敗だけ（`display_acquired` 0）。lit の後の失敗は window の終わりで、次の present が 1 pipe で点ける。高い mode の拒否は head の prepare（§4.3.1 の latch）で起こる |
| F10 | resident の lease が無い時の head の present | ENXIO |
| F11 | B→A の CPU copy の前に cache を捨てる | `drv_i915_gt_clflush` してから copy・publish（resident と head の両方） |
| F12 | head の k の dpcd・panel の hook が resident の eDP に繋がる | 未対応（推測の指摘）。HDMI の commit は dpcd・panel を呼ばない、DP_EXT は cfg の aux_emit を使う（resident の DP_EXT の run と同じ）。残りの危険として実機で log を見る |
| F13 | QGV・SAGV の帯域を 2 pipe の和で見ない | 未対応。5330 は SAGV off・最大の point。実機の試験で underrun が無いことを確かめる |
| F14 | latch が topology の sequence で解ける・1 枠 | latch は connector と claim の generation で（抜き差しで解ける）。1 枠のまま（head は 1 つ） |
| F15 | 点け直しで消える時間は 1 秒前後になりうる | ユーザーに伝える（Q1 経由） |
| F16 | back buffer へ書く前の flip_poll | `i915_head_target` が screen 1 で poll・retire してから選ぶ |
| F17 | resident の lease の確認と head の claim の間の競合 | 受け入れる（F10 で anchor の無い head の present は ENXIO） |
| F18 | 点灯ごとの PPGTT の VA の漏れ | 受け入れる（小さい、resident と同じ bump allocator）。残りに記録 |
| F19 | 選んだ screen を待つ flip_wait | presenting thread から呼ばない旨を注記（試験だけが使う） |
| 試験 | DBUF の遷移・点け直しの状態機械の host 試験 | display の host 試験の runner（`run-lcd-modeset-host-test.sh`）は今の tree に無い。復活させず、head の規則の host 試験（`plan/ws113/tests/host-head-rules.sh`）と、実機の log（DBUF の pipes・slices・joined、`resident display: ended PASS`、underrun 無し）で確かめる。実機の試験に login の hand-over・sleep と resume・2 出力の後の 1 出力・head の停止の後の PASS を足す |
