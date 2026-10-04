/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws131-p015: the host test of the declarative menus' and controls' model
 * (userland/desktop/libkeiland/ui/declare.c): a table becomes the
 * operations a sink records, and only what changed is sent.
 */

#include "declare.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The operations recorded, as text ("A3 L3 ..."), and an operation to refuse. */
static char recorded[4096];
static unsigned refuse_operation;
static uint32_t refuse_id;
static int failures;
static int checks;

static int sink(void *data, unsigned operation, const struct keiui_declare_item *item);
static void expect(const char *name, const char *wanted);
static void expect_int(const char *name, int got, int wanted);

/* A small menu: File (Open, Save, a line, Quit) and View (Grid, a check). */
static const struct kl_menu_entry menu_one[] = {
	{ 1, KL_MENU_ROOT, KL_MENU_ITEM_SUBMENU, "File", 0, KL_MENU_ROLE_NONE, 0, 0 },
	{ 2, 1, KL_MENU_ITEM_NORMAL, "Open...", 100, KL_MENU_ROLE_OPEN, KL_MENU_CTRL, 'o' },
	{ 3, 1, KL_MENU_ITEM_NORMAL, "Save", 101, KL_MENU_ROLE_NONE, 0, 0 },
	{ 4, 1, KL_MENU_ITEM_SEPARATOR, "", 0, KL_MENU_ROLE_NONE, 0, 0 },
	{ 5, 1, KL_MENU_ITEM_NORMAL, "Quit", 102, KL_MENU_ROLE_QUIT, 0, 0 },
	{ 6, KL_MENU_ROOT, KL_MENU_ITEM_SUBMENU, "View", 0, KL_MENU_ROLE_NONE, 0, 0 },
	{ 7, 6, KL_MENU_ITEM_CHECKBOX, "Grid", 103, KL_MENU_ROLE_NONE, 0, 0 }
};

int
main(void)
{
	struct kl_menu_entry menu_two[8];
	struct kl_control_entry controls[2];
	struct keiui_declare_states states;
	struct keiui_declare_model menu;
	struct keiui_declare_model bar;
	const struct keiui_declare_item *found;
	uint32_t action;
	int error;

	/* 1. The first table builds the whole: each item, its role and shortcut. */
	keiui_declare_states_init(&states);
	keiui_declare_init(&menu, KEIUI_DECLARE_MENU);
	recorded[0] = '\0';
	error = keiui_declare_menu(&menu, menu_one, 7, &states, sink, NULL);
	expect_int("first: status", error, 0);
	expect("first", "B A1 A2 R2 S2 A3 A4 A5 R5 A6 A7 C");

	/* 2. The same table again sends nothing, not even a transaction. */
	recorded[0] = '\0';
	error = keiui_declare_menu(&menu, menu_one, 7, &states, sink, NULL);
	expect_int("same: status", error, 0);
	expect("same", "");

	/* 3. Only a label changed. */
	memcpy(menu_two, menu_one, sizeof(menu_one));
	menu_two[2].label = "Save As...";
	recorded[0] = '\0';
	(void)keiui_declare_menu(&menu, menu_two, 7, &states, sink, NULL);
	expect("label", "B L3 C");

	/* 4. An action's state goes to the items of that action only. */
	(void)keiui_declare_set_state(&states, 103, KL_ACTION_CHECKED);
	recorded[0] = '\0';
	(void)keiui_declare_refresh(&menu, &states, sink, NULL);
	expect("state", "B T7=2 C");
	recorded[0] = '\0';
	(void)keiui_declare_refresh(&menu, &states, sink, NULL);
	expect("state again", "");

	/* 5. A shortcut and an action changed: each sent alone. */
	menu_two[2].modifiers = KL_MENU_CTRL;
	menu_two[2].keysym = 's';
	menu_two[4].action = 104;
	recorded[0] = '\0';
	(void)keiui_declare_menu(&menu, menu_two, 7, &states, sink, NULL);
	expect("shortcut and action", "B S3 X5 C");

	/* 6. An item added moves the tree: the top-level items go (with what is under them) and the whole is built again, the state kept. */
	menu_two[7] = menu_two[6];
	menu_two[7].id = 8;
	menu_two[7].label = "List";
	menu_two[7].action = 105;
	recorded[0] = '\0';
	(void)keiui_declare_menu(&menu, menu_two, 8, &states, sink, NULL);
	expect("structure", "B D1 D6 A1 A2 R2 S2 A3 S3 A4 A5 R5 A6 A7 T7=2 A8 C");

	/* 7. A refusal still ends the transaction, and the next table is built again. */
	refuse_operation = KEIUI_DECLARE_LABEL;
	refuse_id = 2;
	menu_two[1].label = "Open File...";
	recorded[0] = '\0';
	error = keiui_declare_menu(&menu, menu_two, 8, &states, sink, NULL);
	expect_int("refusal: status", error, EIO);
	expect("refusal", "B L2! C");
	refuse_operation = 0;
	recorded[0] = '\0';
	(void)keiui_declare_menu(&menu, menu_two, 8, &states, sink, NULL);
	expect("after refusal", "B D1 D6 A1 A2 R2 S2 A3 S3 A4 A5 R5 A6 A7 T7=2 A8 C");

	/* 8. The controls: a new table, a label, an action (nothing sent, the model knows it), a role (built again), a state. */
	keiui_declare_init(&bar, KEIUI_DECLARE_CONTROLS);
	controls[0].id = 10;
	controls[0].role = KL_CONTROL_BACK;
	controls[0].priority = KL_PRIORITY_PRIMARY;
	controls[0].group = 0;
	controls[0].label = "Back";
	controls[0].action = 200;
	controls[1].id = 11;
	controls[1].role = KL_CONTROL_SEARCH;
	controls[1].priority = KL_PRIORITY_NORMAL;
	controls[1].group = 1;
	controls[1].label = "Search";
	controls[1].action = 201;
	recorded[0] = '\0';
	(void)keiui_declare_controls(&bar, controls, 2, &states, sink, NULL);
	expect("controls", "B A10 A11 C");
	controls[1].label = "Find";
	controls[0].action = 202;
	recorded[0] = '\0';
	(void)keiui_declare_controls(&bar, controls, 2, &states, sink, NULL);
	expect("controls: label and action", "B L11 C");
	found = keiui_declare_find(&bar, 10);
	action = 0;
	if (found != NULL)
		action = found->action;
	expect_int("controls: action kept", (int)action, 202);
	controls[0].role = KL_CONTROL_HOME;
	recorded[0] = '\0';
	(void)keiui_declare_controls(&bar, controls, 2, &states, sink, NULL);
	expect("controls: role", "B D10 D11 A10 A11 C");
	(void)keiui_declare_set_state(&states, 201, KL_ACTION_DISABLED);
	recorded[0] = '\0';
	(void)keiui_declare_refresh(&bar, &states, sink, NULL);
	expect("controls: state", "B T11=1 C");

	/* 9. Count 0 takes every item away; the action states' table. */
	recorded[0] = '\0';
	(void)keiui_declare_menu(&menu, NULL, 0, &states, sink, NULL);
	expect("empty", "B D1 D6 C");
	expect_int("state: action 0", keiui_declare_set_state(&states, 0, 0), EINVAL);
	for (action = 1000; action < 1000 + KEIUI_DECLARE_ACTIONS; action++)
		(void)keiui_declare_set_state(&states, action, 0);
	expect_int("state: full", keiui_declare_set_state(&states, 5000, 0), ENOSPC);
	expect_int("state: unknown", (int)keiui_declare_state_of(&states, 7777), 0);
	expect_int("state: known", (int)keiui_declare_state_of(&states, 201), KL_ACTION_DISABLED);

	/* The end. */
	keiui_declare_fini(&menu);
	keiui_declare_fini(&bar);
	printf("host-declare: %d/%d passed\n", checks - failures, checks);
	return failures != 0;
}

/* Records an operation (and refuses the one asked for). */
static int
sink(
	void *data,
	unsigned operation,
	const struct keiui_declare_item *item)
{
	static const char letters[] = "?BDALXRSTC";
	char word[32];
	int refused;

	/* The operation's letter, the item's ID, and a state's bits. */
	(void)data;
	refused = item != NULL && operation == refuse_operation && item->id == refuse_id;
	if (item == NULL)
		snprintf(word, sizeof(word), "%c", letters[operation]);
	else if (operation == KEIUI_DECLARE_STATE)
		snprintf(word, sizeof(word), "%c%u=%u", letters[operation], (unsigned)item->id, item->state);
	else
		snprintf(word, sizeof(word), "%c%u%s", letters[operation], (unsigned)item->id, refused ? "!" : "");
	if (recorded[0] != '\0')
		strncat(recorded, " ", sizeof(recorded) - strlen(recorded) - 1U);
	strncat(recorded, word, sizeof(recorded) - strlen(recorded) - 1U);

	/* Refused, or carried out. */
	if (refused)
		return EIO;
	return 0;
}

/* Checks the operations recorded. */
static void
expect(
	const char *name,
	const char *wanted)
{
	int same;

	checks++;
	same = strcmp(recorded, wanted);
	if (same == 0)
		return;
	failures++;
	printf("FAIL %s: '%s', not '%s'\n", name, recorded, wanted);
}

/* Checks a number. */
static void
expect_int(
	const char *name,
	int got,
	int wanted)
{
	checks++;
	if (got == wanted)
		return;
	failures++;
	printf("FAIL %s: %d, not %d\n", name, got, wanted);
}
