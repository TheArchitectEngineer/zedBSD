/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The parts of PDF Viewer that speak Wayland and zdesktop's extensions:
 * the window (libkeiland's kl_window since ws090-p008: the toplevel, the
 * seat's input and the frames shown with Vulkan), the menus (menu.c) and
 * the titlebar's controls (titlebar.c).  The host tests build the rest of
 * the program without them.
 */

#ifndef PDFVIEWER_WINDOW_H
#define PDFVIEWER_WINDOW_H

#include "viewer.h"
#include "touch.h"

#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>
#include <keiland.h>

/*
 * The window: libkeiland's window, which queues the input (the menus' and
 * the titlebar's actions among it, posted in the order they came) and
 * shows the frames.
 *
 * One lives for the whole run.
 */
struct pv_window {
	struct kl_window *kui;
};

/*
 * What the menus and the titlebar show of the viewer: whether a document
 * is open, the page and the count, the mode and the fit.
 */
struct pv_state {
	int has_document;
	size_t page;
	size_t count;
	int mode;
	int fit;
	int thumbnails;
};

/*
 * The window's menus as given to zdesktop (menu.c): the connection's menu
 * service (NULL when the compositor has none, and the window then has no
 * menus), the menu and the window's place for it, and the state the menus
 * last showed.
 */
struct pv_menu {
	struct kl_menu_service *service;
	struct kl_menu *menu;
	struct kl_window_menu *window_menu;
	struct pv_state shown;
	struct pv_window *window;
};

/*
 * The window's titlebar in zdesktop (titlebar.c): zdesktop's titlebar
 * object (NULL without one), the state it last showed, and whether it was
 * ever sent.
 */
struct pv_titlebar {
	struct pv_window *window;
	struct kl_titlebar *titlebar;
	struct pv_state shown;
	int sent;
};

/* The window's actions: the menus' and the titlebar's choices, queued among the input. */
void pv_window_action(struct pv_window *window, uint32_t action);

/* The menus (menu.c). */
int pv_menu_open(struct pv_menu *menu, struct pv_window *window, const struct pv_state *state);
void pv_menu_refresh(struct pv_menu *menu, const struct pv_state *state);
void pv_menu_close(struct pv_menu *menu);

/* The titlebar's controls (titlebar.c). */
int pv_titlebar_open(struct pv_titlebar *titlebar, struct pv_window *window, const struct pv_state *state);
void pv_titlebar_refresh(struct pv_titlebar *titlebar, const struct pv_state *state);
void pv_titlebar_close(struct pv_titlebar *titlebar);

#endif
