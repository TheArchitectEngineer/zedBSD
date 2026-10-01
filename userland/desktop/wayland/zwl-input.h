/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Separates device access from the seat's interpretation of evdev events. */
#ifndef ZWL_INPUT_H
#define ZWL_INPUT_H

#include "zwl-evdev.h"
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct zwl_server;

/*
 * The capability bitmaps of one evdev node, as EVIOCGBIT reports them.
 *
 * One instance lives on the stack while a node is classified.
 */
struct zwl_input_caps {
	unsigned long event[EV_MAX / (8U * sizeof(unsigned long)) + 1U];
	unsigned long key[KEY_MAX / (8U * sizeof(unsigned long)) + 1U];
	unsigned long relative[REL_MAX / (8U * sizeof(unsigned long)) + 1U];
	unsigned long absolute[ABS_MAX / (8U * sizeof(unsigned long)) + 1U];
};

/* Opens unseen input devices and hands their capabilities to the seat. */
void zwl_input_scan(struct zwl_server *server);
/* Reads an axis range; returns zero or an errno value. */
int zwl_input_device_absinfo(int descriptor, uint32_t axis, struct input_absinfo *info);
/* Reads the device's name; returns zero or an errno value. */
int zwl_input_device_name(int descriptor, char *name, size_t size);
/* Reads the device's identity; returns zero or an errno value. */
int zwl_input_device_id(int descriptor, struct input_id *id);
/* Returns an event count, zero at EOF, or -1 with errno (EIO for a torn event). */
ssize_t zwl_input_device_read(int descriptor, struct input_event *events, size_t capacity);
/* Closes the descriptor and returns any OS ownership of the device. */
void zwl_input_device_close(struct zwl_server *server, int descriptor);
/* Classifies and takes ownership of the descriptor, closing unsupported nodes. */
void zwl_input_probe(struct zwl_server *server, int descriptor, const char *path, const struct zwl_input_caps *capabilities);

#endif
