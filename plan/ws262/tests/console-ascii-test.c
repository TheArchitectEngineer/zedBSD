/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A host test of the UEFI loader's console_ascii(): a line feed at the
 * 125th character must not carry the UTF-16 buffer past its end.
 *
 * Build with -fsanitize=address and -DBOOTX64_SOURCE='"path/to/bootx64.c"'
 * (console-ascii-run.sh).  The loader's debug-port writes fault on the host;
 * a SIGSEGV handler steps over each OUT instruction.
 */

#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>

#include BOOTX64_SOURCE

static unsigned long written;

static EFI_STATUS EFIAPI record_string(struct efi_simple_text_output_protocol *, const CHAR16 *);
static void skip_out(int signal_number, siginfo_t *info, void *context);

/* Writes a long line ending in a line feed at the 125th character through console_ascii(). */
int
main(
	void)
{
	static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL console;
	static EFI_SYSTEM_TABLE system_table;
	struct loader_context context;
	char text[200];
	struct sigaction action;

	/* Steps over the debug port's OUT instructions. */
	memset(&action, 0, sizeof(action));
	action.sa_sigaction = skip_out;
	action.sa_flags = SA_SIGINFO;
	sigaction(SIGSEGV, &action, 0);

	/* A firmware console that only counts the characters. */
	memset(&console, 0, sizeof(console));
	console.OutputString = record_string;
	memset(&system_table, 0, sizeof(system_table));
	system_table.ConOut = &console;
	memset(&context, 0, sizeof(context));
	context.system = &system_table;

	/* 125 characters, a line feed, and a few after it. */
	memset(text, 'a', 125);
	text[125] = '\n';
	strcpy(text + 126, "bcdefgh");
	console_ascii(&context, text);

	/* 133 characters and one carriage return reach the console. */
	printf("written=%lu\n", written);
	if (written != 134UL)
		return 1;

	/* Succeeded: no overflow was reported and every character arrived. */
	return 0;
}

/* Counts the characters the loader writes. */
static EFI_STATUS EFIAPI
record_string(
	struct efi_simple_text_output_protocol *console,
	const CHAR16 *string)
{
	(void)console;

	/* Counts up to the terminator. */
	while (*string != 0) {
		written++;
		string++;
	}

	/* Succeeded. */
	return EFI_SUCCESS;
}

/* Steps over an OUT instruction that faulted. */
static void
skip_out(
	int signal_number,
	siginfo_t *info,
	void *context)
{
	ucontext_t *user;
	const unsigned char *pc;

	(void)signal_number;
	(void)info;

	/* OUT imm8 is two bytes, OUT dx is one; anything else is a real fault. */
	user = context;
	pc = (const unsigned char *)user->uc_mcontext.gregs[REG_RIP];
	if (pc[0] == 0xe6U) {
		user->uc_mcontext.gregs[REG_RIP] += 2;
	} else if (pc[0] == 0xeeU) {
		user->uc_mcontext.gregs[REG_RIP] += 1;
	} else {
		signal(SIGSEGV, SIG_DFL);
	}
}
