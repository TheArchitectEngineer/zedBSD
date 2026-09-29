/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * BUG-051 probe: user registers survive interrupts and preemption.
 *
 * regcheck_spin(iterations, seed) fills every general register but %rsp,
 * xmm0-xmm15 and eight stack slots with values derived from seed, then
 * spins, checking all of them on every pass.  It returns 0 when every
 * pass saw every value, or the 1-based number of the first register or
 * slot found changed (1-14 general, 15-30 xmm, 31-38 stack).
 *
 *   uint64_t regcheck_spin(uint64_t iterations, uint64_t seed);
 *
 * regcheck_red_zone(iterations, seed) is a leaf function that keeps sixteen
 * values in the 128-byte red zone below %rsp (the System V amd64 ABI lets a
 * leaf function do so without moving %rsp) and checks them on every pass.
 * It returns 0, or the 1-based number of the first word found changed.
 * A signal frame pushed over the red zone changes them (BUG-051).
 *
 *   uint64_t regcheck_red_zone(uint64_t iterations, uint64_t seed);
 */

	.text
	.globl	regcheck_spin
	.type	regcheck_spin, @function
regcheck_spin:
	pushq	%rbp
	pushq	%rbx
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15
	/* The expected values: slot k of the table holds seed + k * 0x0101010101010101. */
	subq	$(8 * 40), %rsp
	movq	%rsi, %rax
	movabsq	$0x0101010101010101, %rcx
	xorl	%edx, %edx
1:	movq	%rax, (%rsp,%rdx,8)
	addq	%rcx, %rax
	incq	%rdx
	cmpq	$40, %rdx
	jne	1b
	/* The iteration count lives in slot 39 (never checked). */
	movq	%rdi, 39*8(%rsp)
	/* Stack slots 31..38 are the table itself at 31*8..38*8. */
	movq	0*8(%rsp), %rax
	movq	1*8(%rsp), %rbx
	movq	2*8(%rsp), %rcx
	movq	3*8(%rsp), %rdx
	movq	4*8(%rsp), %rsi
	movq	5*8(%rsp), %rdi
	movq	6*8(%rsp), %rbp
	movq	7*8(%rsp), %r8
	movq	8*8(%rsp), %r9
	movq	9*8(%rsp), %r10
	movq	10*8(%rsp), %r11
	movq	11*8(%rsp), %r12
	movq	12*8(%rsp), %r13
	movq	13*8(%rsp), %r14
	movq	14*8(%rsp), %xmm0
	movq	15*8(%rsp), %xmm1
	movq	16*8(%rsp), %xmm2
	movq	17*8(%rsp), %xmm3
	movq	18*8(%rsp), %xmm4
	movq	19*8(%rsp), %xmm5
	movq	20*8(%rsp), %xmm6
	movq	21*8(%rsp), %xmm7
	movq	22*8(%rsp), %xmm8
	movq	23*8(%rsp), %xmm9
	movq	24*8(%rsp), %xmm10
	movq	25*8(%rsp), %xmm11
	movq	26*8(%rsp), %xmm12
	movq	27*8(%rsp), %xmm13
	movq	28*8(%rsp), %xmm14
	movq	29*8(%rsp), %xmm15
#ifdef REGCHECK_SELFTEST
	/* The self test breaks %rsi (slot 5) to show the checks see a change. */
	xorl	%esi, %esi
#endif
.Lpass:
	movl	$1, %r15d
	cmpq	0*8(%rsp), %rax
	jne	.Lbad
	incl	%r15d
	cmpq	1*8(%rsp), %rbx
	jne	.Lbad
	incl	%r15d
	cmpq	2*8(%rsp), %rcx
	jne	.Lbad
	incl	%r15d
	cmpq	3*8(%rsp), %rdx
	jne	.Lbad
	incl	%r15d
	cmpq	4*8(%rsp), %rsi
	jne	.Lbad
	incl	%r15d
	cmpq	5*8(%rsp), %rdi
	jne	.Lbad
	incl	%r15d
	cmpq	6*8(%rsp), %rbp
	jne	.Lbad
	incl	%r15d
	cmpq	7*8(%rsp), %r8
	jne	.Lbad
	incl	%r15d
	cmpq	8*8(%rsp), %r9
	jne	.Lbad
	incl	%r15d
	cmpq	9*8(%rsp), %r10
	jne	.Lbad
	incl	%r15d
	cmpq	10*8(%rsp), %r11
	jne	.Lbad
	incl	%r15d
	cmpq	11*8(%rsp), %r12
	jne	.Lbad
	incl	%r15d
	cmpq	12*8(%rsp), %r13
	jne	.Lbad
	incl	%r15d
	cmpq	13*8(%rsp), %r14
	jne	.Lbad
	/* xmm: compare the low quadword through %r15 is taken, so use the stack slot 30 as scratch. */
	.irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	movq	%xmm\n, 30*8(%rsp)
	incl	%r15d
	pushq	%rax
	movq	(30+1)*8(%rsp), %rax
	cmpq	(14+\n+1)*8(%rsp), %rax
	popq	%rax
	jne	.Lbad
	.endr
	/* Stack slots: slot 31..38 must still hold seed + k * step; recompute from slot 0. */
	movl	$31, %r15d
.Lslot:
	pushq	%rax
	pushq	%rcx
	movq	(0+2)*8(%rsp), %rax
	movabsq	$0x0101010101010101, %rcx
	imulq	%r15, %rcx
	addq	%rcx, %rax
	cmpq	(2)*8(%rsp,%r15,8), %rax
	popq	%rcx
	popq	%rax
	jne	.Lbad
	incl	%r15d
	cmpl	$39, %r15d
	jne	.Lslot
	/* Next pass. */
	decq	39*8(%rsp)
	jnz	.Lpass
	xorl	%eax, %eax
	jmp	.Lout
.Lbad:
	movl	%r15d, %eax
.Lout:
	addq	$(8 * 40), %rsp
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rbx
	popq	%rbp
	ret
	.size	regcheck_spin, .-regcheck_spin

	.globl	regcheck_red_zone
	.type	regcheck_red_zone, @function
regcheck_red_zone:
	/* Word k at -128 + 8 * k below %rsp holds seed + k. */
	xorl	%ecx, %ecx
1:	leaq	(%rsi,%rcx), %rax
	movq	%rax, -128(%rsp,%rcx,8)
	incq	%rcx
	cmpq	$16, %rcx
	jne	1b
.Lred_pass:
	xorl	%ecx, %ecx
2:	leaq	(%rsi,%rcx), %rax
	cmpq	%rax, -128(%rsp,%rcx,8)
	jne	.Lred_bad
	incq	%rcx
	cmpq	$16, %rcx
	jne	2b
	decq	%rdi
	jnz	.Lred_pass
	xorl	%eax, %eax
	ret
.Lred_bad:
	leaq	1(%rcx), %rax
	ret
	.size	regcheck_red_zone, .-regcheck_red_zone

	.section .note.GNU-stack,"",@progbits
