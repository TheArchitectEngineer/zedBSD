/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The compositor without the screen capture (ws173-p002): every image but
 * a test image built with ZEDBSD_TEST_SCREEN_CAPTURE=y links this file in
 * place of shot.c, so the compositor opens no capture socket and copies
 * no frame.
 */

#include "compose.h"

/* Tells whether this compositor was built with the capture (it was not). */
int
zwl_shot_enabled(
	void)
{
	return 0;
}

/* Has no capture to wait for. */
int
zwl_shot_waiting(
	void)
{
	return 0;
}

/* No socket. */
void
zwl_shot_open(
	struct zwl_server *server)
{
	(void)server;
}

/* Nothing to close. */
void
zwl_shot_close(
	struct zwl_server *server)
{
	(void)server;
}

/* Nothing to serve. */
void
zwl_shot_tick(
	struct zwl_server *server)
{
	(void)server;
}

/* No copy. */
void
zwl_shot_record(
	struct zwl_server *server,
	VkCommandBuffer command,
	VkImage image)
{
	(void)server;
	(void)command;
	(void)image;
}

/* Nothing to send. */
void
zwl_shot_complete(
	struct zwl_server *server)
{
	(void)server;
}
