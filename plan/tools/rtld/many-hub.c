/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws140: a library that is there for its dependencies.  build-many.sh
 * links it with forty leaf libraries, which become its DT_NEEDED entries,
 * and rtld-many.c opens it to load them all and looks their functions up
 * through its handle.
 */

int hub_marker(void);

/*
 * Reports that the hub is loaded.
 */
int
hub_marker(
	void)
{
	/* Any value: the test only needs the symbol. */
	return 1;
}
