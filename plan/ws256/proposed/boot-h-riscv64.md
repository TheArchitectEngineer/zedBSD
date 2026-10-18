<!-- awesome-plan project=zedbsd record=ws256-p001-proposed-boot-h -->

# WS256 の `include/kern/boot.h` の差分の案（案 (b)、ユーザーの決定の前は適用しない）

2026-10-18 P2（q1243-i02、ws256-p001 改訂 2）。[design.md](../design.md) §3.3・§7 #9。

`include/kern/boot.h` は「public boot-interface ledger。宣言は記録された architecture/API の決定の後にだけ足す・変える」（file の先頭の comment）。レビュー 1 の M7 のとおり、変えるなら差分を示してユーザーの明示の決定を得る。

## 既定は (a): boot.h を変えない

design.md §7 #9 の既定は、FDT・initrd・boot hart を HAL の `hal_get_arch_handoff("riscv64.fdt")`・`("riscv64.initrd")`・`("riscv64.boot-hart")` で渡す案 (a)（[hal-riscv64.md](hal-riscv64.md) の差分 3）。(a) なら **この file の差分は適用しない**。理由: boot.h の rpi4・sun4u・pc98・x68k の形には「XXX: To be removed. Use the common one.」の注があり、arch ごとの struct を増やす方向と逆。kernel の platform は既に `kern_boot_handoff(name)` の口を持つ。

## 案 (b): rpi4 と同じ形の struct を足す

ユーザーが「kernel の platform は HAL の名前でなく handoff の struct から読む方がよい」と判断した時の差分。rpi4 の `struct rpi4_boot_handoff` の置き方に合わせる。

```diff
--- a/include/kern/boot.h
+++ b/include/kern/boot.h
@@
 /* XXX: To be removed. Use the common one. */
 #define KERN_RPI4_HANDOFF_MAGIC	0x34495052U
 
+/* XXX: To be removed. Use the common one. */
+#define KERN_RISCV64_HANDOFF_MAGIC	0x34365652U /* "RV64" */
+#define KERN_RISCV64_HANDOFF_VERSION	1U
+
@@
 /* XXX: To be removed. Use the common one. */
 struct rpi4_boot_handoff {
 	...
 } __attribute__((packed));
 
+/* XXX: To be removed. Use the common one. */
+struct riscv64_boot_handoff {
+	struct kern_boot_handoff common;
+	uint32_t extension_magic;
+	uint16_t extension_version;
+	uint16_t extension_size;
+	uint64_t boot_hart;
+	uint64_t fdt_phys;
+	uint64_t fdt_size;
+	uint64_t initrd_phys;
+	uint64_t initrd_size;
+	uint32_t reserved[4];
+} __attribute__((packed));
```

- `common` は `KERN_HANDOFF_VERSION_MULTIBOOT`・`KERN_PARTITION_SCHEME_MBR`・index 1・bios 0x80（design.md §2.7）。
- memory の範囲（ZBL6 v6）・framebuffer は入れない: memory は HAL が持ち（kernel は `hal_pmem_*` で見る）、framebuffer は HAL の console が描く。
- kernel 側の変更は無い（`entry.c` の version の分岐は MULTIBOOT のまま。`vfs.c` は `common` だけを見る）。読むのは `src/kern/platform/riscv-virt.c`・`visionfive2.c` だけ。

## 任意の項目: `KERN_DEV_RAM`

`enum kern_boot_device_class` に ramdisk の class が無い（FDD・IDE・SCSI・SD）。design.md §2.7 の既定は `KERN_DEV_SD` を使う（kernel の `src/kern/*.c` は `device_class` を見ない: 2026-10-18 に grep）。名前を正しくしたい時だけ:

```diff
 enum kern_boot_device_class {
 	KERN_DEV_FDD = 1,
 	KERN_DEV_IDE = 2,
 	KERN_DEV_SCSI = 3,
 	KERN_DEV_SD = 4,
+	KERN_DEV_RAM = 5,
 };
```

## 決定の記録

- 2026-10-18: 案を置いた。決定は未。決まったらここに原文と日付を書き、(b) なら適用した差分の SHA-256 を足す。
