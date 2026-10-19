<!-- design-reviewer の報告（2026-10-19、ws275 design.md 版 1）。Q1 が報告の本文をそのまま置いた -->

## WS275 p001 設計の敵対的レビュー（design.md・proposed/hal-sun4u-names.md）

対象: `/home/awe/zedBSD-claude1/plan/ws275/design.md`、`/home/awe/zedBSD-claude1/plan/ws275/proposed/hal-sun4u-names.md`（P1 の worktree の design.md・proposed は main と同一。ws.md は main の方が新しい）。
読むだけで行いました。source・plan の変更、build、QEMU、commit、rm はしていません。実行したのは読み取りのコマンドだけです（grep・sed・diff、出典の `sha256sum`、`/usr/share/qemu/QEMU,VGA.bin` と `openbios-sparc64` への `strings`）。

---

### Blocker

**B1. Xzed の画面を boot-test.sh で判定できない（§5・完了の条件 2）**
- 根拠: `plan/tools/boot-test.py:201-240` の `read_text` が試す原点は 3 つだけです。(0,0)、80x25 を中央に置いた位置、`width%8/2,height%16/2` です。glyph は 8x16 の格子に揃っている前提です。
- 一方、zterm の窓は `XCreateSimpleWindow(…, 20, 8, …)`（`userland/x11/zterm/main.c:346`）で作られ、zwm が frame の中の (`FRAME_BORDER`=2, `TITLE_HEIGHT`=26) に入れ直します（`userland/x11/zwm/main.c:30-33`）。文字の原点はおよそ x≡6 (mod 8)、y≡2 (mod 16) で、どの格子にも乗りません。
- 起こる事: `expect:<zterm の prompt>`・`expect:ws275-ok`・zwm の menu の文字は認識されず、p006 は必ず FAIL します。mouse の判定（focus の変化）も文字の差にならないので、同じく見えません。§5 の「pcat bios-ide で Xzed の回帰」も同じ理由で成り立ちません（今、Xzed の画面を判定する既存の試験はありません）。
- 修正案: Q1 に boot-test.py の変更を依頼し、p006 の外部依存として明記する。変更は、8x16 の全 offset（128 通り）を探索して認識数の最大を採る形。あるいは、判定を T1 の PNG の目視に切り替えると決める。

### Major

**M1. kernel_putc を publish すると、log が serial と画面に二重に出る（§4.3・D2）**
- 根拠: `src/kern/klog.c:128-131` が `kernel_putc` に書いた後、`kern_platform_debug_write` も呼びます（同 141-150 付近）。sun4u の `kern_platform_debug_write` は `hal_putc` に渡し（`src/kern/platform/sun4u.c:140-153`）、`hal_putc` は publish 後は `kernel_putc` に委ねます（`src/hal/sparcv9/bsp-sun4u/console.c:62-66`）。
- 加えて、今の `console_putc` も `hal_putc` を呼びます（`src/drivers/platform/sun4u/sun4u-console.c:157-164`）。
- 起こる事: kernel の全 record が 2 回出ます。もし `kernel_putc` に pcat と同じ `kern_text_kernel_putc` を使うと、quiet の時に kern_log_write → debug_write → hal_putc → kernel_putc と再帰します。
- 修正案: 出力の道を設計で 1 本に決める。
  - tty の出力（`console_putc`）と `kernel_putc` は、grid と 16550 の THR に直に書く。`hal_putc` は通さない。
  - `kern_platform_debug_write` は no-op にするか serial だけにして、二重を避ける。
  - grid の更新は IRQ-safe な spinlock 1 つで守る。
  - §6 の `src/kern/platform/sun4u.c` の変更内容に debug_write を足す。

**M2. QEMU の firmware framebuffer を「32 bpp・LE」とみなしているが、どちらも根拠が無い（§4.2）**
- byte order: QEMU の std VGA は、big endian の target では既定で framebuffer を BE で読みます。HAL 自身の注記（`src/hal/sparcv9/bsp-sun4u/framebuffer.c:15-17`）にそうあり、そのため qext 0x604 を LE に書いています。D7(a)（Bochs の直の設定をやめる）にすると、LE にする者が居なくなります。
- 深さ: QEMU は `graphic_depth` を fw_cfg で OpenBIOS に渡します（`sun4u.c:709-711`、写しの SHA は一致）。SPARC の既定は 1024x768x8 と推測します（未確認）。そうなら、`boot-test.sh` の sun4u（`-g` なし、`plan/tools/boot-test.sh:384-390`）で通る道は 8 bpp だけです。32 bpp・BE の道は QEMU で一度も検証されません。
- OBP の property には byte order が無く、loader は「LE／BE」を判別する手段を持ちません。
- 修正案:
  - sun4u の depth 32 は既定で BE xRGB（byte が X,R,G,B の順）とする。QEMU（1234:1111）の時だけ qext の register を読んで決めるか、明示して LE にするかを決める。
  - 試験は `-g 1024x768x8` と `-g 1024x768x32` の 2 本にする（boot-test.sh に `BOOT_QEMU_EXTRA` の口を足す依頼、Q1）。

**M3. 実機（Sun Blade 150）への道に、計画に無い依存がある（§4.2b・§7 p008・完了の条件 4）**
- loader は CMD646 が無い machine を拒みます（`bootloader/sparcv9/handoff.c:103-109`・`416`）。Sun Blade 150 の IDE は ALi M5229、keyboard と mouse は USB（OHCI）だけで PS/2 が無いと推測します（未確認）。
- §4.2b の「PCI の走査で既知の card」に対して、sun4u の kernel には PCI の config の access も PCI core もありません。pcat は `drv_pcat_graphics_pci_register` と PCI core で引き取っています（`src/kern/platform/pcat.c:368-375`）が、sparcv9 の source の一覧（`platform/sparcv9/vmunix.mk`）に PCI は無く、`hal.h` にも PCI の API はありません。
- Sun Blade 150 の標準の onboard は PGX64（Rage XL）で、OBP の screen が XVR ではなくそちらの可能性もあります（推測）。
- 起こる事: p008 は「実機だけで確かめる」とありますが、zedBSD が SB150 で起動しない限り clear できません。
- 修正案: 次のどちらかに分け、依存を Phase の表に書く。
  - sun4u の PCI の config（sabre・IIe）と、M5229・USB の入力を別の WS（または Future Work）にする。
  - card の照合を loader が OBP の tree から列挙した display node（id と assigned-addresses）に限る。

**M4. D3(a)（font を generic へ移す）が、全 platform の起動の判定と amd64 の HAL を壊す**
- 根拠: `plan/tools/boot-test.py:43,394-395` は `src/drivers/platform/pcat/graphics/vgafont.c` を path で読み、symbol `drv_pcat_vgafont16` で解析します。
- amd64 の HAL `src/hal/amd64/bsp-pcat/cons.c:29,817` も同じ header と symbol を使い、`platform/amd64/vmunix.mk:454` は graphics の有無に依らず compile しています。
- 他にも `plan/ws035/tests/guest-console.py`、`plan/ws073/tests/*.py`、`plan/ws174/tests/run-boot-keys-qemu.py`、`plan/ws256/tests/boot-test-host.py` が参照しています。§6 の表にはどれも無く、pcat の回帰では検出できません（boot-test 自体が壊れるため）。
- 修正案: D3 の既定を (b)「sun4u の kernel が pcat の vgafont.c をそのまま compile」に変える。移す場合は、Q1 による boot-test.py の追従を先に済ませることを依存に書く。

**M5. 行の buffer 4096 byte を写すと、実機の解像度で BLIT が黙って失敗する（§1「sun4u も同じ値」）**
- 根拠: `pcat-graphics.c:31,496-499`（RGB24 で幅 > 1365 なら EINVAL）、`460,529`（MONO1 は幅 > 1024 で EINVAL）。
- Xzed は mode を 1 つしか選べない時、全幅で BLIT し、戻り値を無視します（`userland/x11/xzed/main.c:796`）。pcat は複数の mode から 800x600 以下を選ぶので顕在化しません。
- 起こる事: firmware の mode が 1600x1200・1920x1080 などなら、画面に何も出ません。
- 修正案: 前段で 1 行を 4096 byte ごとに分けて backend に渡す。または上限を最大幅に合わせる。

**M6. HAL の承認の案が「具体的な差分」になっていない（proposed/hal-sun4u-names.md）**
- Guardrail（`plan/guardrail.md:13-16`）は差分ごとの承認を求めています。しかし `include/kern/boot.h` に足す定数の名前・値・意味が書かれていません。
- `boot.h:57-61` は「four bytes per pixel」と定義しているので、INDEX8 を足すと定義文も変わります。
- INDEX8 は palette が分からないと描けません。`color!` が失敗した時の扱いも未定義です。
- 「LE の xRGB」は既存の `BGRX8888` と同じなので、新設は不要です。
- 修正案:
  - 足すのは `KERN_BOOT_FRAMEBUFFER_XRGB8888`（byte が X,R,G,B の順、値 3）と `_RGB332`（loader が palette を設定済みの 8 bpp、値 4）の 2 つにし、定義文の差分を示す。
  - `color!` が失敗したら loader は fb を渡さない（または 2 色の扱いを明記する）。
  - `.size` が linebytes×height であることを明記する。

**M7. 実装の担当が QEMU を起動する前提になっている（§8、p002〜p004）**
- 「p003 の最初に gdb で 1 pixel」「p002 で monitor の info mtree」「p004 で monitor と gdb」とありますが、AGENTS.md の検証の節（2026-10-03）は「実装の担当は QEMU を自分で起動しない」です。
- 修正案: 確かめを T1 への依頼にまとめ、Phase の依存（T1 の結果待ち）と LW に反映する。または、例外にするならユーザーに聞く。

**M8. 完了の条件 3 と §4.6・D6 が矛盾する**
- 条件 3 は「base と X11 を arch で分けない」ですが、sparcv9 の root は `sh sysctl mount umount` と選んだ program だけです（`platform/sparcv9/vmunix.mk:273-276`）。base の全部の build はユーザーの担当として外されています（`plan/master.md:651`）。
- 起こる事: 本 WS では条件 3 を検証できません。
- 修正案: 条件を「X11 の program は platform `*` のまま、arch の分岐を足さない」に書き換え、base の全部はユーザーの作業として外部の条件にする。

### Minor

- **m1. QEMU の 8042 の translate**: §4.5 の「QEMU の 8042 の既定は set 2 を set 1 に translate」は、QEMU の reset 直後の状態としては誤りと推測します（translate は firmware が config byte で立てる。OpenBIOS は pc_kbd・8042 を持つことを strings で確認、設定値は未確認）。driver が poll の初期化で config byte（KCC の bit、IRQ の bit は落とす）を自分で書く、と設計に書くのが安全です。
- **m2. 「console の keyboard 入力は範囲外」は不正確**: 汎用の console は全 evdev の keyboard を subscribe します（`src/drivers/generic/console.c:1052`）。i8042 を登録すれば console の入力にもなります。Xzed は grab しないので、`keys:` が背後の root shell にも届きます（pcat と同じ挙動と推測）。設計に明記し、試験の文字列は shell に入っても無害な物に限ってください。
- **m3. libX11 の主張は影響を過大に見ている**: `Atom` は `uint32_t`（`include/libc/X11/X.h:68`）で、format 32 の native の memcpy は確かに BE で誤りです。ただ、Xzed は format 8 の WM_NAME と icon path しか保持しません（`userland/x11/xzed/main.c:2075-2100`）。format 32 の property（zterm の TARGETS）は捨てられ、GetProperty でも返りません。修正は正しいものの、完了の条件には効きません。§4.7 の「値が壊れる」は「潜在」と書くのが正確です。
- **m4. D1 の理由が D3・D5 と食い違う**: D1(a) は「pcat の回帰を巻き込まない」「写しを増やさない」としていますが、D3(a)・D5(a) は pcat の file を移すので回帰は必須です。また generic の前段は、移行が済むまで実質 3 つ目の写しです。移行の Future Work の ID と時期を書いてください。
- **m5. 引き取りの手順は簡単にできる**: card の driver は boot 時に一度に決まるので、「owner が居れば close まで保留」は不要と考えます（Xzed の close の中で mode の設定が走る形も避けられる）。backend の選択は `/dev/graphics` の登録の前に済ませ、text は既存の `kern_text_register` の置き換え（`include/kern/text-display.h` の「A second call replaces the first」）を使ってください。unmap は text の切り替えの後にし、`reveal_fatal` が古い写像に描かない順を明記してください。なお device window の VA は戻りません（`src/hal/sparcv9/space.c:452-456` の bump）。
- **m6. resume で描き直す範囲**: 80x25 の cell だけを描き直すと、1024x768 の残りに X の絵が残ります。resume では fb の全体を消してから描き直してください。
- **m7. `/chosen` の stdout が screen とは限らない**: 実機では stdout が serial（ttya）のことがあり、その時 firmware は display を開いておらず、mode も未設定の可能性があります。`/aliases/screen` への fallback と、loader による display の open を足してください。
- **m8. 細かな形式**: depth 24 の 3 byte の packed の箱（linebytes/width で判別）の扱いが未定義です。UPA・SBus の screen には PCI の id が無いので、`pci.vendor` は NULL になり得ます。
- **m9. D8(d)（QEMU の std VGA の card の driver）の副作用**: これを入れると、QEMU で firmware fb の道が通らなくなります。引き取りを止める設定（boot parameter など）を試験に用意してください。
- **m10. ps2 の移動で直す物が §6 に無い**: `plan/ws081/tests/host-ps2-wheel.sh:3`（WS081 は incomplete）、`plan/ws073/tests` の path、`ps2-8042.c:190-198` の host 試験の hook の保持。
- **m11. 記録の不整合**: ws.md の Phase の表が p001 だけです（design §7 には p002〜p010）。ws.md の完了の条件 1 の「framebuffer の写像」は、design の「mmap は無い」と表現が合いません。
- **m12. build の細部**: sparcv9 の `vmunix.mk` に `$(ZEDBSD_GRAPHICS_CONFIG_STAMP)` の依存を足す（pcat は `platform/pcat/vmunix.mk:82,362`）。Makefile の「Only PC/AT and PC-98」の注記（`Makefile:519-526`）と `docs/reference/control-devices.md:43-46` を更新する。

### 確かめて問題が無かった事

- **/dev/graphics の契約**: graphics.h の 11 の ioctl、ENTER 前に使える ioctl、ENXIO・EBUSY・ENODEV・EBADF、framebuffer の mmap が無いこと。いずれも `pcat-graphics.c` と control-devices.md:49-80 に一致します。pc98 の前段は pcat の前段の写しで、名前を置き換えると差は 10 行でした（design の「18 行」はおおむね正しい）。
- **Xzed の要る cap と endian**: 要る cap は FLUSH・GLYPH・BLIT_RGB24 です（`xzed/main.c:472-474`）。BLIT は R,G,B の byte 列（`:785-795`）、私用の RGB24 の upload も byte で組み立て（`:2593-2620`）、protocol は client の order で読み書きします。endian に依りません。
- **mode が 1 つでも動く**: `choose_mode` は mode が 1 件でも pass 3 で選びます（`:585-610`）。前段は bpp 8・32 の ENTER を受けます（`pcat-graphics.c:221-233`）。
- **IE の写像**: `resolve_device` の TTE は uncached・side effect・IE です（`space.c:1294-1298`）。LE の fb には native の store、BE の fb には byte を逆にした store、という §4.2 の写像の考えは正しいです。8 bpp は byte の store なので影響を受けません。
- **sparcv9 の port I/O**: `kern_io_in8` は PCI の I/O 空間の offset です（`src/hal/sparcv9/io.c`）。QEMU の ebus の ISA は `pci_address_space_io`、`TYPE_I8042`、IRQ 1・12 → OBIO です（`sun4u.c:304,330,610-619`）。HAL に IRQ は無く poll です（`irq.c:97-110`）。
- **poll にできる**: pcat の 8042 は status の AUX の bit で keyboard と mouse を分けるので（`ps2-8042.c:771-773,1119-1121`）、poll にしても分離できます。
- **userland の build**: X11 の 4 program は class basic・platform `*` で、`SPARCV9_USER_BASIC_COMMAND` で static に build されます。startx・Xzedrc・zwm.conf は zwm を選ぶと `ZEDBSD_PACKAGE_FILES` 経由で rootfs の rule に入ります（`Makefile:1132-1138,1349`）。
- **OBP の property と color!**: OpenBIOS の QEMU VGA の FCode（`/usr/share/qemu/QEMU,VGA.bin`）に `width`・`height`・`depth`・`linebytes`・`address`・`color!`・`set-colors` の文字列があります。openbios-sparc64 には `instance-to-package`・`translate`・`pc_kbd`・`8042` があります（strings で確認。動作は未確認）。
- **Guardrail との合い方**: hal.h の変更が要らないこと、名前付きの値という形（WS256 VF2 の framebuffer の承認と同じ形）、kernel が `bootloader/` の header を読まないことは Guardrail（`guardrail.md:99-103`）に合います。sun4u は 2026-10-18・19 の事前承認の対象外なので、承認を求めるのは正しいです。
- **出典**: §9 の QEMU の 2 file は SHA-256 が一致し、引用した行番号も合っていました。

### 確かめていない事（推測を含む）

- QEMU の SPARC の既定の `graphic_depth`（8 と推測）。OpenBIOS の screen の node の実際の depth、qext の byte order の register の値、`translate` の結果。
- QEMU の 8042 の config byte を OpenBIOS が設定するか（translate の有無）。
- Sun Blade 150 の IDE（M5229）、USB だけの入力、onboard の PGX64、XVR-500/600 を載せられるか（どれも記憶に基づく推測で、ユーザーの `show-displays` 等の確認が要る）。
- 実機の ebus・ISA の device を PCI の I/O の port として扱えるか。
- pcat で Xzed の動作中に、keyboard の入力が console の tty にも届くか（evdev の grab の有無は読んだが、実挙動は未確認）。
- LW の妥当性は、規模（前段 816 行の書き直し、8042 の 1269 行の移動）から見ておおむね妥当と判断しただけで、検証はしていません。

### 結論

p001 は、このままでは設計として受け入れられません。B1（判定の方法が成り立たない）と、M1〜M4・M6 を直した改訂の後に、再レビューが要ります。
