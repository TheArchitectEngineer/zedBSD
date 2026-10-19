# 案: sun4u の HAL の名前付きの値と boot.h の定数（WS275、未適用・承認待ち、改訂 2）

hal.h は変えない。承認を求める差分は (1) と (2) の 2 つ。

## (1) include/kern/boot.h

```diff
 /*
- * Pixel formats a platform's boot framebuffer format value may name: four
- * bytes per pixel, red first or blue first, the fourth byte unused.
+ * Pixel formats a platform's boot framebuffer format value may name.  The
+ * first three use four bytes per pixel, one byte unused: red first, blue
+ * first, or the unused byte first followed by red, green and blue.  RGB332
+ * uses one byte per pixel, an index into a palette the loader set to the
+ * 3-3-2 red, green and blue ramp before it left the firmware.
  */
 #define KERN_BOOT_FRAMEBUFFER_RGBX8888			1U
 #define KERN_BOOT_FRAMEBUFFER_BGRX8888			2U
+#define KERN_BOOT_FRAMEBUFFER_XRGB8888			3U
+#define KERN_BOOT_FRAMEBUFFER_RGB332			4U
```

- byte の並びは memory の address の順（XRGB8888 は X が最下位の address）。LE の xRGB は既存の `BGRX8888` を使う（新設しない）。
- `RGB332` は loader が `color!` で palette を設定できた時だけ渡す。`color!` が失敗したら loader は framebuffer を渡さない。

## (2) src/hal/sparcv9/bsp-sun4u/boot.c の `hal_get_arch_handoff`

値は 64 bit の word への pointer。loader が framebuffer を渡さない時（handoff 版 3 の `framebuffer_present` が 0）、framebuffer の名前は全部 NULL。

| 名前 | 値 |
| --- | --- |
| `sun4u.framebuffer.phys` | firmware framebuffer の物理 address |
| `sun4u.framebuffer.size` | byte 数 = `linebytes × height` |
| `sun4u.framebuffer.width`・`.height` | pixel（node の `width`・`height`） |
| `sun4u.framebuffer.pitch` | 1 行の byte 数（node の `linebytes`） |
| `sun4u.framebuffer.format` | (1) の `KERN_BOOT_FRAMEBUFFER_*`（BGRX8888・XRGB8888・RGB332 のどれか） |
| `sun4u.framebuffer.pci.vendor`・`.device` | display の node の PCI の id（PCI でない display では NULL） |
| `sun4u.i8042.data`・`sun4u.i8042.command` | i8042 の I/O port（QEMU は 0x60・0x64）。D4(a) は loader の handoff から、D4(b) は固定値。無ければ NULL |

計 10 の名前（framebuffer 8、i8042 2）。

```c
	/* Exposes the firmware framebuffer, when the loader found one. */
	match = handoff_name_is(name, "sun4u.framebuffer.phys");
	if (match)
		return framebuffer_present ? &framebuffer_phys : NULL;
```

値は `sun4u_boot_init` で handoff から一度だけ埋める（既存の `serial_port` と同じ形）。
