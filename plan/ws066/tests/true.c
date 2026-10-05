/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws066-p001: a program that does nothing, linked several ways by
 * build-measure.sh (static, dynamic with a SysV hash as the clang driver
 * links, dynamic with a GNU hash) so that startbench can tell the
 * loader's part of a start from the rest.
 */

int main(void);

/*
 * Exits at once.
 */
int
main(
	void)
{
	/* Nothing to do. */
	return 0;
}
