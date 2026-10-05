/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Mail's test data (WS169 p000): two accounts and their messages, made up
 * for the mock -- read and unread ones, one with a file, one with a
 * sign-in code (what the browser is to fill in from a mail later), a sent
 * one and a draft.
 */

#include "mailer.h"

/* The accounts. */
static const struct ml_account data_accounts[] = {
	{ "Personal", "kei@example.net" },
	{ "Work", "kei@corp.example" }
};

/* The folders' names. */
static const char *const data_folders[ML_FOLDERS] = { "Inbox", "Sent", "Drafts", "Archive", "Trash" };

/* The messages, the latest first in each folder. */
static const struct ml_message data_messages[] = {
	{ 0, ML_INBOX, ML_UNREAD, "Example Bank", "no-reply@bank.example", KL_RGB(0x2d9cdb), "kei@example.net",
	  "Your sign-in code", "09:41", "Monday, 5 October 2026 at 09:41",
	  "Hello Kei,\n\nUse this code to finish signing in to Example Bank:\n\n482913\n\nThe code expires in 10 minutes. If you did not try to sign in, you can ignore this message; nobody can sign in without the code.\n\nExample Bank",
	  NULL, NULL, "482913" },
	{ 0, ML_INBOX, ML_UNREAD | ML_ATTACHMENT, "Aiko Tanaka", "aiko@example.org", KL_RGB(0xf2994a), "kei@example.net",
	  "Photos from the river walk", "08:12", "Monday, 5 October 2026 at 08:12",
	  "Hi Kei,\n\nHere are the photos from Sunday. The one at the bridge came out really well - I think it would make a nice cover for the album.\n\nShall we go again next month when the leaves turn?\n\nAiko",
	  "river-walk.zip", "ZIP archive, 18.4 MB", NULL },
	{ 0, ML_INBOX, 0U, "Ben Carter", "ben@example.com", KL_RGB(0x56ccf2), "kei@example.net",
	  "Saturday?", "Yesterday", "Sunday, 4 October 2026 at 20:30",
	  "Hey,\n\nAre we still on for Saturday? I booked the corner table at 19:00. Let me know if that works, or call me.\n\nBen",
	  NULL, NULL, NULL },
	{ 0, ML_INBOX, 0U, "Keiland Updates", "news@keiland.example", KL_RGB(0x6fcf97), "kei@example.net",
	  "What's new in Keiland this month", "Sat", "Saturday, 3 October 2026 at 10:00",
	  "This month: a calendar with a desk calendar in 3D, a phone application that keeps your messages and calls in one timeline, and mail.\n\nThe tablet now remembers the input method of each application.\n\nThank you for trying Keiland.",
	  NULL, NULL, NULL },
	{ 0, ML_INBOX, ML_ATTACHMENT, "Sato Clinic", "info@clinic.example", KL_RGB(0x828282), "kei@example.net",
	  "Appointment confirmation", "Mon 28 Sep", "Monday, 28 September 2026 at 10:04",
	  "Dear Kei,\n\nYour appointment is confirmed for Wednesday 7 October at 15:30. Please bring your insurance card.\n\nThe map to the clinic is attached.\n\nSato Clinic",
	  "map.pdf", "PDF document, 240 KB", NULL },
	{ 0, ML_INBOX, 0U, "\xe3\x81\x8a\xe6\xaf\x8d\xe3\x81\x95\xe3\x82\x93", "mother@example.jp", KL_RGB(0xeb5757), "kei@example.net",
	  "\xe9\x87\x8e\xe8\x8f\x9c\xe3\x82\x92\xe9\x80\x81\xe3\x82\x8a\xe3\x81\xbe\xe3\x81\x97\xe3\x81\x9f", "Fri 2 Oct", "Friday, 2 October 2026 at 07:45",
	  "\xe3\x81\x8a\xe3\x81\xaf\xe3\x82\x88\xe3\x81\x86\xe3\x80\x82\n\n\xe7\x95\x91\xe3\x81\xae\xe9\x87\x8e\xe8\x8f\x9c\xe3\x82\x92\xe9\x80\x81\xe3\x82\x8a\xe3\x81\xbe\xe3\x81\x97\xe3\x81\x9f\xe3\x80\x82\xe6\x98\x8e\xe6\x97\xa5\xe5\xb1\x8a\xe3\x81\x8f\xe3\x81\xaf\xe3\x81\x9a\xe3\x81\xa7\xe3\x81\x99\xe3\x80\x82",
	  NULL, NULL, NULL },
	{ 0, ML_SENT, 0U, "Kei", "kei@example.net", KL_RGB(0x2f7cf6), "ben@example.com",
	  "Re: Saturday?", "Yesterday", "Sunday, 4 October 2026 at 20:41",
	  "Saturday at 19:00 is perfect. See you there!\n\nKei",
	  NULL, NULL, NULL },
	{ 0, ML_DRAFTS, 0U, "Kei", "kei@example.net", KL_RGB(0x2f7cf6), "aiko@example.org",
	  "Album cover", "08:30", "Monday, 5 October 2026 at 08:30",
	  "Hi Aiko,\n\nThank you for the photos! I would love to use the bridge one for",
	  NULL, NULL, NULL },
	{ 1, ML_INBOX, ML_UNREAD, "Kenji Watanabe", "kenji@corp.example", KL_RGB(0x6fcf97), "kei@corp.example",
	  "Quarterly plan - comments by Thursday", "10:02", "Monday, 5 October 2026 at 10:02",
	  "Hi Kei,\n\nThe draft of the quarterly plan is in the shared folder. Could you add your comments by Thursday? The section on the release dates needs your eyes most.\n\nThanks,\nKenji",
	  "Quarterly-plan.pdf", "PDF document, 1.2 MB", NULL },
	{ 1, ML_INBOX, 0U, "Lena Fischer", "lena@corp.example", KL_RGB(0x2d9cdb), "kei@corp.example",
	  "Tablet field test", "Fri 2 Oct", "Friday, 2 October 2026 at 22:15",
	  "The tablet arrived and I set it up on the kitchen table. The handwriting is really smooth, and leaving the phone upstairs while the tablet takes the messages works well.\n\nI will send a fuller report next week.\n\nLena",
	  NULL, NULL, NULL },
	{ 1, ML_INBOX, 0U, "IT Desk", "it@corp.example", KL_RGB(0xbb6bd9), "kei@corp.example",
	  "Maintenance on Saturday", "Thu 1 Oct", "Thursday, 1 October 2026 at 16:00",
	  "The mail servers will be down for maintenance on Saturday from 01:00 to 03:00.\n\nNo action is needed.",
	  NULL, NULL, NULL },
	{ 1, ML_ARCHIVE, 0U, "Kenji Watanabe", "kenji@corp.example", KL_RGB(0x6fcf97), "kei@corp.example",
	  "Kick-off notes", "Sep 3", "Thursday, 3 September 2026 at 11:30",
	  "Notes from the kick-off are attached to the calendar invitation.",
	  NULL, NULL, NULL }
};

/*
 * Reports the accounts and how many there are.
 */
const struct ml_account *
ml_accounts(
	size_t *count)
{
	/* The array of the program. */
	*count = sizeof(data_accounts) / sizeof(data_accounts[0]);
	return data_accounts;
}

/*
 * Reports the messages and how many there are.
 */
const struct ml_message *
ml_messages(
	size_t *count)
{
	/* The array of the program. */
	*count = sizeof(data_messages) / sizeof(data_messages[0]);
	return data_messages;
}

/*
 * Reports a folder's name.
 */
const char *
ml_folder_name(
	enum ml_folder folder)
{
	/* A folder not known. */
	if ((unsigned)folder >= ML_FOLDERS)
		return "";

	/* Its name. */
	return data_folders[folder];
}
