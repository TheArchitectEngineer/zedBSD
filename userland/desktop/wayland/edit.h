/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The editing operations of the on-screen keyboard's tool face (keiland_edit_v1,
 * ws102-p017, plan/ws102/design.md section 2.10): the protocol's requests and
 * the keyboard shortcuts that stand in for the face's buttons until p016.
 */

#ifndef KERN_KEILAND_EDIT_H
#define KERN_KEILAND_EDIT_H

#include "zwl.h"

int zwl_edit_request(struct zwl_object *object, uint32_t opcode, const unsigned char *bytes, size_t size);
void zwl_edit_object_gone(struct zwl_object *object);
int zwl_edit_key(struct zwl_server *server, uint32_t key, uint32_t state);

#endif
