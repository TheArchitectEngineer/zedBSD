# 案: sun4u の HAL の名前付きの値と boot.h の定数（WS275、未適用・承認待ち、改訂 3）

2026-10-19 19:06 ユーザーは改訂 2 の差分を「承認する」とした。改訂 3 でレビュー 2 の M-3・M-6・m-12 により定義文と名前の意味が
変わったので、改めて示す（変える前と後は末尾の「改訂 2 から改訂 3 の変化」）。

hal.h は変えない。承認を求める差分は (1) と (2) の 2 つ。

## (1) include/kern/boot.h

```diff
 /*
- * Pixel formats a platform's boot framebuffer format value may name: four
- * bytes per pixel, red first or blue first, the fourth byte unused.
+ * Pixel formats a platform's boot framebuffer format value may name.  Each
+ * name lists the bytes of one pixel in increasing address order.  The
+ * first three use four bytes per pixel, one byte unused: red, green, blue,
+ * unused; blue, green, red, unused; or unused, red, green, blue.  The last
+ * is the reverse of the DRM format of the same name, which BGRX8888 here
+ * matches.  RGB332 uses one byte per pixel, an index into a palette the
+ * loader set before it left the firmware: red in bits 7-5, green in bits
+ * 4-2 and blue in bits 1-0, index i showing red ((i >> 5) & 7) * 255 / 7,
+ * green ((i >> 2) & 7) * 255 / 7 and blue (i & 3) * 255 / 3, rounded down.
  */
 #define KERN_BOOT_FRAMEBUFFER_RGBX8888			1U
 #define KERN_BOOT_FRAMEBUFFER_BGRX8888			2U
+#define KERN_BOOT_FRAMEBUFFER_XRGB8888			3U
+#define KERN_BOOT_FRAMEBUFFER_RGB332			4U
```

- byte の並びは memory の address の昇順（定義文に書いた）。XRGB8888 は X が最下位の address。LE の xRGB（DRM の XRGB8888）は既存の
  `BGRX8888` を使う（新設しない）。
- 定数と同じ commit で、`src/hal/sparcv9/bsp-sun4u/framebuffer.c` の l.94・l.251 の「XRGB8888」の comment（DRM の意味）を「a
  little-endian word, blue first in memory」と直す（comment だけ、承認の対象外）。
- `RGB332` は loader が `color!` で palette を設定できた時だけ渡す。`color!` が失敗したら loader は framebuffer を渡さない。

## (2) src/hal/sparcv9/bsp-sun4u/boot.c の `hal_get_arch_handoff`

値は 64 bit の word への pointer。名前の意味は「**HAL が表示している framebuffer**」（M-3）。

- loader が framebuffer を渡した時: その値。
- 渡さず、HAL が Bochs の fallback（D7 (b)）で画面を出した時: phys＝`pci_memory_base`＋BAR0、width 640、height 400、pitch 2560、
  size 1024000、format `BGRX8888`（HAL が qext を LE に書くので）、pci は 0x1234・0x1111。
- HAL が画面を出せなかった時: framebuffer の名前は全部 NULL（kernel は kernel_putc を publish しない）。

| 名前 | 値 |
| --- | --- |
| `sun4u.framebuffer.phys` | firmware framebuffer の物理 address |
| `sun4u.framebuffer.size` | byte 数 = `linebytes × height` |
| `sun4u.framebuffer.width`・`.height` | pixel（node の `width`・`height`） |
| `sun4u.framebuffer.pitch` | 1 行の byte 数（node の `linebytes`） |
| `sun4u.framebuffer.format` | (1) の `KERN_BOOT_FRAMEBUFFER_*`（BGRX8888・XRGB8888・RGB332 のどれか） |
| `sun4u.framebuffer.pci.vendor`・`.device` | display の node の PCI の id（PCI でない display では NULL） |
| `sun4u.i8042.data`・`sun4u.i8042.command` | i8042 の I/O port（QEMU は 0x60・0x64）。loader の handoff 版 3 から（D4 (a)）。無ければ NULL |

計 10 の名前（framebuffer 8、i8042 2）。

```c
	/* Exposes the firmware framebuffer, when the loader found one. */
	match = handoff_name_is(name, "sun4u.framebuffer.phys");
	if (match)
		return framebuffer_shown ? &framebuffer_phys : NULL;
```

値は `sun4u_boot_init` で handoff から一度だけ埋め、fallback の値は `sun4u_framebuffer_init` が画面を出した時に埋める
（`framebuffer_shown` を立てる）。どちらも既存の `serial_port` と同じ形。

D4 は p002 の前にユーザーに聞く（m-12）。ここは D4 (a) の差分だけ。(b)（HAL が ISA の固定値を返す）を選ばれたら、この表と code を
書き直して改めて示す。

## 改訂 2 から改訂 3 の変化

- boot.h の定義文（定数の名前と値 3U・4U は不変）:
  - 前: "Pixel formats a platform's boot framebuffer format value may name.  The first three use four bytes per pixel, one byte
    unused: red first, blue first, or the unused byte first followed by red, green and blue.  RGB332 uses one byte per pixel, an
    index into a palette the loader set to the 3-3-2 red, green and blue ramp before it left the firmware."
  - 後: 上の (1) の diff の `+` の行。address の順、DRM の同名と逆であること、RGB332 の bit の並びと ramp を足した。
- 名前付きの値: 10 の名前・型は不変。意味が「loader が渡した framebuffer」から「HAL が表示している framebuffer」に変わり、Bochs の
  fallback の時も値を返す（改訂 2 では NULL）。i8042 の行は D4 (a) だけに絞った。
