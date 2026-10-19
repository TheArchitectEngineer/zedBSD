# 案: boot-test.sh の mode `mips32-malta`・`mips64-malta`（Q1 が適用）

2026-10-19 P1（q1273、改訂 2 は q1277 で design-review-1 の Minor 12 を当てた）。P1 は plan/tools/ を変えていない。既存の mode の振る舞いは不変。
行の番号は 2026-10-19 の main の boot-test.sh。変える所は 5 か所。

## 1. usage の comment（20〜22 行）
```
#   BOOT_MODE      uefi-nvme (default), uefi-usb, bios-ide, raspi4b,
#                  riscv64-virt, riscv64-uboot, riscv32-virt, m68k-virt,
#                  ppc32-mac99, ppc64-pseries, mips32-malta, mips64-malta
#                  or sun4u
```
`BOOT_INTERVAL` の行（33〜34 行付近）の「0.5 in the riscv64 modes」を「0.5 in the modes that boot a kernel directly」に。
67〜68 行の riscv32-virt の説明の後に 1 段落を足す:
```
# BOOT_MODE=mips32-malta boots a little-endian MIPS32 kernel (default
# build/mips32-malta/vmunix) on qemu-system-mipsel -M malta with the std VGA,
# and mips64-malta a MIPS64 one (build/mips64-malta/vmunix) on
# qemu-system-mips64el with the MIPS64R2-generic CPU.
```

## 2. 1 番目の case（既定の operand と interval、106〜109 行の riscv32-virt の後）
```
mips32-malta)
	image=${1:-${IMAGE:-${KERNEL:-$root/build/mips32-malta/vmunix}}}
	boot_interval=${BOOT_INTERVAL:-0.5}
	;;
mips64-malta)
	image=${1:-${IMAGE:-${KERNEL:-$root/build/mips64-malta/vmunix}}}
	boot_interval=${BOOT_INTERVAL:-0.5}
	;;
```

## 3. 2 番目の case（qemu と required、170〜176 行の riscv32-virt の後）
これが無いと 199 行の「unknown BOOT_MODE」で止まる。
```
mips32-malta)
	qemu=${QEMU:-qemu-system-mipsel}
	required=("$image")
	;;
mips64-malta)
	qemu=${QEMU:-qemu-system-mips64el}
	required=("$image")
	;;
```

## 4. disk の image を作らない mode の列（237〜238 行）
```
if [[ $boot_mode != riscv64-virt && $boot_mode != riscv32-virt && $boot_mode != m68k-virt &&
	$boot_mode != ppc32-mac99 && $boot_mode != ppc64-pseries &&
	$boot_mode != mips32-malta && $boot_mode != mips64-malta ]]; then
```

## 5. 起動（m68k-virt の elif の後、同じ形）
```
elif [[ $boot_mode == mips32-malta || $boot_mode == mips64-malta ]]; then
	# The malta machine with the std VGA the kernel sets up and draws its
	# console on, and the Super I/O COM1 on a socket for typing into.
	mips_machine=(-machine malta -m "${BOOT_MEMORY:-256M}" -vga std
		-display none -no-reboot
		-serial "unix:$serial,server,nowait"
		-qmp "unix:$monitor,server,nowait")
	if [[ $boot_mode == mips64-malta ]]; then
		mips_machine+=(-cpu MIPS64R2-generic)
	fi
	mips_boot=(-kernel "$image")
	if [[ -n ${BOOT_APPEND:-} ]]; then
		mips_boot+=(-append "$BOOT_APPEND")
	fi
	read -r -a mips_extra <<<"${QEMU_EXTRA:-}"
	"$qemu" "${mips_machine[@]}" "${mips_boot[@]}" "${mips_extra[@]}" \
		>"$output/qemu.log" 2>&1 &
```

## 注
- 判定は今の QMP の screendump の PNG（serial は対話だけ）。
- malta の `-serial` の 1 番目は Super I/O の COM1（port 0x3f8）と読んだ（未確認）。HAL の debug の出力を CBUS UART（`serial_hd(2)`）に出すなら `-serial` を 3 つ並べる必要がある。判定に使わないので 1 つでよい。
- screendump が VGA を取り、FPGA の text console（`vc:320x200`）を取らないことは未確認（design.md §1.5）。最初の T1 の画面で確かめ、違えば Q1 に返す。
