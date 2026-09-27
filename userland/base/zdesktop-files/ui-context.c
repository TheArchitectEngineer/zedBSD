/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The context menus of zdesktop-files (spec §15, design §10.2): what a
 * right press offers, on the selected items, on the empty part of a
 * folder, in the trash, or on a place of the sidebar, and the actions only
 * a context menu has.
 *
 * zdesktop draws the menu at the press (menu.c); this part works out its
 * rows from the window's state and knows nothing of Wayland, so the host's
 * tests read the rows directly.
 */

#include "files.h"

#include <stdio.h>
#include <string.h>

/* The submenus' numbers, past any row's. */
#define CONTEXT_OPEN_WITH	100U
#define CONTEXT_TAGS		101U
#define CONTEXT_VIEW		102U
#define CONTEXT_SORT		103U

/* The number of a row that carries out an action: the action's, moved past the others. */
#define CONTEXT_ACTION_ID	1000U

static void context_items(struct fm_app *app, const struct fm_menu_state *state, struct fm_context *context);
static void context_trash(const struct fm_menu_state *state, struct fm_context *context);
static void context_empty(const struct fm_menu_state *state, struct fm_context *context);
static void context_place(struct fm_app *app, struct fm_context *context);
static void context_add(struct fm_context *context, unsigned parent, unsigned kind, const char *label, unsigned action, int enabled);
static void context_submenu(struct fm_context *context, unsigned id, const char *label, int enabled);
static void context_check(struct fm_context *context, unsigned parent, const char *label, unsigned action, int checked);
static int context_folder(struct fm_app *app);

/*
 * Works out the context menu of the last right press (app->context_*),
 * from what the window's menus would show now.
 */
void
fm_ui_context(
	struct fm_app *app,
	struct fm_context *context)
{
	struct fm_menu_state state;

	/* Nothing yet, and the state the rows are enabled by. */
	memset(context, 0, sizeof(*context));
	fm_ui_menu_state(app, &state);

	/* A place of the sidebar. */
	if (app->context_where == FM_CONTEXT_PLACE) {
		context_place(app, context);
		return;
	}

	/* The empty part of a folder, or of the trash. */
	if (app->context_where == FM_CONTEXT_EMPTY || state.selection == 0) {
		context_empty(&state, context);
		return;
	}

	/* Items in the trash, or elsewhere. */
	if (state.trash != 0) {
		context_trash(&state, context);
		return;
	}

	/* Items anywhere else. */
	context_items(app, &state, context);
}

/*
 * Carries out an action only a context menu has.  Returns 1 when the
 * action was one of them, 0 otherwise.
 */
int
fm_ui_context_action(
	struct fm_app *app,
	unsigned action)
{
	const struct fm_tab *tab;
	struct fm_location location;
	int item;

	/* A folder of the selection in a new tab. */
	if (action == FM_ACTION_OPEN_IN_NEW_TAB) {
		item = fm_preview_item(app);
		tab = fm_ui_tab(app);
		if (item < 0 || tab->listing.entries[item].folder == 0)
			return 1;
		memset(&location, 0, sizeof(location));
		location.kind = FM_LOCATION_FOLDER;
		snprintf(location.path, sizeof(location.path), "%s", tab->listing.entries[item].path);
		fm_tabs_new(app, &location);
		return 1;
	}

	/* The trash's actions on the selection and on the whole trash. */
	if (action == FM_ACTION_PUT_BACK) {
		fm_action_put_back(app);
		return 1;
	}

	/* The selection gone for good (after asking). */
	if (action == FM_ACTION_DELETE_NOW) {
		fm_action_delete(app);
		return 1;
	}

	/* The whole trash. */
	if (action == FM_ACTION_EMPTY_TRASH) {
		fm_action_empty_trash(app);
		return 1;
	}

	/* The pressed place of the sidebar: in a new tab, or off the sidebar. */
	if (action == FM_ACTION_PLACE_NEW_TAB) {
		if (app->context_place >= 0 && app->context_place < app->places.count)
			fm_tabs_new(app, &app->places.items[app->context_place].location);
		return 1;
	}

	/* The pressed favorite off the sidebar. */
	if (action == FM_ACTION_PLACE_REMOVE) {
		if (app->context_place >= 0 && app->context_place < app->places.count)
			fm_action_remove_favorite(app, app->context_place);
		return 1;
	}

	/* Not a context menu's own. */
	return 0;
}

/* Works out the rows for selected items outside the trash. */
static void
context_items(
	struct fm_app *app,
	const struct fm_menu_state *state,
	struct fm_context *context)
{
	int single;
	int folder;
	int checked;
	int index;

	/* One item selected, and whether it is a folder. */
	single = 0;
	if (state->selection == 1)
		single = 1;
	folder = context_folder(app);

	/* Opening: the default way, in a new tab (a folder), and the other ways. */
	context_add(context, 0U, FM_ROW_ITEM, "Open", FM_ACTION_OPEN, 1);
	if (single != 0 && folder != 0)
		context_add(context, 0U, FM_ROW_ITEM, "Open in New Tab", FM_ACTION_OPEN_IN_NEW_TAB, state->tabs < FM_TABS);
	context_submenu(context, CONTEXT_OPEN_WITH, "Open With", state->opener_count > 0);
	for (index = 0; index < state->opener_count; index++)
		context_add(context, CONTEXT_OPEN_WITH, FM_ROW_ITEM, state->openers[index], FM_ACTION_OPEN_WITH_FIRST + (unsigned)index, 1);

	/* The clipboard. */
	context_add(context, 0U, FM_ROW_LINE, "", 0U, 1);
	context_add(context, 0U, FM_ROW_ITEM, "Cut", FM_ACTION_CUT, 1);
	context_add(context, 0U, FM_ROW_ITEM, "Copy", FM_ACTION_COPY, 1);
	context_add(context, 0U, FM_ROW_ITEM, "Paste", FM_ACTION_PASTE, state->can_paste);

	/* Changing the items. */
	context_add(context, 0U, FM_ROW_LINE, "", 0U, 1);
	context_add(context, 0U, FM_ROW_ITEM, "Rename", FM_ACTION_RENAME, single);
	context_add(context, 0U, FM_ROW_ITEM, "Duplicate", FM_ACTION_DUPLICATE, 1);

	/* The tags, each checked when every selected item has it. */
	context_add(context, 0U, FM_ROW_LINE, "", 0U, 1);
	context_submenu(context, CONTEXT_TAGS, "Tags", state->tag_count > 0);
	for (index = 0; index < state->tag_count; index++) {
		checked = 0;
		if ((state->tags_checked & (1U << index)) != 0U)
			checked = 1;
		context_check(context, CONTEXT_TAGS, state->tags[index], FM_ACTION_TAG_FIRST + (unsigned)index, checked);
	}

	/* The information, and the trash. */
	context_add(context, 0U, FM_ROW_LINE, "", 0U, 1);
	context_add(context, 0U, FM_ROW_ITEM, "Get Info", FM_ACTION_GET_INFO, 1);
	context_add(context, 0U, FM_ROW_ITEM, "Move to Trash", FM_ACTION_TRASH, 1);
}

/* Works out the rows for selected items in the trash. */
static void
context_trash(
	const struct fm_menu_state *state,
	struct fm_context *context)
{
	/* Back where they were, or gone for good; the whole trash. */
	(void)state;
	context_add(context, 0U, FM_ROW_ITEM, "Put Back", FM_ACTION_PUT_BACK, 1);
	context_add(context, 0U, FM_ROW_ITEM, "Delete Immediately", FM_ACTION_DELETE_NOW, 1);
	context_add(context, 0U, FM_ROW_LINE, "", 0U, 1);
	context_add(context, 0U, FM_ROW_ITEM, "Empty Trash", FM_ACTION_EMPTY_TRASH, 1);
	context_add(context, 0U, FM_ROW_LINE, "", 0U, 1);
	context_add(context, 0U, FM_ROW_ITEM, "Get Info", FM_ACTION_GET_INFO, 1);
}

/* Works out the rows for the empty part of a place: new items, paste, how the items are shown. */
static void
context_empty(
	const struct fm_menu_state *state,
	struct fm_context *context)
{
	/* The trash offers to be emptied. */
	if (state->trash != 0) {
		context_add(context, 0U, FM_ROW_ITEM, "Empty Trash", FM_ACTION_EMPTY_TRASH, 1);
		context_add(context, 0U, FM_ROW_LINE, "", 0U, 1);
	}

	/* A folder takes new items and the clipboard's. */
	if (state->folder != 0) {
		context_add(context, 0U, FM_ROW_ITEM, "New Folder", FM_ACTION_NEW_FOLDER, 1);
		context_add(context, 0U, FM_ROW_ITEM, "Paste", FM_ACTION_PASTE, state->can_paste);
		context_add(context, 0U, FM_ROW_LINE, "", 0U, 1);
	}

	/* The view: icons or a list. */
	context_submenu(context, CONTEXT_VIEW, "View", 1);
	context_check(context, CONTEXT_VIEW, "as Icons", FM_ACTION_VIEW_ICONS, state->view == FM_VIEW_ICONS);
	context_check(context, CONTEXT_VIEW, "as List", FM_ACTION_VIEW_LIST, state->view == FM_VIEW_LIST);

	/* The order. */
	context_submenu(context, CONTEXT_SORT, "Sort By", 1);
	context_check(context, CONTEXT_SORT, "Name", FM_ACTION_SORT_NAME, state->sort == FM_SORT_NAME);
	context_check(context, CONTEXT_SORT, "Kind", FM_ACTION_SORT_KIND, state->sort == FM_SORT_KIND);
	context_check(context, CONTEXT_SORT, "Size", FM_ACTION_SORT_SIZE, state->sort == FM_SORT_SIZE);
	context_check(context, CONTEXT_SORT, "Date Modified", FM_ACTION_SORT_MODIFIED, state->sort == FM_SORT_MODIFIED);

	/* The hidden files. */
	context_check(context, 0U, "Show Hidden Files", FM_ACTION_SHOW_HIDDEN, state->hidden);
}

/* Works out the rows for a place of the sidebar. */
static void
context_place(
	struct fm_app *app,
	struct fm_context *context)
{
	const struct fm_place *place;
	int removable;

	/* The place pressed; none left means no rows. */
	if (app->context_place < 0 || app->context_place >= app->places.count)
		return;
	place = &app->places.items[app->context_place];

	/* In a new tab; a favorite folder can leave the sidebar. */
	context_add(context, 0U, FM_ROW_ITEM, "Open in New Tab", FM_ACTION_PLACE_NEW_TAB, app->tab_count < FM_TABS);
	removable = 0;
	if (place->section == FM_SECTION_FAVORITES && place->location.kind == FM_LOCATION_FOLDER)
		removable = 1;
	if (removable != 0) {
		context_add(context, 0U, FM_ROW_LINE, "", 0U, 1);
		context_add(context, 0U, FM_ROW_ITEM, "Remove from Sidebar", FM_ACTION_PLACE_REMOVE, 1);
	}
}

/* Adds a row at the end, when there is room; its number follows the last. */
static void
context_add(
	struct fm_context *context,
	unsigned parent,
	unsigned kind,
	const char *label,
	unsigned action,
	int enabled)
{
	struct fm_context_row *row;

	/* A full menu takes no more rows. */
	if (context->count == FM_CONTEXT_ROWS)
		return;

	/* The row, numbered from 1; a row that carries out an action is numbered by it (the same in every menu). */
	row = &context->rows[context->count];
	memset(row, 0, sizeof(*row));
	row->id = context->count + 1U;
	if (action != 0U)
		row->id = CONTEXT_ACTION_ID + action;
	row->parent = parent;
	row->kind = kind;
	snprintf(row->label, sizeof(row->label), "%s", label);
	row->action = action;
	if (enabled != 0)
		row->enabled = 1;
	context->count++;
}

/* Adds a submenu's row, numbered by the submenu (its rows name it as their parent). */
static void
context_submenu(
	struct fm_context *context,
	unsigned id,
	const char *label,
	int enabled)
{
	/* The row, then its number replaced by the submenu's. */
	context_add(context, 0U, FM_ROW_SUBMENU, label, 0U, enabled);
	if (context->count > 0U && context->rows[context->count - 1U].kind == FM_ROW_SUBMENU)
		context->rows[context->count - 1U].id = id;
}

/* Tells whether the item the selection is shown by is a folder. */
static int
context_folder(
	struct fm_app *app)
{
	const struct fm_tab *tab;
	int item;

	/* The item, when there is one. */
	item = fm_preview_item(app);
	if (item < 0)
		return 0;

	/* A folder. */
	tab = fm_ui_tab(app);
	if (tab->listing.entries[item].folder != 0)
		return 1;

	/* Not a folder. */
	return 0;
}

/* Adds a row that can be checked (a view, an order, a tag), checked or not. */
static void
context_check(
	struct fm_context *context,
	unsigned parent,
	const char *label,
	unsigned action,
	int checked)
{
	/* The row, then its mark. */
	context_add(context, parent, FM_ROW_CHECK, label, action, 1);
	if (checked != 0 && context->count > 0U)
		context->rows[context->count - 1U].checked = 1;
}
