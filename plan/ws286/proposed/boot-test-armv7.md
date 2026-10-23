# 案: boot-test.sh の mode `armv7-virt`（Q1 が適用）— 版 3

**版 3**（2026-10-23 P2、review [C2・M11 の 5](../phase001/review-20261023.md)）: QEMU の arm は ELF の kernel に `r2`（DTB）を渡さず `-initrd` も読まない（`hw/arm/boot.c` 956・1042 行）ので、**既定の image を raw の `build/armv7-virt/vmunix.bin`** にした。raw の image は Linux の起動の約束になり、`0x40010000` に置かれ、`r0 = 0`・`r1 = -1`・`r2` = DTB で入り、`-initrd` は `/chosen` の `linux,initrd-start`/`-end` に、`-append` は `bootargs` に書かれる（design §1.3・§4.6）。zybo の JTAG の流れ（`jtag-boot.sh`）と同じ `r2` の約束。`-machine` に `gic-version=2` を明示した（HAL は GICv2 だけ）。版 2 の案（ELF の `vmunix`）は適用しない。変えたのは注の段落・hunk 2・hunk 5。行の番号は 2026-10-23 の版 3 の基点（main 02a1b7837 を merge した所）でも同じ（boot-test.sh は 445 行のまま、151b879de から変わっていない: P2 が確かめた）。

2026-10-23 P1（[ws286-p001](../phase001/phase.md) 版 2、設計 [design.md](../design.md) §4.6・§7.2）。P1 は plan/tools/ を変えていない。既存の mode の振る舞いは不変。
行の番号は 2026-10-23 の main（151b879de）の boot-test.sh（445 行）。形は WS271 の `mips32-malta` の案と同じ 5 か所。
判定は今の QMP の screendump の PNG（HAL が ramfb に描く console）。serial は打つだけ（`BOOT_THEN` の `type:`）。

ZYBO（`xilinx-zynq-a9` と実機）は **boot-test.sh に入れない**: 表示の device が無く PNG で判定できないため。別の道具 `plan/tools/zybo/qemu-zynq.sh`・`jtag-boot.sh` と `serial.py`（生きた line の対話）で判定する案で、ユーザーの判断（design §10 の U3）を待つ。版 1 が約束した `BOOT_MODE=zybo` は作らない。

## 1. usage の comment（20〜23 行）
```
#   BOOT_MODE      uefi-nvme (default), uefi-usb, bios-ide, raspi4b,
#                  riscv64-virt, riscv64-uboot, riscv64-uboot-gop, riscv32-virt, m68k-virt,
#                  ppc32-mac99, ppc64-pseries, mips32-malta, mips64-malta,
#                  armv7-virt or sun4u
```
95 行（sun4u の説明）の前に 1 段落を足す:
```
# BOOT_MODE=armv7-virt boots a raw 32-bit ARMv7-A kernel image (default
# build/armv7-virt/vmunix.bin) on qemu-system-arm -M virt with a Cortex-A15
# and a ramfb display the kernel draws its console on.  QEMU enters a raw
# image the way it boots Linux, with the device tree in r2, so INITRD and
# BOOT_APPEND reach the kernel through /chosen; an ELF image would get
# neither.
```

## 2. 1 番目の case（既定の operand と interval、125〜128 行の riscv32-virt の後）
```
armv7-virt)
	image=${1:-${IMAGE:-${KERNEL:-$root/build/armv7-virt/vmunix.bin}}}
	boot_interval=${BOOT_INTERVAL:-0.5}
	;;
```

## 3. 2 番目の case（qemu と required、194〜200 行の riscv32-virt の後）
これが無いと 231 行の「unknown BOOT_MODE」で止まる。
```
armv7-virt)
	qemu=${QEMU:-qemu-system-arm}
	required=("$image")
	if [[ -n ${INITRD:-} ]]; then
		required+=("$INITRD")
	fi
	;;
```

## 4. disk の image を作らない mode の列（269〜271 行）
```
if [[ $boot_mode != riscv64-virt && $boot_mode != riscv32-virt && $boot_mode != m68k-virt &&
	$boot_mode != ppc32-mac99 && $boot_mode != ppc64-pseries &&
	$boot_mode != mips32-malta && $boot_mode != mips64-malta &&
	$boot_mode != armv7-virt ]]; then
```

## 5. 起動（mips の elif の後、同じ形）
```
elif [[ $boot_mode == armv7-virt ]]; then
	# The virt machine with one Cortex-A15, a GICv2, a ramfb display the
	# kernel draws its console on, and the PL011 on a socket for typing
	# into.  The raw image is booted like Linux: r2 holds the device tree.
	armv7_machine=(-machine virt,gic-version=2 -cpu cortex-a15 -smp "${BOOT_SMP:-1}"
		-m "${BOOT_MEMORY:-512M}" -device ramfb -display none -no-reboot
		-serial "unix:$serial,server,nowait"
		-qmp "unix:$monitor,server,nowait")
	armv7_boot=(-kernel "$image")
	if [[ -n ${INITRD:-} ]]; then
		armv7_boot+=(-initrd "$INITRD")
	fi
	if [[ -n ${BOOT_APPEND:-} ]]; then
		armv7_boot+=(-append "$BOOT_APPEND")
	fi
	read -r -a armv7_extra <<<"${QEMU_EXTRA:-}"
	"$qemu" "${armv7_machine[@]}" "${armv7_boot[@]}" "${armv7_extra[@]}" \
		>"$output/qemu.log" 2>&1 &
```

## 注
- `-smp` の既定は 1（WS286 は 1 CPU、design §3.7）。SMP の WS で `BOOT_SMP=2`。
- arm の virt で ramfb が付く事（fw_cfg の MMIO）は QEMU 10.0 の `hw/arm/virt.c` の `machine_class_allow_dynamic_sysbus_dev(mc, TYPE_RAMFB_DEVICE)` に拠る（v10.0.11 の 3171 行、SHA-256 の先頭 25ba8b095c0999cc。2026-10-23 P1 が読んで確かめた。実際に表示が出る事は最初の T1 の起動で確かめる）。
- 版 3: `-kernel` は raw の image（`llvm-objcopy -O binary` の `vmunix.bin`）。ELF を渡すと QEMU は `r0`〜`r2` を設定せず `-initrd` を黙って無視する（`hw/arm/boot.c` 692〜721・1042 行）ので、`KERNEL=…/vmunix`（ELF）を渡さない事を mode の注に書く。gdb の symbol は ELF の `vmunix`（`QEMU_EXTRA='-S -gdb tcp::PORT'`）。
- 版 3: 最初の T1 の起動（p004）で、`-S` で止めて gdb で `hbreak *0x40010000` → `continue` → `p/x $r2`・`x/wx $r2`（`0xedfe0dd0`）を確かめる（design §4.6）。
- `-smp` を 2 以上にする時（SMP の WS）は PSCI の conduit が HVC である事（既定の virt）を HAL が DTB から読む（design §3.7）。
