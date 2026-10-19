<!-- awesome-plan project=zedbsd record=ws275-p002 -->

# ws275-p002 loader の firmware framebuffer・i8042、boot.h、HAL の名前付きの値と早期の console

- 親: [ws275](../ws.md)、Queue: q1311 / q1311-i01（P1、2026-10-19）
- Status: cleared（2026-10-19 Q1 の判定: T1-743 で handoff の値と 3 条件の画面を観察、Q1 の判定で条件 1〜6 を満たす。ユーザーの「全部 merge する」で main に merge（50bd10c5f）。QEMU のみ・実機は未実施）
- 設計: [design.md](../design.md) §4.2・§4.4・§4.5・§6、HAL の差分 [proposed/hal-sun4u-names.md](../proposed/hal-sun4u-names.md)（2026-10-19 19:12 ユーザー「改訂 3 で承認」）

## 受け入れの条件

1. loader（stage2）が handoff 版 3 で firmware framebuffer（phys・width・height・pitch・format・PCI の id）と i8042 の port を渡す。
   display は `/chosen` の stdout（`instance-to-package` で display 型）か `/aliases` の `screen`（open して閉じない）。8 bpp は `color!`
   で RGB332 の palette、失敗なら渡さない。4 byte は既定 XRGB8888、1234:1111 だけ qext を読んで決める（書かない）。15・16・packed 24 は渡さない。
2. boot.h に承認済みの文面で `XRGB8888`=3・`RGB332`=4。
3. HAL の名前付きの値 10（承認済みの意味「HAL が表示している framebuffer」、fallback の時も返す、画面が無い時は NULL）。
4. HAL の早期の console が handoff の 3 形と任意の width・pitch に描き、無い時は Bochs の 640x400 の fallback（D7 (b)）。
5. build の warning 0（sun4u・amd64・pcat・riscv64）、host 試験、hal.h は不変。
6. p002b の T1 の観察（下の依頼文）。

## 行った事（2026-10-19 P1）

| commit | 中身 |
| --- | --- |
| 4d4559d0d | `include/kern/boot.h`: 承認済みの定義文と `KERN_BOOT_FRAMEBUFFER_XRGB8888 3U`・`RGB332 4U`（proposed (1) の diff のとおり） |
| 96cd3fa49 | loader: `handoff.h` 版 3（`framebuffer_*`・`i8042_*`、`display_*` は fallback 用に残す）、`handoff.c`（`find_display`・`framebuffer_bar`・`framebuffer_address`・`qemu_vga_format`・`set_rgb332_palette`・`discover_i8042`）、`ofw.h`/`ofw.c`（`ofw_instance_to_package`・`ofw_call_method`・`ofw_translate`）、host 試験 |
| 3766632d0 | HAL: `framebuffer.c`/`.h`（firmware fb の 3 形と任意の size、Bochs の fallback、`sun4u_framebuffer_shown()`、l.94 の comment を 0xRRGGBB に）、`boot.c`（名前 10）、`cmain.c`（init の呼び出し） |

- 物理 address は `address` を MMU の `translate` で引き、display の PCI の memory の BAR（最大の物）の中に入る時だけ使う。外れる・使えない時は
  BAR の先頭。`translate` の結果の並び（flag・mode・phys.hi・phys.lo）は OpenBIOS で未確認なので、BAR の範囲で確かめる形にした。
- i8042 は ebus の下の name `8042` の node の `reg`（1 つ目が data、2 つ目があれば command、無ければ data+4）。
- qext は loader が stage2 の中で ASI 0x1d（`ASI_PHYS_BYPASS_EC_E_L`）の `lduwa` で読む（firmware の上の特権で走る前提、p002b で確かめる）。

## 確認（2026-10-19 P1）

- `make ZEDBSD_CONFIG=plan/tools/ports/config-sun4u.mk BUILD=build/ws275-p002/sun4u vmunix bootloader -j8`: rc 0、warning 0、
  `SPARC V9 boot check: PASS (stage1=3141, stage2=11579)`、`SPARC V9 kernel check: PASS`。
- `config/ci/config-{amd64,pcat,riscv64-virt}.mk` の `vmunix`（`BUILD=build/ws275-p002/<名前>`）: どれも rc 0、warning 0。
- host 試験 `plan/ws275/tests/host-handoff.sh`（handoff.c を QEMU sun4u 風の偽の OpenFirmware の tree で走らせる。qext の読みだけ偽物に置換）:
  8 件（x32 qext LE・BE・他の値、x8 の palette 256 回と値、`color!` の失敗、x16 は渡さない、translate が BAR の中・外）`host-handoff: PASS`。
- 未実施: QEMU（p002b の T1）。HAL の framebuffer.c の描画（3 形）は host 試験していない（inline asm の物理 store のため。p002b の PNG で見る）。

## p002b: T1 への観察の依頼文

```
依頼: ws275-p002b（sun4u の firmware framebuffer の handoff の観察）。worktree /home/awe/zedBSD-worktrees/p1 の
commit 3766632d0 以降。image は plan/tools/ports/config-sun4u.mk の build（login まで）と同じ作り方。
1 つの QEMU の instance で、次の 3 条件を順に（各 1 回）:
  (A) QEMU_EXTRA='-g 1024x768x8'  (B) QEMU_EXTRA='-g 1024x768x32'  (C) -g 無し（既定の depth）
各条件で:
 1. gdbstub（-S -gdb tcp::<port>）で起動し、HAL の cmain の sun4u_framebuffer_init の入口で break。
    引数の handoff（%o0）の framebuffer_phys・framebuffer_width・framebuffer_height・framebuffer_pitch・framebuffer_format・
    framebuffer_vendor・framebuffer_device・i8042_data・i8042_command・extension_version と display_framebuffer・display_registers を読む
    （p *(struct sun4u_boot_handoff *)$o0、symbol は vmunix から）。
 2. finish の後に sun4u_framebuffer_shown() の返す struct（framebuffer_shown）の 8 つの値を読む。
 3. monitor で `xp /4wx <framebuffer_phys>`（HAL が描いた先頭の画素）と、qext の register
    （pci_memory_base＋display_registers＋0x604）の値を `xp /1wx` で読む。`info mtree` で 0x60/0x64 が i8042 の I/O か見る。
 4. continue して login の prompt まで進め、QMP の screendump で PNG（HAL の早期の console と kernel の login の画面）を撮る。
期待: (A) format 4（RGB332）、pitch 1024、color! の palette が効いて文字が白・灰で読める。(B) format 2（BGRX8888）か 3（XRGB8888）が
qext の値と合い、文字の色が正しい（赤と青が入れ替わらない）。(C) は (A) か (B) のどちらかと同じ形。phys は BAR0 の位置
（pci_memory_base＋display_framebuffer）。i8042 は 0x60・0x64。どれかで framebuffer_phys が 0 なら、Bochs の fallback で
640x400・format 2 が名前に出るはず（その時は handoff の depth・property が足りなかった。OpenBIOS の display の node の property を
monitor か gdb で読めれば添える）。
返して欲しい物: 3 条件の値の表、xp の値、PNG の path。判定は Q1。
```

## p002b の結果（T1-743、2026-10-19 19:33〜19:35、QEMU のみ・実機は未実施）

- tree: P1 の 8bf4911c3 と main 26f43269d を merge。sun4u build rc 0、boot/kernel/image check PASS。
- gdb（init の入口）で handoff 版 3: A `-g 1024x768x8`・C 既定は phys 0x1ff22000000・1024x768・pitch 1024・format 4（RGB332）、
  B x32 は pitch 4096・format 3（XRGB8888）。3 条件とも PCI 1234:1111・i8042 0x60/0x64。`sun4u_framebuffer_shown` も同じ値（Bochs の fallback に落ちない）。
- `info mtree` に i8042-data 0x60・i8042-cmd 0x64。3 条件とも login prompt（boot-test rc 0）: `build/review/t1-743-{A,B,C}.png`（Q1 が B を目視）。
- 未確認: qext の実値（xp は 0xbebebebe で MMIO を読めていない）、B の赤と青の順（画面に色が無い）、translate・`color!` の直接の観察。
- Q1 の判定: 「条件 1〜6 は満たす。色の順（B の赤と青）と qext の実値は未確認で、p004 以降の色のある画面で確かめる」。

## 残り・再開の条件

- p002b（T1）の結果で、`translate` の結果の並び・`address` の有無・`color!` の効き・qext の値・ASI の load が通るかを確かめ、
  合わなければ p002 を直す（uncleared にして再開）。
- merge の前に sparcv9 の file の差分をユーザーに見せる（D6、§6 の「はい」の行。今回変えたのは loader の 4 file と HAL の 4 file）。
