/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The server side of the System Menu protocol (WS070, plan/ws070/design.md
 * section 2): xdg_menu_manager_v1, xdg_menu_v1 and xdg_toplevel_menu_v1.
 *
 * A client builds a menu model (a tree of numbered items) in transactions:
 * begin_update copies the shown model, the changes go to the copy, and
 * commit shows the copy at once.  An xdg_toplevel_menu_v1 ties a model to a
 * window; several windows may show one model.  What the user chooses comes
 * back as xdg_toplevel_menu_v1.activated.  Drawing and input are in
 * menu-shell.c.
 */

#include "menu.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The bounds of one model: its items, how deep its tree is, and its strings in bytes. */
#define MENU_ITEMS_MAX		1024U
#define MENU_DEPTH_MAX		8U
#define MENU_TEXT_MAX		255U

/* The highest role and the modifier bits a shortcut may name. */
#define MENU_ROLE_LAST		19U
#define MENU_MODIFIERS_ALL	15U

/* xdg_menu_manager_v1's requests and error. */
#define MANAGER_DESTROY			0U
#define MANAGER_CREATE_MENU		1U
#define MANAGER_GET_TOPLEVEL_MENU	2U
#define MANAGER_ERROR_ALREADY_EXISTS	0U

/* xdg_menu_v1's requests. */
#define MENU_DESTROY		0U
#define MENU_BEGIN_UPDATE	1U
#define MENU_COMMIT		2U
#define MENU_APPEND_ITEM	3U
#define MENU_INSERT_ITEM	4U
#define MENU_REMOVE_ITEM	5U
#define MENU_SET_LABEL		6U
#define MENU_SET_ACTION		7U
#define MENU_SET_ENABLED	8U
#define MENU_SET_VISIBLE	9U
#define MENU_SET_CHECKED	10U
#define MENU_SET_ROLE		11U
#define MENU_SET_ICON_NAME	12U
#define MENU_SET_SHORTCUT	13U

/* xdg_menu_v1's errors. */
#define MENU_ERROR_INVALID_ID		0U
#define MENU_ERROR_INVALID_PARENT	1U
#define MENU_ERROR_INVALID_TYPE		2U
#define MENU_ERROR_INVALID_VALUE	3U
#define MENU_ERROR_NOT_UPDATING		4U
#define MENU_ERROR_ALREADY_UPDATING	5U
#define MENU_ERROR_BAD_SERIAL		6U
#define MENU_ERROR_TOO_LARGE		7U

/* xdg_toplevel_menu_v1's requests and events. */
#define PLACE_DESTROY		0U
#define PLACE_SET_MENU		1U
#define PLACE_ACTIVATED		0U
#define PLACE_OPENED		1U
#define PLACE_CLOSED		2U

static uint32_t menu_word(const unsigned char *bytes, size_t offset);
static int menu_string(const unsigned char *bytes, size_t size, size_t offset, const char **text, size_t *next);
static int manager_request(struct zwl_object *manager, uint32_t opcode, const unsigned char *bytes, size_t size);
static int model_request(struct zwl_object *menu, uint32_t opcode, const unsigned char *bytes, size_t size);
static int model_edit(struct zwl_object *menu, uint32_t opcode, const unsigned char *bytes, size_t size);
static int place_request(struct zwl_object *place, uint32_t opcode, const unsigned char *bytes, size_t size);
static int model_begin(struct zwl_object *menu, uint32_t serial);
static int model_commit(struct zwl_object *menu, uint32_t serial);
static int model_add(struct zwl_object *menu, uint32_t id, uint32_t parent, uint32_t before, uint32_t type, const char *label, uint32_t action);
static int model_remove(struct zwl_object *menu, uint32_t id);
static int model_set_number(struct zwl_object *menu, uint32_t opcode, uint32_t id, uint32_t value);
static int model_set_text(struct zwl_object *menu, uint32_t opcode, uint32_t id, const char *text);
static int model_set_shortcut(struct zwl_object *menu, uint32_t id, uint32_t modifiers, uint32_t keysym);
static int model_fail(struct zwl_object *menu, uint32_t code, const char *reason);
static void model_free(struct zwl_menu_model *model);
static int pending_index(const struct zwl_menu_model *model, uint32_t id);
static unsigned pending_depth(const struct zwl_menu_model *model, uint32_t id);
static int pending_room(struct zwl_menu_model *model);
static int items_copy(struct zwl_menu_item *to, const struct zwl_menu_item *from, unsigned count);
static void items_free(struct zwl_menu_item *items, unsigned count);
static char *text_copy(const char *text);
static uint32_t place_seat(struct zwl_object *place);

/*
 * Carries out one request on a System Menu object.
 *
 * Returns 0, or EPROTO once a protocol error has been queued (a specific one
 * when the object's interface names it, otherwise the caller's generic one).
 */
int
zwl_menu_request(
	struct zwl_object *object,
	uint32_t opcode,
	const unsigned char *bytes,
	size_t size)
{
	int error;

	/* Each interface has its own requests. */
	switch (object->kind) {
	case ZWL_MENU_MANAGER:
		error = manager_request(object, opcode, bytes, size);
		break;
	case ZWL_MENU:
		error = model_request(object, opcode, bytes, size);
		break;
	case ZWL_TOPLEVEL_MENU:
		error = place_request(object, opcode, bytes, size);
		break;
	default:
		error = EPROTO;
		break;
	}

	/* Reports a request that was refused. */
	if (error != 0)
		return error;

	/* Succeeded: the request was carried out. */
	return 0;
}

/*
 * Unties an object that is going from the System Menu objects that name it.
 *
 * A toplevel, an xdg_toplevel_menu_v1 or an xdg_menu_v1 may go before the
 * others; the survivors stop naming it, and a menu open on it closes first.
 * A model goes with its xdg_menu_v1.
 */
void
zwl_menu_object_gone(
	struct zwl_object *object)
{
	struct zwl_object *other;

	/* A menu drawn or open from the object closes while its links still say whose it was. */
	zwl_menu_forget(object->client->server, object);

	/* A toplevel's place for a menu is left without a window. */
	if (object->kind == ZWL_TOPLEVEL && object->toplevel_menu != NULL) {
		object->toplevel_menu->top = NULL;
		object->toplevel_menu = NULL;
		return;
	}

	/* A place leaves its window, and shows nothing. */
	if (object->kind == ZWL_TOPLEVEL_MENU) {
		if (object->top != NULL)
			object->top->toplevel_menu = NULL;
		object->top = NULL;
		object->shown_menu = NULL;
		return;
	}

	/* Nothing else is tied to anything but a model. */
	if (object->kind != ZWL_MENU)
		return;

	/* The places that showed this menu show nothing. */
	for (other = object->client->objects; other != NULL; other = other->next) {
		/* Only a place can show a menu. */
		if (other->kind != ZWL_TOPLEVEL_MENU)
			continue;

		/* It stops naming the menu. */
		if (other->shown_menu == object)
			other->shown_menu = NULL;
	}

	/* The model goes with its object, and the windows are drawn without it. */
	model_free(object->menu_model);
	object->menu_model = NULL;
	object->client->server->dirty = 1;
}

/*
 * Finds the menu a window shows: its committed model, and the
 * xdg_toplevel_menu_v1 (place) its activations go to.  Returns NULL when the
 * window shows none.
 */
struct zwl_menu_model *
zwl_menu_of_surface(
	struct zwl_object *surface,
	struct zwl_object **place)
{
	struct zwl_object *toplevel;
	struct zwl_object *menu;

	/* Nothing is found until the whole chain is. */
	*place = NULL;

	/* The surface's xdg_surface and its toplevel. */
	if (surface == NULL || surface->dead || surface->role == NULL)
		return NULL;
	toplevel = surface->role->top;
	if (toplevel == NULL || toplevel->dead)
		return NULL;

	/* The toplevel's place for a menu, and the menu in it. */
	if (toplevel->toplevel_menu == NULL)
		return NULL;
	menu = toplevel->toplevel_menu->shown_menu;
	if (menu == NULL || menu->dead || menu->menu_model == NULL)
		return NULL;

	/* Succeeded: the model and where its activations go. */
	*place = toplevel->toplevel_menu;
	return menu->menu_model;
}

/*
 * Finds a committed item by its ID; NULL when the model has none.
 */
const struct zwl_menu_item *
zwl_menu_item(
	const struct zwl_menu_model *model,
	uint32_t id)
{
	unsigned index;

	/* The model has at most MENU_ITEMS_MAX items, so a search is cheap. */
	for (index = 0; index < model->count; index++) {
		/* The item with this ID. */
		if (model->items[index].id == id)
			return &model->items[index];
	}

	/* No item has the ID. */
	return NULL;
}

/*
 * Lists the visible children of an item (ZWL_MENU_ROOT for the top level)
 * in their order, as they are shown: a separator only between two other
 * items, never two together.  Returns how many were stored.
 */
unsigned
zwl_menu_children(
	const struct zwl_menu_model *model,
	uint32_t parent,
	const struct zwl_menu_item **children,
	unsigned capacity)
{
	const struct zwl_menu_item *separator;
	const struct zwl_menu_item *item;
	unsigned index;
	unsigned count;

	/* A separator waits until an item follows it. */
	separator = NULL;
	count = 0;
	for (index = 0; index < model->count; index++) {
		/* Only the parent's visible children, while there is room. */
		item = &model->items[index];
		if (item->parent != parent || !item->visible)
			continue;
		if (count == capacity)
			break;

		/* A separator is kept only after something, and once. */
		if (item->type == ZWL_MENU_SEPARATOR) {
			if (count != 0)
				separator = item;
			continue;
		}

		/* An item brings the separator before it. */
		if (separator != NULL && count + 1U < capacity) {
			children[count] = separator;
			count++;
		}

		/* The separator is used up. */
		separator = NULL;

		/* The item itself. */
		children[count] = item;
		count++;
	}

	/* Succeeded: the children as shown. */
	return count;
}

/*
 * Tells a window's client that the user chose an item: its ID, its action,
 * the client's seat and a new serial for the input that chose it.
 */
void
zwl_menu_send_activated(
	struct zwl_object *place,
	const struct zwl_menu_item *item,
	const char *via)
{
	struct zwl_server *server;
	uint32_t words[4];
	int error;

	/* A place whose client has failed hears nothing. */
	server = place->client->server;
	if (place->dead || place->client->fatal)
		return;

	/* The item, its action, the seat (or none) and the serial. */
	words[0] = item->id;
	words[1] = item->action;
	words[2] = place_seat(place);
	words[3] = zwl_next_serial(server);

	/* The event; a client that cannot take it is failed. */
	error = zwl_emit(place->client, place->id, PLACE_ACTIVATED, words, sizeof(words));
	if (error != 0) {
		place->client->fatal = 1;
		place->client->fatal_time = zwl_milliseconds();
	}

	/* The log line the tests read. */
	printf("ZWL MENU activate client=%llu place=%u item=%u action=%u serial=%u via=%s\n",
	       (unsigned long long)place->client->number, place->id, item->id, item->action, words[3], via);
}

/*
 * Tells a window's client that the popup of a submenu opened or closed.
 */
void
zwl_menu_send_popup(
	struct zwl_object *place,
	uint32_t item,
	unsigned opened)
{
	uint32_t opcode;
	int error;

	/* A place whose client has failed hears nothing. */
	if (place->dead || place->client->fatal)
		return;

	/* opened or closed, with the submenu's item. */
	opcode = PLACE_CLOSED;
	if (opened)
		opcode = PLACE_OPENED;
	error = zwl_emit(place->client, place->id, opcode, &item, sizeof(item));
	if (error != 0) {
		place->client->fatal = 1;
		place->client->fatal_time = zwl_milliseconds();
	}
}

/* Reads one native-endian protocol word the caller has checked is there. */
static uint32_t
menu_word(
	const unsigned char *bytes,
	size_t offset)
{
	uint32_t word;

	/* The payload need not be aligned. */
	memcpy(&word, bytes + offset, sizeof(word));

	/* Succeeded: the word. */
	return word;
}

/* Reads one non-null string argument and where the next argument starts; EPROTO when it is malformed. */
static int
menu_string(
	const unsigned char *bytes,
	size_t size,
	size_t offset,
	const char **text,
	size_t *next)
{
	uint32_t length;
	size_t aligned;
	size_t actual;

	/* The length word must be in the payload. */
	if (offset > size || size - offset < 4U)
		return EPROTO;

	/* A non-null string has at least its terminator, within the payload. */
	length = menu_word(bytes, offset);
	if (length == 0U || length > size - offset - 4U)
		return EPROTO;

	/* Its storage is padded to a word, which must be in the payload too. */
	aligned = ((size_t)length + 3U) & ~(size_t)3U;
	if (aligned > size - offset - 4U)
		return EPROTO;

	/* It ends with its terminator. */
	*text = (const char *)bytes + offset + 4U;
	if ((*text)[length - 1U] != '\0')
		return EPROTO;

	/* And has no other NUL, which could hide bytes. */
	actual = strlen(*text);
	if (actual + 1U != length)
		return EPROTO;

	/* Succeeded: the next argument follows the padded string. */
	*next = offset + 4U + aligned;
	return 0;
}

/* Carries out a request of xdg_menu_manager_v1. */
static int
manager_request(
	struct zwl_object *manager,
	uint32_t opcode,
	const unsigned char *bytes,
	size_t size)
{
	struct zwl_object *created;
	struct zwl_object *toplevel;
	uint32_t id;

	/* The binding goes; its menus and places stay. */
	if (opcode == MANAGER_DESTROY) {
		if (size != 0U)
			return EPROTO;
		zwl_object_destroy(manager);
		return 0;
	}

	/* A new, empty menu model. */
	if (opcode == MANAGER_CREATE_MENU) {
		if (size != 4U)
			return EPROTO;

		/* The object; its model is allocated with it. */
		id = menu_word(bytes, 0U);
		created = zwl_create(manager->client, id, ZWL_MENU, manager->version);
		if (created == NULL)
			return EPROTO;
		created->menu_model = calloc(1, sizeof(*created->menu_model));
		if (created->menu_model == NULL) {
			zwl_object_destroy(created);
			return EPROTO;
		}

		/* Succeeded: the client owns the menu. */
		return 0;
	}

	/* Only get_toplevel_menu is left. */
	if (opcode != MANAGER_GET_TOPLEVEL_MENU || size != 8U)
		return EPROTO;

	/* The window must be one of the client's toplevels. */
	toplevel = zwl_find(manager->client, menu_word(bytes, 4U));
	if (toplevel == NULL || toplevel->kind != ZWL_TOPLEVEL)
		return EPROTO;

	/* A window has one place for a menu at a time. */
	if (toplevel->toplevel_menu != NULL) {
		(void)zwl_error_code(manager->client, manager->id, MANAGER_ERROR_ALREADY_EXISTS, "the toplevel already has an xdg_toplevel_menu_v1");
		return EPROTO;
	}

	/* The place, tied to the window from both ends. */
	id = menu_word(bytes, 0U);
	created = zwl_create(manager->client, id, ZWL_TOPLEVEL_MENU, manager->version);
	if (created == NULL)
		return EPROTO;
	created->top = toplevel;
	toplevel->toplevel_menu = created;

	/* Succeeded: the window has a place for a menu. */
	return 0;
}

/* Carries out a request of xdg_menu_v1. */
static int
model_request(
	struct zwl_object *menu,
	uint32_t opcode,
	const unsigned char *bytes,
	size_t size)
{
	int error;

	/* The menu goes; windows showing it show nothing (zwl_menu_object_gone). */
	if (opcode == MENU_DESTROY) {
		if (size != 0U)
			return EPROTO;
		zwl_object_destroy(menu);
		return 0;
	}

	/* A transaction starts. */
	if (opcode == MENU_BEGIN_UPDATE) {
		if (size != 4U)
			return EPROTO;
		error = model_begin(menu, menu_word(bytes, 0U));
		return error;
	}

	/* A transaction is shown. */
	if (opcode == MENU_COMMIT) {
		if (size != 4U)
			return EPROTO;
		error = model_commit(menu, menu_word(bytes, 0U));
		return error;
	}

	/* Every other request changes the model, which only a transaction may. */
	if (opcode > MENU_SET_SHORTCUT)
		return EPROTO;
	if (!menu->menu_model->updating) {
		error = model_fail(menu, MENU_ERROR_NOT_UPDATING, "a change outside begin_update and commit");
		return error;
	}

	/* The change itself. */
	error = model_edit(menu, opcode, bytes, size);
	if (error != 0)
		return error;

	/* Succeeded: the change waits for the commit. */
	return 0;
}

/* Decodes one change of a model in a transaction and applies it to the pending copy. */
static int
model_edit(
	struct zwl_object *menu,
	uint32_t opcode,
	const unsigned char *bytes,
	size_t size)
{
	const char *text;
	size_t next;
	int error;

	/* Each change has its own arguments. */
	switch (opcode) {
	case MENU_APPEND_ITEM:
		/* id, parent, type, label, action. */
		error = menu_string(bytes, size, 12U, &text, &next);
		if (error != 0 || next + 4U != size)
			return EPROTO;
		error = model_add(menu, menu_word(bytes, 0U), menu_word(bytes, 4U), 0U, menu_word(bytes, 8U), text, menu_word(bytes, next));
		break;
	case MENU_INSERT_ITEM:
		/* id, parent, the sibling it goes before, type, label, action. */
		error = menu_string(bytes, size, 16U, &text, &next);
		if (error != 0 || next + 4U != size)
			return EPROTO;
		error = model_add(menu, menu_word(bytes, 0U), menu_word(bytes, 4U), menu_word(bytes, 8U), menu_word(bytes, 12U), text, menu_word(bytes, next));
		break;
	case MENU_REMOVE_ITEM:
		/* The item. */
		if (size != 4U)
			return EPROTO;
		error = model_remove(menu, menu_word(bytes, 0U));
		break;
	case MENU_SET_LABEL:
	case MENU_SET_ICON_NAME:
		/* The item and a string. */
		error = menu_string(bytes, size, 4U, &text, &next);
		if (error != 0 || next != size)
			return EPROTO;
		error = model_set_text(menu, opcode, menu_word(bytes, 0U), text);
		break;
	case MENU_SET_SHORTCUT:
		/* The item, the modifiers and the keysym. */
		if (size != 12U)
			return EPROTO;
		error = model_set_shortcut(menu, menu_word(bytes, 0U), menu_word(bytes, 4U), menu_word(bytes, 8U));
		break;
	default:
		/* The item and one number: action, enabled, visible, checked, role. */
		if (size != 8U)
			return EPROTO;
		error = model_set_number(menu, opcode, menu_word(bytes, 0U), menu_word(bytes, 4U));
		break;
	}

	/* Reports a change that was refused. */
	if (error != 0)
		return error;

	/* Succeeded: the pending copy has the change. */
	return 0;
}

/* Carries out a request of xdg_toplevel_menu_v1. */
static int
place_request(
	struct zwl_object *place,
	uint32_t opcode,
	const unsigned char *bytes,
	size_t size)
{
	struct zwl_object *menu;
	uint32_t id;

	/* The place goes; its window shows no menu (zwl_menu_object_gone). */
	if (opcode == PLACE_DESTROY) {
		if (size != 0U)
			return EPROTO;
		place->client->server->dirty = 1;
		zwl_object_destroy(place);
		return 0;
	}

	/* Only set_menu is left, with one nullable menu. */
	if (opcode != PLACE_SET_MENU || size != 4U)
		return EPROTO;

	/* A menu named must be one of the client's. */
	id = menu_word(bytes, 0U);
	menu = NULL;
	if (id != 0U) {
		menu = zwl_find(place->client, id);
		if (menu == NULL || menu->kind != ZWL_MENU)
			return EPROTO;
	}

	/* A place whose window has gone keeps nothing. */
	if (place->top == NULL)
		return 0;

	/* The window shows the menu's committed state from the next frame. */
	place->shown_menu = menu;
	place->client->server->dirty = 1;
	printf("ZWL MENU set client=%llu place=%u menu=%u\n", (unsigned long long)place->client->number, place->id, id);

	/* Succeeded: the window shows the menu. */
	return 0;
}

/* Starts a transaction: the changes that follow go to a copy of the shown model. */
static int
model_begin(
	struct zwl_object *menu,
	uint32_t serial)
{
	struct zwl_menu_model *model;
	unsigned capacity;
	int error;

	/* One transaction at a time. */
	model = menu->menu_model;
	if (model->updating) {
		error = model_fail(menu, MENU_ERROR_ALREADY_UPDATING, "begin_update inside a transaction");
		return error;
	}

	/* The copy has room for the shown items and a few more. */
	capacity = model->count + 16U;
	model->pending = calloc(capacity, sizeof(*model->pending));
	if (model->pending == NULL)
		return EPROTO;
	model->pending_capacity = capacity;

	/* Every item, with its strings. */
	error = items_copy(model->pending, model->items, model->count);
	if (error != 0) {
		free(model->pending);
		model->pending = NULL;
		model->pending_capacity = 0;
		return EPROTO;
	}

	/* The transaction is open under the client's serial. */
	model->pending_count = model->count;
	model->updating = 1;
	model->update_serial = serial;

	/* Succeeded: changes may follow. */
	return 0;
}

/* Ends a transaction: the copy becomes the shown model at once. */
static int
model_commit(
	struct zwl_object *menu,
	uint32_t serial)
{
	struct zwl_menu_model *model;
	int error;

	/* Only an open transaction, under the serial it was opened with. */
	model = menu->menu_model;
	if (!model->updating) {
		error = model_fail(menu, MENU_ERROR_NOT_UPDATING, "commit without begin_update");
		return error;
	}

	/* The serial pairs the commit with its begin_update. */
	if (serial != model->update_serial) {
		error = model_fail(menu, MENU_ERROR_BAD_SERIAL, "commit names another serial than begin_update");
		return error;
	}

	/* The old model goes and the copy takes its place. */
	items_free(model->items, model->count);
	free(model->items);
	model->items = model->pending;
	model->count = model->pending_count;
	model->capacity = model->pending_capacity;
	model->pending = NULL;
	model->pending_count = 0;
	model->pending_capacity = 0;

	/*
	 * The generation tells the drawing that the model changed; the open
	 * popups are checked against it on the next pass (menu-shell.c).
	 */
	model->updating = 0;
	model->generation++;
	menu->client->server->dirty = 1;

	/* Succeeded: the log line the tests read. */
	printf("ZWL MENU commit client=%llu menu=%u serial=%u items=%u generation=%llu\n",
	       (unsigned long long)menu->client->number, menu->id, serial, model->count, (unsigned long long)model->generation);
	return 0;
}

/* Adds an item to the pending copy, before a sibling or after the last child. */
static int
model_add(
	struct zwl_object *menu,
	uint32_t id,
	uint32_t parent,
	uint32_t before,
	uint32_t type,
	const char *label,
	uint32_t action)
{
	struct zwl_menu_model *model;
	struct zwl_menu_item *item;
	unsigned depth;
	size_t length;
	int position;
	int found;
	int error;

	/* A new ID: not zero, not in use. */
	model = menu->menu_model;
	found = pending_index(model, id);
	if (id == 0U || found >= 0) {
		error = model_fail(menu, MENU_ERROR_INVALID_ID, "an item ID that is zero or already in use");
		return error;
	}

	/* One of the five types. */
	if (type > ZWL_MENU_SUBMENU) {
		error = model_fail(menu, MENU_ERROR_INVALID_TYPE, "an unknown item type");
		return error;
	}

	/* The parent is the top level or a submenu. */
	if (parent != ZWL_MENU_ROOT) {
		found = pending_index(model, parent);
		if (found < 0 || model->pending[found].type != ZWL_MENU_SUBMENU) {
			error = model_fail(menu, MENU_ERROR_INVALID_PARENT, "a parent that is not a submenu");
			return error;
		}
	}

	/* The model stays within its bounds. */
	depth = pending_depth(model, parent) + 1U;
	length = strlen(label);
	if (model->pending_count >= MENU_ITEMS_MAX || depth > MENU_DEPTH_MAX || length > MENU_TEXT_MAX) {
		error = model_fail(menu, MENU_ERROR_TOO_LARGE, "too many items, too deep, or too long a label");
		return error;
	}

	/* The sibling it goes before, or the end. */
	position = (int)model->pending_count;
	if (before != 0U) {
		position = pending_index(model, before);
		if (position < 0 || model->pending[position].parent != parent) {
			error = model_fail(menu, MENU_ERROR_INVALID_PARENT, "before_id is not a child of the parent");
			return error;
		}
	}

	/* Room for one more. */
	error = pending_room(model);
	if (error != 0)
		return EPROTO;

	/* The item's place in the array, made free. */
	memmove(&model->pending[position + 1], &model->pending[position], (model->pending_count - (unsigned)position) * sizeof(*model->pending));
	item = &model->pending[position];
	memset(item, 0, sizeof(*item));

	/* The item, with the defaults for what the request does not give. */
	item->id = id;
	item->parent = parent;
	item->type = type;
	item->action = action;
	item->enabled = 1;
	item->visible = 1;
	item->label = text_copy(label);
	item->icon_name = text_copy("");
	model->pending_count++;
	if (item->label == NULL || item->icon_name == NULL)
		return EPROTO;

	/* Succeeded: the item is in the pending copy. */
	return 0;
}

/* Removes an item and everything under it from the pending copy. */
static int
model_remove(
	struct zwl_object *menu,
	uint32_t id)
{
	struct zwl_menu_model *model;
	unsigned char *doomed;
	unsigned index;
	unsigned kept;
	unsigned more;
	int found;
	int error;

	/* The item must be there. */
	model = menu->menu_model;
	found = pending_index(model, id);
	if (found < 0) {
		error = model_fail(menu, MENU_ERROR_INVALID_ID, "remove_item names no item");
		return error;
	}

	/* A mark for each item that goes. */
	doomed = calloc(model->pending_count, 1);
	if (doomed == NULL)
		return EPROTO;
	doomed[found] = 1;

	/* The marks spread to children until no new one is made (the tree is at most MENU_DEPTH_MAX deep). */
	more = 1;
	while (more) {
		more = 0;
		for (index = 0; index < model->pending_count; index++) {
			/* A child of a doomed item goes too. */
			if (doomed[index])
				continue;
			found = pending_index(model, model->pending[index].parent);
			if (found >= 0 && doomed[found]) {
				doomed[index] = 1;
				more = 1;
			}
		}
	}

	/* The marked items are freed and the rest close up in their order. */
	kept = 0;
	for (index = 0; index < model->pending_count; index++) {
		if (doomed[index]) {
			items_free(&model->pending[index], 1U);
			continue;
		}

		/* A kept item moves down over the freed ones. */
		model->pending[kept] = model->pending[index];
		kept++;
	}

	/* Succeeded: the item and its subtree are gone from the copy. */
	model->pending_count = kept;
	free(doomed);
	return 0;
}

/* Sets one numeric attribute of an item in the pending copy. */
static int
model_set_number(
	struct zwl_object *menu,
	uint32_t opcode,
	uint32_t id,
	uint32_t value)
{
	struct zwl_menu_item *item;
	int found;
	int error;

	/* The item must be there. */
	found = pending_index(menu->menu_model, id);
	if (found < 0) {
		error = model_fail(menu, MENU_ERROR_INVALID_ID, "a change names no item");
		return error;
	}

	/* The action is any number the client chooses. */
	item = &menu->menu_model->pending[found];
	if (opcode == MENU_SET_ACTION) {
		item->action = value;
		return 0;
	}

	/* A role is one of those the protocol names. */
	if (opcode == MENU_SET_ROLE) {
		if (value > MENU_ROLE_LAST) {
			error = model_fail(menu, MENU_ERROR_INVALID_VALUE, "an unknown role");
			return error;
		}

		/* Succeeded: the role is kept (v1 draws nothing from it). */
		item->role = value;
		return 0;
	}

	/* The rest are true or false. */
	if (value > 1U) {
		error = model_fail(menu, MENU_ERROR_INVALID_VALUE, "a boolean that is not 0 or 1");
		return error;
	}

	/* enabled, visible, or checked (which only a checkbox or a radio item has). */
	if (opcode == MENU_SET_ENABLED) {
		item->enabled = value;
	} else if (opcode == MENU_SET_VISIBLE) {
		item->visible = value;
	} else {
		if (item->type != ZWL_MENU_CHECKBOX && item->type != ZWL_MENU_RADIO) {
			error = model_fail(menu, MENU_ERROR_INVALID_TYPE, "set_checked on an item that cannot be checked");
			return error;
		}

		/* The client's state of the item. */
		item->checked = value;
	}

	/* Succeeded: the copy has the value. */
	return 0;
}

/* Sets an item's label or icon name in the pending copy. */
static int
model_set_text(
	struct zwl_object *menu,
	uint32_t opcode,
	uint32_t id,
	const char *text)
{
	struct zwl_menu_item *item;
	char *copy;
	size_t length;
	int found;
	int error;

	/* The item must be there. */
	found = pending_index(menu->menu_model, id);
	if (found < 0) {
		error = model_fail(menu, MENU_ERROR_INVALID_ID, "a change names no item");
		return error;
	}

	/* The text must be within bounds. */
	length = strlen(text);
	if (length > MENU_TEXT_MAX) {
		error = model_fail(menu, MENU_ERROR_TOO_LARGE, "too long a string");
		return error;
	}

	/* The item keeps its own copy. */
	copy = text_copy(text);
	if (copy == NULL)
		return EPROTO;

	/* The new text replaces the label or the icon name. */
	item = &menu->menu_model->pending[found];
	if (opcode == MENU_SET_LABEL) {
		free(item->label);
		item->label = copy;
	} else {
		free(item->icon_name);
		item->icon_name = copy;
	}

	/* Succeeded: the copy has the text. */
	return 0;
}

/* Sets an item's shortcut in the pending copy (keysym 0 removes it). */
static int
model_set_shortcut(
	struct zwl_object *menu,
	uint32_t id,
	uint32_t modifiers,
	uint32_t keysym)
{
	struct zwl_menu_item *item;
	int found;
	int error;

	/* The item must be there. */
	found = pending_index(menu->menu_model, id);
	if (found < 0) {
		error = model_fail(menu, MENU_ERROR_INVALID_ID, "a change names no item");
		return error;
	}

	/* Only the four modifiers the protocol names. */
	if ((modifiers & ~MENU_MODIFIERS_ALL) != 0U) {
		error = model_fail(menu, MENU_ERROR_INVALID_VALUE, "an unknown modifier");
		return error;
	}

	/* The shortcut, or none. */
	item = &menu->menu_model->pending[found];
	item->modifiers = modifiers;
	item->keysym = keysym;
	if (keysym == 0U)
		item->modifiers = 0U;

	/* Succeeded: the copy has the shortcut. */
	return 0;
}

/* Queues a menu's protocol error with its code and returns EPROTO. */
static int
model_fail(
	struct zwl_object *menu,
	uint32_t code,
	const char *reason)
{
	int error;

	/* The error names the menu and the xdg_menu_v1 error code. */
	error = zwl_error_code(menu->client, menu->id, code, reason);

	/* Reports the refusal. */
	return error;
}

/* Frees a model: its shown items and any pending copy. */
static void
model_free(
	struct zwl_menu_model *model)
{
	/* No model, nothing to free. */
	if (model == NULL)
		return;

	/* Both arrays with their strings, then the record. */
	items_free(model->items, model->count);
	free(model->items);
	items_free(model->pending, model->pending_count);
	free(model->pending);
	free(model);
}

/* Finds an item of the pending copy by its ID; -1 when there is none (and for the root, 0). */
static int
pending_index(
	const struct zwl_menu_model *model,
	uint32_t id)
{
	unsigned index;

	/* The root is not an item. */
	if (id == ZWL_MENU_ROOT)
		return -1;

	/* A search of at most MENU_ITEMS_MAX items. */
	for (index = 0; index < model->pending_count; index++) {
		/* The item with this ID. */
		if (model->pending[index].id == id)
			return (int)index;
	}

	/* None has it. */
	return -1;
}

/* Tells how deep an item of the pending copy is (the root 0, a top-level item 1). */
static unsigned
pending_depth(
	const struct zwl_menu_model *model,
	uint32_t id)
{
	unsigned depth;
	int found;

	/* Up the parents to the root; the bound keeps a broken chain from looping. */
	depth = 0;
	while (id != ZWL_MENU_ROOT && depth <= MENU_DEPTH_MAX) {
		found = pending_index(model, id);
		if (found < 0)
			break;
		depth++;
		id = model->pending[found].parent;
	}

	/* Succeeded: the depth. */
	return depth;
}

/* Makes room for one more item in the pending copy; ENOMEM when it cannot. */
static int
pending_room(
	struct zwl_menu_model *model)
{
	struct zwl_menu_item *grown;
	unsigned capacity;

	/* There is room already. */
	if (model->pending_count < model->pending_capacity)
		return 0;

	/* The array doubles. */
	capacity = model->pending_capacity * 2U;
	if (capacity < 16U)
		capacity = 16U;
	grown = realloc(model->pending, capacity * sizeof(*grown));
	if (grown == NULL)
		return ENOMEM;

	/* Succeeded: the copy has room. */
	model->pending = grown;
	model->pending_capacity = capacity;
	return 0;
}

/* Copies items with their strings into storage with room for them; ENOMEM when a string cannot be copied. */
static int
items_copy(
	struct zwl_menu_item *to,
	const struct zwl_menu_item *from,
	unsigned count)
{
	unsigned index;

	/* Each item, then its own copies of its strings. */
	for (index = 0; index < count; index++) {
		to[index] = from[index];
		to[index].label = text_copy(from[index].label);
		to[index].icon_name = text_copy(from[index].icon_name);
		if (to[index].label == NULL || to[index].icon_name == NULL) {
			items_free(to, index + 1U);
			return ENOMEM;
		}
	}

	/* Succeeded: the copy owns its strings. */
	return 0;
}

/* Frees the strings of items (not the array they are in). */
static void
items_free(
	struct zwl_menu_item *items,
	unsigned count)
{
	unsigned index;

	/* Each item's label and icon name. */
	for (index = 0; index < count; index++) {
		free(items[index].label);
		free(items[index].icon_name);
		items[index].label = NULL;
		items[index].icon_name = NULL;
	}
}

/* Returns a malloc'ed copy of a string, or NULL. */
static char *
text_copy(
	const char *text)
{
	char *copy;
	size_t length;

	/* The bytes and the terminator. */
	length = strlen(text) + 1U;
	copy = malloc(length);
	if (copy == NULL)
		return NULL;
	memcpy(copy, text, length);

	/* Succeeded: the copy. */
	return copy;
}

/* Finds the ID of a seat the place's client bound (0 when it bound none). */
static uint32_t
place_seat(
	struct zwl_object *place)
{
	struct zwl_object *object;

	/* The first live seat of the client. */
	for (object = place->client->objects; object != NULL; object = object->next) {
		/* Only a live wl_seat. */
		if (object->kind == ZWL_SEAT && !object->dead)
			return object->id;
	}

	/* The client has no seat: the argument is null. */
	return 0;
}
