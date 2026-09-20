/* -*- mode: c; c-file-style: "linux"; tab-width: 8; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

#ifndef DRV_PCAT_SERIAL_MIRROR_H
#define DRV_PCAT_SERIAL_MIRROR_H

/*
 * Repeats one console character on the first serial port.
 *
 * The display shows the console to a person at the machine; this repeats the
 * same stream where a host running an emulator, or a serial cable, can read
 * it.  Output only: nothing here reads the port, so the terminal discipline
 * above the console is unaffected.
 */
void drv_pcat_serial_mirror(int character);

#endif
