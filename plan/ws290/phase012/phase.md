<!-- awesome-plan project=zedbsd record=ws290-p012 -->

# ws290-p012 表示

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §18 の p012（版 4.14、ghci.md d9252e4）。
- 状態: cleared（2026-10-28 P3 q1644-i01: driver 1019・全 host 試験 PASS、GHCI 有りの amd64・riscv64 の warning 0、review の H1・M1〜M3・L1〜L8 を反映。panic の表示の範囲はユーザーの判断待ち（呼び手無しで残す）、boot framebuffer は v1 で解放しない）
- 依存: p007・p009・p011

## 範囲

表示: DISP_SET・FLIP、DISP_IRQ、copied・cursor の写し、reset の数え直し、panic、boot framebuffer、adv7511。+ `DISPLAY | DISPLAY_EVENTS`

詳細は design の該当の節（§4〜§16）と §14 の p012 の場面。

## 受け入れ

- p012 の場面 PASS
- 試験は host 試験と amd64・riscv64 の build（warning 0）。実機は p014 だけ。
- 見積り: 22 LW。

## 仕様の仮の扱い（ghci-review の回答で変われば直す）

design §19 の表のうち、この Phase の code に効く物を着手の時にここへ写す。

- 「memory line」（FLIP の base・FB_STRIDE の揃え、§13.2）の大きさを仕様は書いていない。driver は 64 byte（linear の行の揃え、layout.h の `GHCI_LAYOUT_ALIGN`）と読む（`GHCI_DISPLAY_LINE_BYTES`）。
- DISP_IRQ_ENABLE の書き手の種類は表に無い（RW とだけある）。host がどの状態でも書く物と読む（error の一時の mask に使う）。

## 記録

- 2026-10-28 P3（q1638-i01）: design §18 から立てた。
- 2026-10-28 P3（q1644-i01）: p011 から渡す物: 最初の自分の flip が取られた後に `ghci_memory_boot_release(device)` を呼ぶ（discrete の boot framebuffer の範囲を local の allocator に返す）、detach で表示を boot framebuffer に戻すか ENABLE = 0。window（BAR2）は CAP_BLT 無しの discrete の表示の写しに使える。multisample の行の pitch は N79 の答えまで §12.11 の P（layout.h の `ghci_layout_row`）。
- 2026-10-28 16:42 P3 の helper（q1644-i01、p3-ws290g、f52150714 から）: 実装の 1 段目。`display.c`・`display-scan.c`・`display-scan.h`・`adv7511.c` を 1.0 に書き直した（hcr.h・packets.h の include を外した）。送り方は `CMD_DISP_SET`・`CMD_FLIP`（32 bit の物理、16 byte の record は fence page の +64・+80）を display.lock → 門（`GHCI_HOLD_DISPLAY`、待たない）→ ring に置き、門が RETRY なら何も記録せず display.lock を取り直して後で置き直す。一部だけ置けた run は flip を置いた物として記録し、reset の数え直しが処理する。`ghci-regs.h` に DISP2 の register、`ghci.h` に表示の型と関数、`ghci.c` の start（cp_start の前に attach）・stop（executor の後、cp_stop の前に detach）・登録（`DISPLAY | DISPLAY_EVENTS`、cursor は DC_CAPS[8] と HW_CURSOR）・close、`cp.c` の IRQ（DISP_IRQ_STATUS を W1C してから IRQ_STATUS[3]）、`recover.c` の段 5（数え直し）と service の表示、2 つの `vmunix.mk` の list。host の cc で 6 file が warning 0。試験と kernel の build はこれから。
- 2026-10-28 P3 の helper: 2 段目。build（warning 0、exit 0）: `config-amd64-ghci.mk` → `build/ws290-p012-amd64`、`config-visionfive2-ghci.mk` → `build/ws290-p012-vf2`（display の 3 file を含む）。model に DISP2 を足した（register の種類 S・C・L・W・R、DISP_SET・FLIP の実行、vsync（`model_tick` ごと）、record と TAKEN0/1、HALTED・FAULTED の間の record の保留（STATUS[31]）、DISP_IRQ_STATUS と IRQ_STATUS[3]、BAD_WRITE の数、COPY_BUFFER の実行）。model の自己試験に DISP2 の場面（271 PASS）。driver の試験に表示の 11 場面（mode・present の shared と copied・queue と mailbox・discrete の BLT と window の写し・cursor・hotplug・error・reset の数え直し（ring と失せた flip、HALT 中に取られた flip の record、門の閉じた間の present）・boot framebuffer（取られるまで保ち、console が描く間は返さず、描かなければ返す）・panic・detach・DISP2 の無い device）。`ghci-driver-host-test` 942 PASS・0 FAIL。attach の 2 つの期待値を表示の分だけ直した（IRQ_ENABLE に DISPLAY、capability に DISPLAY | DISPLAY_EVENTS）。`ghci-host-test.sh` の driver・vk の link に display の 3 file を足した。
- 2026-10-28 P3 の helper: 3 段目。CI の 2 つ（`config/ci/config-amd64.mk` → `build/ws290-p012-amd64-ci`、`config/ci/config-visionfive2.mk` → `build/ws290-p012-vf2-ci`）も warning 0・exit 0。全体の `ghci-host-test.sh` の 1 回目は vk の registration の 2 つ（capability に DISPLAY | DISPLAY_EVENTS が増えた）だけ FAIL で、期待値を直した。自分の review で直した事: detach の終わりに DISP_IRQ_ENABLE = 0 と DISP_IRQ_STATUS の W1C（present = 0 の後に IRQ_STATUS[3] が鳴り続けないように）、reset の数え直しで失せた点灯の run は CTRL に依らず点け直す、error の dark は lease の終わりの印（dark_placed）を残さない、出典を file の頭から block へ移した、coding-style の §6〜§8（式で作る Boolean、loop の前の comment、分けた呼び出しは 1 行 1 引数、閉じ括弧の後の空行）。
- 2026-10-28 16:58 P3 の helper: 4 段目（最後の確かめ）。style の直しの後に `sh plan/ws290/tests/ghci-host-test.sh` が PASS（exit 0。words 141・ref 56・model 271・driver 942・old-names 42 source・vk 438・pipeline 175・execute 198・meta 98 と gsi1 の全部、FAIL 0）。GHCI 有りの 2 つを build し直し warning 0・exit 0（amd64 の vmunix check PASS）。CI の 2 つは GHCI の file を含まない（log に display の file 無し）ので style の直しの前の build（warning 0）のまま。`git diff --check` 0。hcr.h・packets.h を include する物は無くなった（消すのは P3）。commit はしていない（P3）。

### 決めた事（helper、P3 の確認待ち）

- 門は待たない enter（`ghci_cp_enter(device, GHCI_HOLD_DISPLAY, 0)`）で通り、RETRY・LOST なら display.lock を取り直して EAGAIN・EIO を返す（何も記録しない）。置き直しは回復の後の service の表示の仕事（`ghci_display_service` を `ghci_service_step` の毎回の round で呼ぶ）と present・待ちの loop の `ghci_scan_update`。
- 表示の packet の後にはいつも driver の `CMD_FENCE` を置く（hang の判定と dark の ticket）。
- 一部だけ ring に置けた run（回復が途中で始まった）は flip を置いた物として記録し、段 5 の数え直し（STATUS[29]・[30]、TAKEN0/1、CTRL.ENABLE）に任せる。
- lease の終わりは dark（今の code と同じ）。detach は boot の image を保っている間は boot の image を点け直し（ring、だめなら HALT の後に host）、返した後と boot の無い時は dark。panic も保っている間だけ boot の image を host が点ける。
- idle frame（connect され lease が無い時に一度点ける黒い frame）は discrete でも host memory（4 GiB 未満、仕様は host の aperture の scan を許す）。cp_start の前に取るので local の allocator と window に依らない。copied の 2 つの frame と cursor の 2 つの surface は discrete では local（claim の時）。
- cursor の写しは CPU（16 KiB。local は window）。copied の present は UMA が CPU、discrete は CAP_BLT が有れば frame を session 0 に常に map しておき、source を一時に session 0 に map して `CMD_COPY_BUFFER`（stride が同じなら 1 個、違えば行ごと）、無ければ window を開けて CPU。
- 1.1 の bring-up 用の報告（`ghci_display_report`・`ghci_adv7511_report`・`ghci_display_idle_frame`）は呼ぶ物が無い（U6 で diag・paint を消した）ので外した。

### 残り（open）と提案

1. **boot framebuffer の解放と console**: kernel には「framebuffer の console や graphics の backend が、ある物理の範囲にまだ描くか」を問う API も、そこから外す API も無い（`graphics-backend.h` の乗っ取りは boot の時だけ、`kern_text_register` は差し替えだけ）。driver は boot display を見つけたら `boot_console` = 1 にし、最初の自分の flip が取られても `ghci_memory_boot_release` を呼ばない（範囲は確保の外のまま、discrete は window も開いたまま。p011 の前提「描く物が無い時だけ呼ぶ」を守る側）。HAL（hal.h）の差分は要らない。kern の API の差分の案（include/kern。承認と別の WS が要る）:

   ```c
   /*
    * Stops every console and graphics backend of the kernel drawing to a
    * run of physical memory, such as a boot framebuffer a display driver
    * takes over: the text console keeps its other outputs.  Returns 0 once
    * nothing of the kernel draws there, or EBUSY when a backend in use
    * cannot leave it.  The caller holds no lock.
    */
   int kern_framebuffer_leave(uint64_t physical, uint64_t bytes);
   ```

   driver は最初の flip が取られた後（lock 無し）に、CPU から見た boot framebuffer の物理（UMA は FB_BASE、discrete は `window_physical + (FB_BASE − LOCAL_BASE)`）と `FB_STRIDE × 高さ` で呼び、0 なら `boot_console` = 0 にして `ghci_memory_boot_release` を呼ぶ。試験は `boot_console` = 0 を直に置いて解放の道を確かめている。
2. **panic の呼び手**: kernel の `kern_fatal`（include/kern/panic.h）に表示の driver を呼ぶ口が無いので、`ghci_display_panic` を呼ぶ物は今は無い。案: `kern_panic_display_register(void (*show)(void *argument), void *argument)` を kern に足し、`kern_fatal` が止める前に呼ぶ（lock を取らず眠らない関数だけを登録する約束）。
3. 実機（p014 の段 3）で確かめる事: memory line の 64 byte、KC705 の DC_CAPS（BGRA8・RGBA8 の bit、cursor、mode の数）、DISP_IRQ の順、HALT 中の record の保留（STATUS[31]）。
- 2026-10-28 17:40 P3 の helper（q1644-i01、review 969eb764d..93b1b6072 の直し、93b1b6072 から。commit は P3）:
  - H1: service の round は待たない。`ghci_display_service` は display_lock を `mutex_trylock`（取れなければ round を飛ばし、DISP_IRQ の event は次の round に残す）、その間の配置は `display.service_placing` で `ghci_cp_try_enter`（`cp.c` に新設: cp_lock を try、回復中でなく、今 ring に run + fence の余地と log の空きがある時だけ OPEN、外の lock は手放さない）を通る。service の flush（`scan_flush(device, 1)`）は device.lock・display_lock を try で取り、取れなければ drop を残す。boot framebuffer の返却は service でしない（後の普通の flush が返す）。
  - M1: `ghci_cp_enter(…, wait = 0)` は RETRY・LOST で cp_lock だけを放し、呼び手の外の lock を保つ（`ring_put` の取り直しを外した）。wait = 1 の呼び手（vm.c・cp.c の submit）は前のまま。
  - M2: `ghci_cp_stop` は reset の後に `ghci_display_records_settle`（DISP2 STATUS[31] が 0 になるのを最大 100 ms 待つ）。消えず bus mastering が残る時は fence page を allocator に返さず quarantine に置く（device の間は返さない）。
  - M3: host が controller を変える時（detach の fallback、ring の使えない lease の終わり・dark、reset の数え直しの dark）、processor の HALTED と controller の IDLE の両方を見た時だけ shown・flip の memory を普通に放す。見ていなければ `display.host_unsure` = 1 で `unsure[]` に入れ、flush が `allocation.scan_unsure` = 1 を付けて hold を放す（最後の持ち主の `ghci_memory_unhold` が quarantine に入れる）。ring の dark の ticket が DONE、host の dark・light が idle を見た時に host_unsure を戻す。
  - L1: `detach_begin` は dark_placed を消さない。`detach_step` は回復に断られた dark を置き直し、ticket が DONE になってから shown を放す。dark の controller の shown はそこで放す。detach の終わりに回復が queue に戻した present を放す。
  - L2: 数え直しで、点灯の run の flip が取られ ENABLE が 0 のまま（lease の終わりでない）なら、shown を queue の先頭へ戻し（hold も移す）、点け直す。
  - L3: frame の大きさは `ghci_scan_frame_bytes`（全 mode の stride(w) × h の最大）。query の max_frame_bytes、copied の 2 frame、idle frame。
  - L4: cursor の x・y は 16 bit の符号付きの範囲に丸める（-32768〜32767、切り捨てない）。
  - L5: release は lease の memory の scan-out が退いた（dark の ticket と cancelled の record、ring の使えない時は host の dark）後にだけ 0 を返す。上限の時間を外し、lock を持たずに眠る。回復に断られた dark は release の loop が置き直す。
  - L6: `GHCI_DISPLAY_ADOPTION_MS` を 1000 から `GHCI_HANG_MS + 2000`（12 s）にした（present の queue の待ちも同じ値）。
  - 試験（`plan/ws290/tests/ghci-driver-host-test.c` に 8 場面と cursor の 1 点）: service の待たない round（4 KiB の ring を保持した processor で満たし、round が眠らず何も置かない、hang の時間の後に回復し idle frame が点く、display_lock・device.lock を他が持つ間は飛ばす）、待たない門が display_lock を保つことと閉じた門の 2 つの present が別の番号、stop の fence page（record が残り bus mastering が残れば quarantine、消えれば返す、bus mastering を切れば返す）、HALTED の見えない host の dark で image が quarantine、lease の終わりの dark を service が置いた後の detach が ticket を待って放す、点灯の flip の後・ENABLE の前で切れた run の点け直し、全 mode の frame の大きさ、release が 1 s を越えて待ち image を放してから返る、cursor の位置の丸め。
  - 結果: `sh plan/ws290/tests/ghci-host-test.sh` exit 0（words 141・ref 56・model 271・driver 1019・old-names 42 source・vk 438・pipeline 175・execute 198・meta 98 と gsi1 の全部、FAIL 0）。build: `config-amd64-ghci.mk` → `build/ws290-p012-amd64`（exit 0、warning 0、amd64 vmunix check PASS）、`config-visionfive2-ghci.mk` → `build/ws290-p012-vf2`（exit 0、warning 0）。`git diff --check` 0。
  - 未実施: CI の 2 つの config の build（GHCI の file を含まない）、実機（p014）。新しい試験を直す前の code で落ちることは理屈で確かめただけ（古い code に対して流していない）。

### 既知の制限と Future Work の候補（review の結果）

- L6 の制限: present の adoption の期限は ring の中の他の session の work の後ろにある。期限を hang の時間より長くしたので、動かない ring は回復で置き直されて間に合うが、submission ごとに hang の時間より短く進み続ける遅い ring では期限を越えうる（越えると lease の present は EBUSY）。
- L7（P3 の決定）: lease の終わりは controller を dark にする（idle frame を点け直さない）。
- `kern_framebuffer_leave` は v1 では作らない（P3 の決定）。`boot_console` = 1 のまま（boot framebuffer は返さない）。理由: firmware の framebuffer の backend（kernel の console）が boot framebuffer に描き続けるので、描く物を外す API 無しに返すと allocator が渡した memory に console が描く。Future Work の候補。
- panic の表示（`ghci_display_panic`）は v1 では呼ぶ物を作らない（P3 の決定、範囲は Q1・ユーザーの判断待ち）。理由: panic は lock を取らずに CTRL.HALT と host の書き込みをするので、回復の CTRL.RESET（cp_lock の下）と競る。Future Work の候補。

### 決めた事（helper、P3 の確認待ち）

- 門は待たない enter（`ghci_cp_enter(device, GHCI_HOLD_DISPLAY, 0)`）で通り、RETRY・LOST なら display.lock を取り直して EAGAIN・EIO を返す（何も記録しない）。置き直しは回復の後の service の表示の仕事（`ghci_display_service` を `ghci_service_step` の毎回の round で呼ぶ）と present・待ちの loop の `ghci_scan_update`。
- 表示の packet の後にはいつも driver の `CMD_FENCE` を置く（hang の判定と dark の ticket）。
- 一部だけ ring に置けた run（回復が途中で始まった）は flip を置いた物として記録し、段 5 の数え直し（STATUS[29]・[30]、TAKEN0/1、CTRL.ENABLE）に任せる。
- lease の終わりは dark（今の code と同じ）。detach は boot の image を保っている間は boot の image を点け直し（ring、だめなら HALT の後に host）、返した後と boot の無い時は dark。panic も保っている間だけ boot の image を host が点ける。
- idle frame（connect され lease が無い時に一度点ける黒い frame）は discrete でも host memory（4 GiB 未満、仕様は host の aperture の scan を許す）。cp_start の前に取るので local の allocator と window に依らない。copied の 2 つの frame と cursor の 2 つの surface は discrete では local（claim の時）。
- cursor の写しは CPU（16 KiB。local は window）。copied の present は UMA が CPU、discrete は CAP_BLT が有れば frame を session 0 に常に map しておき、source を一時に session 0 に map して `CMD_COPY_BUFFER`（stride が同じなら 1 個、違えば行ごと）、無ければ window を開けて CPU。
- 1.1 の bring-up 用の報告（`ghci_display_report`・`ghci_adv7511_report`・`ghci_display_idle_frame`）は呼ぶ物が無い（U6 で diag・paint を消した）ので外した。

### 残り（open）と提案

1. **boot framebuffer の解放と console**: kernel には「framebuffer の console や graphics の backend が、ある物理の範囲にまだ描くか」を問う API も、そこから外す API も無い（`graphics-backend.h` の乗っ取りは boot の時だけ、`kern_text_register` は差し替えだけ）。driver は boot display を見つけたら `boot_console` = 1 にし、最初の自分の flip が取られても `ghci_memory_boot_release` を呼ばない（範囲は確保の外のまま、discrete は window も開いたまま。p011 の前提「描く物が無い時だけ呼ぶ」を守る側）。HAL（hal.h）の差分は要らない。kern の API の差分の案（include/kern。承認と別の WS が要る）:

   ```c
   /*
    * Stops every console and graphics backend of the kernel drawing to a
    * run of physical memory, such as a boot framebuffer a display driver
    * takes over: the text console keeps its other outputs.  Returns 0 once
    * nothing of the kernel draws there, or EBUSY when a backend in use
    * cannot leave it.  The caller holds no lock.
    */
   int kern_framebuffer_leave(uint64_t physical, uint64_t bytes);
   ```

   driver は最初の flip が取られた後（lock 無し）に、CPU から見た boot framebuffer の物理（UMA は FB_BASE、discrete は `window_physical + (FB_BASE − LOCAL_BASE)`）と `FB_STRIDE × 高さ` で呼び、0 なら `boot_console` = 0 にして `ghci_memory_boot_release` を呼ぶ。試験は `boot_console` = 0 を直に置いて解放の道を確かめている。
2. **panic の呼び手**: kernel の `kern_fatal`（include/kern/panic.h）に表示の driver を呼ぶ口が無いので、`ghci_display_panic` を呼ぶ物は今は無い。案: `kern_panic_display_register(void (*show)(void *argument), void *argument)` を kern に足し、`kern_fatal` が止める前に呼ぶ（lock を取らず眠らない関数だけを登録する約束）。
3. 実機（p014 の段 3）で確かめる事: memory line の 64 byte、KC705 の DC_CAPS（BGRA8・RGBA8 の bit、cursor、mode の数）、DISP_IRQ の順、HALT 中の record の保留（STATUS[31]）。
- 2026-10-28 P3 の helper（review 969eb764d..93b1b6072 の直し、93b1b6072 から）: 途中の記録。code の直しを入れた（H1・M1・M2・M3・L1〜L6）。driver の試験は既存の 942 PASS のまま。直しの試験はこれから。
- 2026-10-28 P3 の helper: 直しの試験を driver の試験に足した（8 場面と cursor の 1 点）。`ghci-driver-host-test` 1019 PASS・0 FAIL。全体の試験と build はこれから。
- 2026-10-28 P3（q1644-i01）: review（Opus 5.5、969eb764d..93b1b6072）H 1・M 3・L 8、判定「H1・M1〜M3 を直せば L を記録して承認」。helper が全部を直し 8 場面を足した（上の記録）。design 版 4.15 の §13 に実装の形。確かめた事: `plan/ws290/tests/ghci-host-test.sh` PASS（driver 1019 ほか）。helper が GHCI 有りの amd64・VF2 を build し直し warning 0。新しい場面を古い code で FAIL させる確かめは未実施。
