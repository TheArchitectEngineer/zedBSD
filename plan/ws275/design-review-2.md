<!-- design-reviewer の再レビュー 2（2026-10-19、ws275 design.md 改訂 2）。Q1 が報告の本文をそのまま置いた -->

## WS275 p001 改訂 2 の敵対的な再レビュー（design.md 改訂 2・proposed/hal-sun4u-names.md）

対象は `/home/awe/zedBSD-claude1/plan/ws275/design.md` と `/home/awe/zedBSD-claude1/plan/ws275/proposed/hal-sun4u-names.md` です。P1 の worktree の写しは main と同一でした（`diff -q` で差なし）。

作業は読むだけでした。source・plan の変更、build、QEMU、commit、rm はしていません。使ったコマンドは grep・sed・cat・diff・sha256sum・git log/ls-files です。`.internal/` は読んでいません。

---

### Blocker

ありません。

### Major

**M-1. BLIT の分割で MONO1 の画素数が誤っている（design §4.1 l.97-99）**
- 根拠: pcat の前段は MONO1 の 1 行を `uint8_t packed[128]` に copyin します（`src/drivers/platform/pcat/graphics/pcat-graphics.c:460`）。`minimum_stride > sizeof(packed)` なら EINVAL を返します（:529）。そのあと `row_buffer[column]` に **1 画素 1 byte** で展開します（:540、`row_buffer` は 4096 byte、:31・:37）。
- 設計の「MONO1 は 32768 pixel ごと」は、4096 byte を bit で数えた値です。この経路の実際の上限は 1024 pixel（packed で決まる）か、packed を広げても 4096 pixel（展開先で決まる）です。
- 起こる事:
  - pcat を写して 32768 で分けると、幅が 1024 を超える MONO1 は EINVAL のままです。M5 が MONO1 では直りません。
  - 「4096 byte の buffer」に合わせて packed を 4096 byte に広げると、展開で `row_buffer` を最大 28 KiB 越えて書きます。kernel の `.vfs_bss` が壊れ、user の ioctl から起こせます。
- 試験で見えない理由: Xzed は MONO1 を使いません。QEMU の試験は 1024 幅なので、RGB24 でも分割に入りません（1024×3＝3072 < 4096）。
- 修正案:
  - chunk は「展開後の buffer と packed の両方」で決める（MONO1 は 1024、packed を 512 にすれば 4096）。
  - MONO1 の chunk の境界を 8 の倍数にすると明記する（src の bit offset を byte で進めるため）。
  - p003 の host 試験に、幅 1025・1366・4097 の RGB24・INDEX8・MONO1 を足す。

**M-2. mouse の判定の手順が、存在しない UI を前提にしている（§5 l.226-227、ws.md 完了の条件 2）**
- 根拠: `userland/x11/zwm/main.c` に menu はありません（`grep -i menu` は 0 件）。zshell は taskbar だけで、click で起きるのは task の隠す・出すだけです（`userland/x11/zshell/main.c` の `task_click`）。
- さらに、boot-test.py の `read_text` は画面全体で**最良の原点を 1 つ**だけ採ります（`plan/tools/boot-test.py:259-271`）。zterm（22,34）以外の原点にある文字は、zterm の文字と同時には読めません。
- 起こる事: `expect:<zwm の menu の文字>` は満たせず、条件 2 の「mouse で操作できる」を判定できません。
- 修正案（実現性は推測）: zterm の機能で、正の regex で確かめられる手順にする。
  - zterm は左の double click で語を PRIMARY に選び、中の click で貼ります（`userland/x11/zterm/main.c:1458-1485`、`DOUBLE_CLICK_MS` は 400）。
  - 手順の例: `mouse:-2000,-2000` で左上に寄せる（Xzed が座標を clamp すると推測）→ 相対の移動で `ws275-ok` の上へ → `mouse:0,0,left` を 2 回 → `mouse:0,0,middle` → `expect:` で prompt の後に貼られた文字。
  - 2 回の click が 400 ms に入るかは未確認です。だめな時は、Xzed が描く pointer の移動を PNG の目視で見る、と代わりの判定に書く。

**M-3. D7(b) の fallback が、kernel が kernel_putc を publish した後は働かない（§4.2 l.124-125、§4.3、proposed の「framebuffer_present が 0 なら名前は全部 NULL」）**
- 根拠: §4.3 では sun4u-console.c が kernel_putc を publish します。`drv_sun4u_console_init` は `kern_platform_init` の最初に呼ばれます（`src/kern/platform/sun4u.c:45`）。publish の後、HAL は画面に描かず kernel に委ねます（`src/hal/sparcv9/bsp-sun4u/console.c:62-66`、hal.h l.82-84）。
- 起こる事:
  - loader が framebuffer を渡さない時、HAL は Bochs で 640x400 を出します。しかし kernel には名前が無いので THR にしか書きません。
  - 画面は HAL の早い行で止まり、login の prompt は出ません。fallback を既定にした理由（OBP の property が無くても画面が出る）が成り立ちません。
  - 通常の道（property がある）の試験では見つかりません。
- 修正案（どちらか）:
  - (i) HAL が fallback の時も同じ `sun4u.framebuffer.*` を返す。値は phys＝pci_memory_base＋BAR0、640x400、pitch 2560、HAL が qext を LE に書くので `BGRX8888`。名前の定義を「HAL が表示している framebuffer」にする。
  - (ii) 名前が無い時、kernel は kernel_putc を publish せず、今の `console_putc`→`hal_putc` の道を残す。

**M-4. 32 bpp の byte order の誤りが判定で見えない（§5、M2 の対応の検証）**
- 根拠: boot-test.py は `r+g+b >= 384` を点灯とみなします（`plan/tools/boot-test.py:214-216`）。
  - R と B が入れ替わっても、X の byte が B に入って白が黄色になっても、文字は読めて PASS します。
  - QEMU で通るのは、BGRX と XRGB のうち OpenBIOS が qext に残した方だけです。もう一方は一度も通りません。
- 修正案:
  - p003 の host 試験で、3 つの形の画素の詰め方（IE の窓での byte の逆転を含む）を確かめる。
  - T1 の 2 本（x8・x32）で、既知の色の画素を PNG から読んで比べる。例: Xzed の root の 0x203040（zterm の frame の外、例えば (1016,400)）と zshell の bar の 0x17212b。8 bpp は RGB332 に量子化した期待値と比べる。

**M-5. §8 の未確認が Phase の依存に入っておらず、最大の危険が p006 まで見えない（§7・§8）**
- 未確認の内容: OpenBIOS の display の node が `width`・`depth`・`linebytes`・`address` を出すか、`translate` と `color!` が効くか、OpenBIOS が qext に残す値、既定の `graphic_depth`、depth 32 に対応するか、IE の store の振る舞い。
- 起こる事: 実装の担当は QEMU を起動できません（AGENTS.md）。p002〜p005（約 11 LW）を手探りで書き、外れは p006 で初めて分かります。
- 修正案: p002 の後に T1 の観察を置き、それを p003 の依存にする。内容は、gdbstub で handoff の値を読むことと、HAL の早期の console の PNG を -g x8・x32・既定の 3 条件で撮ること。LW は +0.5〜1。

**M-6. 承認を求める boot.h の定義文が曖昧で、tree の既存の「XRGB8888」と逆の意味になる（proposed l.9-15、design §4.4）**
- 根拠: tree の「XRGB8888」は DRM の意味（LE の word、メモリでは blue が先）で 73 箇所使われています。
  - `src/hal/sparcv9/bsp-sun4u/framebuffer.c:94,251`（「a little-endian XRGB8888 word」）
  - `src/hal/riscv64/bsp-virt/framebuffer.c:146`（「XRGB8888 layout, which is blue first in memory」）
- 新しい `KERN_BOOT_FRAMEBUFFER_XRGB8888` はメモリで X,R,G,B の順なので、zedBSD の `BGRX8888`（＝DRM の XRGB8888）と逆です。
- 「address の順」は proposed の箇条書きにだけあり、承認の対象である boot.h の定義文（「the unused byte first followed by red…」）にはありません。M-4 のとおり試験でも見えないので、取り違えが残ります。
- 修正案:
  - 定義文に「bytes in increasing address order」と書く。
  - RGB332 の bit の並び（red は bit 7-5、green は 4-2、blue は 1-0）と ramp の値（pcat の `rgb332()`、`backend.c` と同じ）も定義文に書く。
  - sun4u の framebuffer.c を触る時に、上の 2 つの comment を直す。

### Minor

- **m-1. quiet の時は serial にも何も出ない**: klog は quiet の時に kernel_putc を飛ばします（`src/kern/klog.c:128-131`）。debug_write は publish 後に何もしないので、record は dmesg にしか残りません。pcat も debugcon だけなので整合はしています。ただ、今の sun4u（quiet でも serial に出る）からは挙動が変わるので、設計に明記してください。もう一つの道は、debug_write を「quiet の時だけ THR」にすることです。
- **m-2. lock の細部が未定義**:
  - suspend の flag は console の spinlock の中で立てる（ENTER の後に console が X の上に描かないため）。
  - 「panic の時は try」を判別する API がありません（`include/kern/lock.h` は `spin_trylock` だけ）。UP なので「trylock に失敗したら lock 無しで書く」と書けば足ります。
  - IE の窓に 64 bit の store を使うと、2 画素の順が入れ替わります。使わないと明記してください。
- **m-3. sun4u の text の ops に `reveal_fatal` が無い**: `include/kern/text-display.h:110-114`、`sun4u-console.c:73-84`。X の動作中に panic しても画面は X のままです。§4.3 に足してください。
- **m-4. §4.2 の形の判別に穴がある**:
  - depth 15・16（OpenBIOS が `-g …x16` に応えると推測）の扱いが無い。「渡さない」と書く。
  - qext の値が 0x1e1e1e1e・0xbebebebe 以外（register が無いなど）の時の扱いが無い。
  - loader が BAR2＋0x604 を読む手段が書かれていない。`bootloader/sparcv9/` に物理の load の helper は無く、`ofw.h` には `instance-to-package` も汎用の `call-method` もありません。p002 の 2 LW はきつめです。
- **m-5. §6 の漏れと誤り**:
  - 漏れ: `src/hal/sparcv9/cmain.c`（framebuffer の初期化の呼び出しと、extension の版を完全一致で確かめる所 `cmain.c:113-128` を 3 に）、`bsp-sun4u/framebuffer.h`、`plan/ws081/tests/host-ps2-wheel.c`（.sh だけでなく）、`plan/ws035/tests/refactor-refs.py:63`。
  - 誤り: `plan/ws073` に ps2-8042 の参照はありません（grep で 0 件）。その行は外してください。
- **m-6. Phase の分け方**:
  - p003 の新しい file（generic の前段・firmware fb）は、p005 の vmunix.mk まで sun4u の kernel で compile されません。p003 で kernel の build（warning 0）を確かめられないので、source の追加は p003・p004 に移してください。
  - `src/kern/platform/sun4u.c` は p003 と p004 の両方が編集します。
  - ws.md の依存: D6 は p003〜p005 にも要ります（どれも sparcv9 の file を変える）。D5 は p004、D1・D2 は p003 の依存です。
- **m-7. 古い記録**:
  - §5・§7・ws.md は「QEMU の引数の口と `mouse:` は Q1 待ち」としていますが、main に入っています。名前は `BOOT_QEMU_EXTRA` ではなく `QEMU_EXTRA`（`plan/tools/boot-test.sh:389-397`）、`mouse:` は l.31・432 です。
  - §9 の `fw-*` は F-107〜F-111 として登録済みです。
  - F-111 の再考の契機「実機の入手、またはユーザーの指示」は、§4.1 の「pcat の graphics を次に触る WS、遅くとも Beta 2 の後」と食い違います。
  - master.md の引用 l.651 は、今は l.652 です。
- **m-8. 試験の文字列**: `expect:ws275-ok` は打った行「echo ws275-ok」にも合います。`^ws275-ok$` にするか、出力が入力と違う形にしてください。
  - zterm の原点は code で決まっていて (22,34) です（`zwm/main.c:980` の frame は client の要求の x,y、`:1020` で client を (2,26) に入れる）。(20,8) は zwm が管理に失敗した時だけです。候補を残しても害はありません。
- **m-9. HAL から kernel への画面の引き継ぎ**: publish の時に fb を消すのか、HAL の行の下から続けるのかが書かれていません。kernel の grid は 80×(高さ/16) で、HAL は 80×25 です。
- **m-10. platform の境界**: pcat の `vgafont.c`（`drv_pcat_vgafont16`）を sun4u の kernel で compile するのは、driver の配置の方針（Guardrail l.18）を越えます。例外として記録するか、F-111 で boot-test.py の追従と一緒に移すと書いてください。
- **m-11. pcat の PS/2 の回帰が具体的でない**: bios-ide の login の試験では PS/2 は動きません（`type:` は serial）。`keys:` の step と、`plan/ws081/tests/ps2-wheel-qemu.sh`（T1-015 で使用）を回帰に挙げてください。
- **m-12. HAL の差分の D4(b)**: proposed の i8042 の行は D4(a)・(b) の 2 案を持ち、差分ごとの承認に対して具体的ではありません。D4 を先に聞くか、両案の差分を並べてください。

### 確かめて問題が無かった事

- **M1（二重の出力・再帰）は直っている**: publish の後の道は kernel_putc（klog と hal_putc から）と console_putc だけで、両方とも grid と THR に行きます。debug_write は何もしません。kernel_putc が kern_log_write と hal_putc を呼ばないので、再帰もありません（`klog.c:128-150`、`console.c:62-66`）。
- **zterm の文字の原点の計算は正しい**:
  - zterm は (20,8) に窓を作り、文字の baseline は (r+1)×16（`zterm/main.c:346,1036-1038`）。
  - libX11 の font は ascent 16・descent 0（`userland/x11/libX11/xlib.c:289-290`）。Xzed の `draw_text` は `top = y - height` で描くので（`xzed/main.c` の draw_text）、glyph の上端は r×16 です。
  - zwm を通すと (22,34) になります。zterm の前景 0xdcdde5 は boot-test の閾値を越えます。
  - `BOOT_TEXT_ORIGINS` は `boot-test.py:254-258` に実装済みです。
  - startx の `--size 800x600` で mode が 1024x768 の 1 つだけでも、`choose_mode` は pass 2 で選び、root はその mode の大きさになります（`xzed/main.c:585-610`）。
- **IE の窓の考え**: TTE は uncached・side effect・IE です（`space.c:1294-1298`）。BGRX は native の store、XRGB は byte を逆にした store、RGB332 は byte の store。いずれも正しいです。
- **qext の読み**: 0x1e1e1e1e と 0xbebebebe は byte の回文なので、読む側の endian に依りません。HAL の定数 0x1e1e1e1e とも合います（BE の値は QEMU の vga-pci.c の記憶に基づき、写しには無いので未照合）。
- **boot.h の追加は既存の利用者を壊さない**: 利用者は全部 RGBX・BGRX 以外を拒みます（`riscv64/loader.c:127`、`riscv64/fbcons.c:260`、`kern/platform/visionfive2.c:396`、`i915/display/bootfb.c:429`、`nvrtx/vram.c:527`）。memory の順という意味は `pcat/graphics/backend.c:921` と `riscv64/fbcons.c:293` に合います。
- **HAL の案の形は Guardrail に合う**:
  - hal.h は不変で、名前付きの値で渡す。VF2 の承認（guardrail.md:95）と同じ形で、`bootloader/` の header は HAL だけが読む（:101-103）。
  - boot.h は「recorded decision」の後に変える ledger であり（boot.h:8-14）、承認を求めるのは正しい。
  - 名前は計 10（framebuffer 8、i8042 2）で数が合います。sun4u は事前承認（:93-94）の対象外です。
- **登録の順**: `kern_platform_init`（`src/kern/main.c:143`）は `kern_vfs_init`（:216、その中の `drv_graphics_device_register` は `vfs.c:452-459`）より前です。backend を cdev の登録の前に選ぶ設計は成り立ちます。
- **その他**:
  - sparcv9 の linker script は `.vfs_bss` を持ちます（`platform/sparcv9/vmunix.ld:62`）。
  - `drv_input_device_register` と `kern_io_in8` の名前は実在します。
  - M3・M4・M8 は直っています。F-107〜F-111 は登録済みです。
  - LW の和は 14 です。D1・D2・D4〜D7 の選択肢と既定は ws.md のユーザーの決定（firmware fb だけ、card は口だけ、D8 は選ばない）と矛盾しません。D6 の「はい」の 7 つの数も合います。
- **出典**: QEMU の写しの SHA-256 は §10 と一致し、引用の行（ISA の i8042 は l.330、IRQ は l.610-619、VGA は l.621-624、fw_cfg の depth は l.709-711）も合いました。

### 確かめていない事（推測を含む）

- OpenBIOS の実際の振る舞い:
  - display の node の property、`translate`・`color!` の結果、qext に残す値。
  - 既定の `graphic_depth`、depth 16・32 への対応、`color!` の後に palette が保たれるか。
  - client program が特権で ASI の物理の load を使えるか。
- QEMU の `vga-pci.c` の qext の値と、register が無い時の読み（写しに無い）。
- graphics の間に、汎用の console が evdev の keyboard を受けるか（m2 の実挙動）。
- Xzed の pointer の clamp・加速、2 回の `mouse:` click が 400 ms に入るか。
- TCG で IE の窓に全面を描く時の速さ。
- 8042 を `src/drivers/isa/` に移した時の pcat・amd64 への影響。

### 結論

p001 は、このままでは設計として受け入れられません。

M-1〜M-6 はどれも小さな直しで足ります。直した後は、差分の確認だけで受け入れてよいと考えます。
