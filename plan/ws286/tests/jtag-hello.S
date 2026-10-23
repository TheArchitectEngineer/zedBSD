/*
 * A stand-in kernel for checking jtag-boot.sh before zedBSD runs on the ZYBO:
 * entered at 0x00100000 with r2 = the DTB, it prints r2, whether the FDT magic
 * is there, and a checksum of the image it was loaded as, on UART1 (which SPL
 * has set up at 115200), and then waits.
 * IMAGE_SIZE is how many bytes from _start the sum covers.  Raw, padded to
 * 8 MiB (the host sums the padded file as little-endian words):
 *   arm-linux-gnueabihf-gcc -nostdlib -static -no-pie -Wl,--build-id=none \
 *       -DIMAGE_SIZE=0x800000 -Ttext=0x00100000 -o jtag-hello.elf jtag-hello.S
 *   arm-linux-gnueabihf-objcopy -O binary -j .text jtag-hello.elf jtag-hello.bin
 *   (then append random bytes up to 8 MiB)
 * As an ELF for jtag-boot.sh's ELF path: the same with -Wl,-N
 * -Wl,--no-warn-rwx-segments and -DIMAGE_SIZE=0x100.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 */
	.arm
	.text
	.global	_start
_start:
	mov	r4, r2
	adr	r0, banner
	bl	puts
	mov	r0, r4
	bl	puthex
	ldr	r5, [r4]
	ldr	r6, =0xedfe0dd0		/* 0xd00dfeed, big-endian */
	cmp	r5, r6
	adreq	r0, fdt_ok
	adrne	r0, fdt_bad
	bl	puts
	/* The sum of the words from _start to the end of the padded image. */
	ldr	r5, =_start
	ldr	r6, =_start + IMAGE_SIZE
	mov	r7, #0
1:	ldr	r8, [r5], #4
	add	r7, r7, r8
	cmp	r5, r6
	bne	1b
	adr	r0, sum
	bl	puts
	mov	r0, r7
	bl	puthex
	adr	r0, newline
	bl	puts
2:	wfi
	b	2b

/* Writes the string at r0 to UART1; uses r0-r3. */
puts:
	ldr	r1, =0xE0001000
3:	ldrb	r2, [r0], #1
	cmp	r2, #0
	bxeq	lr
4:	ldr	r3, [r1, #0x2c]		/* channel status: TXFULL is bit 4 */
	tst	r3, #0x10
	bne	4b
	str	r2, [r1, #0x30]		/* TX FIFO */
	b	3b

/* Writes r0 as eight hex digits; uses r0-r3, r9-r11. */
puthex:
	mov	r11, lr
	mov	r9, r0
	mov	r10, #28
5:	lsr	r0, r9, r10
	and	r0, r0, #0xf
	cmp	r0, #10
	addlo	r0, r0, #'0'
	addhs	r0, r0, #'a' - 10
	ldr	r1, =0xE0001000
6:	ldr	r3, [r1, #0x2c]
	tst	r3, #0x10
	bne	6b
	str	r0, [r1, #0x30]
	subs	r10, r10, #4
	bpl	5b
	bx	r11

banner:	.asciz	"\r\nJTAG HELLO r2="
fdt_ok:	.asciz	" fdt=ok"
fdt_bad: .asciz	" fdt=bad"
sum:	.asciz	" sum="
newline: .asciz	"\r\n"
	.balign	4
	.ltorg
