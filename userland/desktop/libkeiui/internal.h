/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What the library's files share among themselves and do not export
 * (exports.map lets only the kui_ calls of <keiui.h> out).
 */

#ifndef KEIUI_INTERNAL_H
#define KEIUI_INTERNAL_H

#include <keiui.h>

/* The line pictures, from KUI_ICON_TILES on (icons-line.c). */
void keiui_icon_line_draw(struct kui_canvas *canvas, enum kui_icon icon, float x, float y, float size, kui_color color);

#endif
