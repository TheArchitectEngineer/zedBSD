/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The clipboard between clients and drag and drop: wl_data_device_manager,
 * wl_data_source, wl_data_device and wl_data_offer (ws035-p079, ws035-p084,
 * data.c).
 */

#ifndef ZWL_DATA_H
#define ZWL_DATA_H

#include "zwl.h"

int zwl_data_request(struct zwl_object *object, uint32_t opcode, const unsigned char *bytes, size_t size);
void zwl_data_focus(struct zwl_server *server, struct zwl_object *focus);
void zwl_data_object_gone(struct zwl_object *object);
void zwl_data_drag_motion(struct zwl_server *server, uint32_t time);
void zwl_data_drag_release(struct zwl_server *server);
void zwl_data_drag_cancel(struct zwl_server *server);
int zwl_data_emit_string(struct zwl_client *client, uint32_t id, uint32_t opcode, const char *text, int descriptor);
int zwl_data_read_string(const unsigned char *bytes, size_t size, size_t offset, const char **text, size_t *next);

/* The clipboard's history (clipboard.c, ws102-p018): a source asked for a type, and zdesktop's own offered text made the selection (data.c). */
int zwl_data_send(struct zwl_object *source, const char *type, int descriptor);
void zwl_data_select_offered(struct zwl_server *server);

/* The history (clipboard.c): a new selection heard, the reading of its pipe, zdesktop's text written to a reader, the list logged. */
void zwl_clipboard_selected(struct zwl_server *server, struct zwl_object *source);
void zwl_clipboard_poll(struct zwl_server *server);
void zwl_clipboard_offer_write(int descriptor);
void zwl_clipboard_history_log(struct zwl_server *server);

/* The primary selection: zwp_primary_selection_device_manager_v1 and its objects (ws035-p100, primary.c). */
int zwl_primary_request(struct zwl_object *object, uint32_t opcode, const unsigned char *bytes, size_t size);
void zwl_primary_focus(struct zwl_server *server, struct zwl_object *focus);
void zwl_primary_object_gone(struct zwl_object *object);

#endif
