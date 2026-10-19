# 案: sun4u の HAL の名前付きの値（WS275、未適用・承認待ち）

- hal.h は変えない。`src/hal/sparcv9/bsp-sun4u/boot.c` の `hal_get_arch_handoff` に名前を足す（HAL の責務の追加）。
- 値は 64 bit の word への pointer。display が無い（`display_vendor == 0`）時は framebuffer の値は NULL。

| 名前 | 値 |
| --- | --- |
| `sun4u.framebuffer.phys`・`.size` | OBP の firmware framebuffer の物理 address と byte 数 |
| `sun4u.framebuffer.width`・`.height`・`.pitch` | screen の node の `width`・`height`・`linebytes` |
| `sun4u.framebuffer.format` | `KERN_BOOT_FRAMEBUFFER_*`（include/kern/boot.h に INDEX8 と BE の xRGB を足す: HAL→kernel の汎用の値の追加） |
| `sun4u.framebuffer.pci.vendor`・`.device` | screen の node の PCI の id |
| `sun4u.i8042.data` | i8042 の data の I/O port（QEMU は 0x60） |
| `sun4u.i8042.command` | i8042 の status/command の I/O port（QEMU は 0x64） |

- framebuffer の値は loader が OBP の screen の node から読み handoff 版 3 に入れる（ユーザーの決定 2026-10-19、firmware framebuffer）。
  無い時は全部 NULL。
- i8042 の 2 つは D4 による: (a) loader が OpenBIOS の device tree から取り `struct sun4u_boot_handoff` に 2 field（版 3）、
  (b) HAL が固定の ISA の値を返す。
- 差分の形（boot.c、framebuffer の例）:

```c
	/* Exposes the firmware framebuffer, when the loader found one. */
	match = handoff_name_is(name, "sun4u.framebuffer.phys");
	if (match)
		return framebuffer_present ? &framebuffer_phys : NULL;
```

  値は `sun4u_boot_init` で handoff から一度だけ埋める（既存の `serial_port` と同じ形）。
