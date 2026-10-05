/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * xdg_activation_v1 (activation.c, ws089-p016): the tokens that let a
 * program bring another program's window to the front, shared with the
 * dispatch and with the launcher, which gives each program it starts a
 * token of its own.
 */

#ifndef ZWL_ACTIVATION_H
#define ZWL_ACTIVATION_H

#include "zwl.h"

/* The room a token's text takes, its NUL counted. */
#define ZWL_ACTIVATION_TOKEN_SIZE	33U

int zwl_activation_request(struct zwl_object *object, uint32_t opcode, const unsigned char *bytes, size_t size);
int zwl_activation_issue(struct zwl_server *server, const char *app_id, const char *via, char *token, size_t size);

#endif
