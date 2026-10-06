/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Mail (WS169 p000): the mock of Keiland's mail application.  Only its
 * face exists: two accounts and their folders and messages are test data
 * in the program (data.c), nothing is stored, and there is no backend --
 * sending, getting mail and the like show that there is none.
 *
 * The view (view.c) draws a frame with libkeiland's canvas and widgets and
 * knows nothing of the window, so that the host tests draw it into
 * pictures; the window (main.c) feeds it the input.
 */

#ifndef MAILER_MAILER_H
#define MAILER_MAILER_H

#include <keiland/keiland.h>

#include <stddef.h>
#include <stdint.h>

/* The folders of an account, in the sidebar's order. */
enum ml_folder {
	ML_INBOX,
	ML_SENT,
	ML_DRAFTS,
	ML_ARCHIVE,
	ML_TRASH,
	ML_FOLDERS
};

/* What a message is (bits). */
#define ML_UNREAD		1U	/* not read yet */
#define ML_ATTACHMENT		2U	/* it carries a file */

/* An account: its name and address. */
struct ml_account {
	const char *name;
	const char *address;
};

/*
 * One message: its account and folder, what it is (ML_*), the sender's
 * name, address and color, to whom, its subject, its date (short for the
 * list, long for the reader), its words (paragraphs split by an empty
 * line), the file it carries ("name" and "type, size", NULL for none), and
 * a code it holds (a sign-in code, NULL for none).
 */
struct ml_message {
	int account;
	enum ml_folder folder;
	unsigned flags;
	const char *from_name;
	const char *from_address;
	kl_color color;
	const char *to;
	const char *subject;
	const char *date_short;
	const char *date_long;
	const char *body;
	const char *file_name;
	const char *file_detail;
	const char *code;
};

/* The actions of the menu, the keys and the buttons. */
#define ML_ACTION_NEW		1U
#define ML_ACTION_SEND		2U
#define ML_ACTION_REPLY		3U
#define ML_ACTION_REPLY_ALL	4U
#define ML_ACTION_FORWARD	5U
#define ML_ACTION_GET		6U
#define ML_ACTION_ARCHIVE	7U
#define ML_ACTION_DELETE	8U
#define ML_ACTION_CANCEL	9U
#define ML_ACTION_QUIT		10U

/* The longest message body written, with its NUL, and the most messages remembered as read. */
#define ML_BODY_MAX		4096U
#define ML_MESSAGES_MAX		64U

/*
 * The view's state.
 *
 * What is shown: the account and folder, the message (-1 for none),
 * whether a narrow window shows the message instead of the list, whether
 * the last frame was narrow, the messages read (a flag each), and the
 * scrolls of the list and of the message.
 *
 * The search, and the message being written: whether it is open, its
 * fields and its body (libkeiland's text area, ws090-p022).
 *
 * The notice shown at the bottom until a time (empty for none), whether
 * the window stands on glass, and whether the program is to end.
 */
struct ml_view {
	int account;
	enum ml_folder folder;
	long selected;
	int opened;
	int narrow;
	unsigned char read[ML_MESSAGES_MAX];
	struct kl_scroll list_scroll;
	struct kl_scroll reader_scroll;

	struct kl_field search;
	int composing;
	struct kl_field to;
	struct kl_field cc;
	struct kl_field subject;
	struct kl_text_area body;

	char notice[128];
	uint64_t notice_until;
	int glass;
	int quit;
};

/* The test data (data.c). */
const struct ml_account *ml_accounts(size_t *count);
const struct ml_message *ml_messages(size_t *count);
const char *ml_folder_name(enum ml_folder folder);

/* The view (view.c). */
int ml_view_init(struct ml_view *view);
void ml_view_release(struct ml_view *view);
void ml_view_action(struct ml_view *view, unsigned action, uint64_t now_us);
void ml_view_key(struct ml_view *view, uint32_t key, unsigned modifiers, uint64_t now_us);
void ml_view_draw(struct ml_view *view, struct kl_ui *ui, const struct kl_style *style, int width, int height, uint64_t now_us);
size_t ml_view_panels(struct ml_view *view, int width, int height, struct kl_glass_panel *panels, size_t capacity);
int ml_view_wait(const struct ml_view *view, uint64_t now_us);

/* The log for the tests (main.c, and the host tests' own). */
void ml_log(const char *format, ...);

#endif
