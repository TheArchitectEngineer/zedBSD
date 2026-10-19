# 案: boot-test.sh の mode `mips32-malta`・`mips64-malta`（Q1 が適用）

2026-10-19 P1（q1273）。P1 は plan/tools/ を変えていない。既存の mode の振る舞いは不変。

- 既定の operand: `mips32-malta` → `build/mips32-malta/vmunix`、`mips64-malta` → `build/mips64-malta/vmunix`（riscv32-virt の 106 行と同じ形）。
- qemu: `qemu-system-mipsel`・`qemu-system-mips64el`。
- 起動（m68k-virt の 304〜312 行と同じ形、`QEMU_EXTRA` を受ける）:
```
mips_machine=(-M malta -m "${BOOT_MEMORY:-256M}" -vga std -display none -no-reboot
	-serial "unix:$serial,server,nowait" -qmp "unix:$monitor,server,nowait")
[[ $boot_mode == mips64-malta ]] && mips_machine+=(-cpu MIPS64R2-generic)
mips_boot=(-kernel "$image")
[[ -n ${BOOT_APPEND:-} ]] && mips_boot+=(-append "$BOOT_APPEND")
```
- 237 行の「disk の image を作らない mode」の列に `mips32-malta`・`mips64-malta` を足す。
- 判定は今の QMP の screendump の PNG（serial は対話だけ）。
- 注: malta の `-serial` の 1 番目は Super I/O の COM1（port 0x3f8）と読んだ（未確認）。HAL の debug の出力は CBUS UART（`serial_hd(2)`）に出すなら `-serial` を 3 つ並べる必要がある。判定に使わないので、boot-test は 1 つでよい。
