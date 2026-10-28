<!-- awesome-plan project=zedbsd record=ws290-p012 -->

# ws290-p012 表示

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §18 の p012（版 4.5、ghci.md cb937e6）。
- 状態: in-progress
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
