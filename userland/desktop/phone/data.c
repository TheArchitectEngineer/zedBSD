/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Phone's test data (WS170 p000): the contacts and timelines the mock
 * shows, made up for it.  Every kind of item is here -- messages of each
 * channel in and out, calls answered and missed on the line and over
 * VoIP, pictures and files -- and one contact with nothing yet.
 */

#include "phone.h"

/* Aiko: RCS, a picture, calls on the line. */
static const struct ph_item data_aiko[] = {
	{ PH_TEXT, PH_RCS, 0, "Sat, 3 Oct", "18:12", "Are you coming on the photo walk next week?", NULL },
	{ PH_TEXT, PH_RCS, 1, "Sat, 3 Oct", "18:20", "Yes! I'll bring the prints.", NULL },
	{ PH_CALL, PH_LINE, 1, "Sun, 4 Oct", "11:02", NULL, "12:48" },
	{ PH_TEXT, PH_RCS, 0, "Today", "09:12", "Good morning. Here is the one from the river.", NULL },
	{ PH_PHOTO, PH_RCS, 0, "Today", "09:12", NULL, "IMG_2048.jpg" },
	{ PH_TEXT, PH_RCS, 1, "Today", "09:30", "Beautiful. The light on the water is perfect.", NULL },
	{ PH_TEXT, PH_RCS, 1, "Today", "09:31", "Can I use it for the album cover?", "Read 09:33" },
	{ PH_TEXT, PH_RCS, 0, "Today", "09:41", "Of course, go ahead!", NULL }
};

/* Ben: SMS only, a missed call. */
static const struct ph_item data_ben[] = {
	{ PH_TEXT, PH_SMS, 0, "Yesterday", "20:05", "Running 10 min late, sorry", NULL },
	{ PH_TEXT, PH_SMS, 1, "Yesterday", "20:06", "No problem, we're at the corner table.", "Delivered" },
	{ PH_CALL, PH_LINE, 0, "Today", "08:17", NULL, NULL },
	{ PH_TEXT, PH_SMS, 0, "Today", "08:18", "Call me when you can, it's about Saturday.", NULL }
};

/* Mother: calls both ways and a few words. */
static const struct ph_item data_mother[] = {
	{ PH_CALL, PH_LINE, 0, "Thu, 1 Oct", "19:30", NULL, "24:10" },
	{ PH_TEXT, PH_SMS, 0, "Fri, 2 Oct", "07:45", "\xe3\x81\x8a\xe3\x81\xaf\xe3\x82\x88\xe3\x81\x86\xe3\x80\x82\xe9\x87\x8e\xe8\x8f\x9c\xe3\x82\x92\xe9\x80\x81\xe3\x82\x8a\xe3\x81\xbe\xe3\x81\x97\xe3\x81\x9f\xe3\x80\x82", NULL },
	{ PH_TEXT, PH_SMS, 1, "Fri, 2 Oct", "08:02", "\xe3\x81\x82\xe3\x82\x8a\xe3\x81\x8c\xe3\x81\xa8\xe3\x81\x86\xef\xbc\x81\xe6\x98\x8e\xe6\x97\xa5\xe5\xb1\x8a\xe3\x81\x8f\xe3\x81\xad\xe3\x80\x82", "Delivered" },
	{ PH_CALL, PH_LINE, 1, "Yesterday", "21:10", NULL, "8:02" }
};

/* The clinic: a reminder from a short number, nothing sent. */
static const struct ph_item data_clinic[] = {
	{ PH_TEXT, PH_SMS, 0, "Mon, 28 Sep", "10:00", "Sato Clinic: your appointment is on Wed 7 Oct at 15:30. Reply 1 to confirm.", NULL },
	{ PH_TEXT, PH_SMS, 1, "Mon, 28 Sep", "10:04", "1", "Delivered" },
	{ PH_TEXT, PH_SMS, 0, "Mon, 28 Sep", "10:04", "Thank you, your appointment is confirmed.", NULL }
};

/* Kenji: VoIP calls and a file. */
static const struct ph_item data_kenji[] = {
	{ PH_CALL, PH_VOIP, 0, "Fri, 2 Oct", "14:00", NULL, "41:37" },
	{ PH_FILE, PH_RCS, 0, "Fri, 2 Oct", "14:45", "Quarterly-plan.pdf", "PDF \xc2\xb7 240 KB" },
	{ PH_TEXT, PH_RCS, 0, "Fri, 2 Oct", "14:45", "Here are the slides from the call.", NULL },
	{ PH_TEXT, PH_RCS, 1, "Fri, 2 Oct", "15:20", "Thanks, I'll read them over the weekend.", "Read 15:22" },
	{ PH_CALL, PH_VOIP, 1, "Today", "07:55", NULL, NULL }
};

/* The pizzeria: an MMS picture. */
static const struct ph_item data_pizza[] = {
	{ PH_TEXT, PH_MMS, 1, "Wed, 30 Sep", "18:40", "Two margheritas for pick-up at 19:30, please.", "Delivered" },
	{ PH_PHOTO, PH_MMS, 0, "Wed, 30 Sep", "18:52", "Today's special", "special.jpg" },
	{ PH_TEXT, PH_MMS, 0, "Wed, 30 Sep", "18:52", "Order received. See you at 19:30!", NULL }
};

/* Lena: RCS in and out over several days. */
static const struct ph_item data_lena[] = {
	{ PH_TEXT, PH_RCS, 1, "Tue, 29 Sep", "22:10", "Did the tablet arrive?", "Read 22:11" },
	{ PH_TEXT, PH_RCS, 0, "Tue, 29 Sep", "22:12", "It did! Setting it up now. The handwriting is really smooth.", NULL },
	{ PH_TEXT, PH_RCS, 0, "Tue, 29 Sep", "22:12", "I'll keep it on the kitchen table and leave my phone charging upstairs.", NULL },
	{ PH_TEXT, PH_RCS, 1, "Tue, 29 Sep", "22:15", "That is exactly the idea.", "Read 22:15" }
};

/* The contacts, the latest conversation first. */
static const struct ph_contact data_contacts[] = {
	{ "Aiko Tanaka", "+81 90-1234-5678", "AT", KL_RGB(0xf2994a), 1U, data_aiko, sizeof(data_aiko) / sizeof(data_aiko[0]) },
	{ "Ben Carter", "+1 415-555-0142", "BC", KL_RGB(0x56ccf2), 2U, data_ben, sizeof(data_ben) / sizeof(data_ben[0]) },
	{ "Kenji Watanabe", "sip:kenji@example.net", "KW", KL_RGB(0x6fcf97), 0U, data_kenji, sizeof(data_kenji) / sizeof(data_kenji[0]) },
	{ "\xe3\x81\x8a\xe6\xaf\x8d\xe3\x81\x95\xe3\x82\x93", "+81 3-5555-0101", "\xe6\xaf\x8d", KL_RGB(0xeb5757), 0U, data_mother, sizeof(data_mother) / sizeof(data_mother[0]) },
	{ "Pizza Napoli", "+81 3-5555-0199", "PN", KL_RGB(0xbb6bd9), 0U, data_pizza, sizeof(data_pizza) / sizeof(data_pizza[0]) },
	{ "Lena Fischer", "+49 30 5555 0123", "LF", KL_RGB(0x2d9cdb), 0U, data_lena, sizeof(data_lena) / sizeof(data_lena[0]) },
	{ "Sato Clinic", "50120", "SC", KL_RGB(0x828282), 0U, data_clinic, sizeof(data_clinic) / sizeof(data_clinic[0]) },
	{ "Sam Lee", "+44 20 5555 0175", "SL", KL_RGB(0xf2c94c), 0U, NULL, 0U }
};

/*
 * Reports the contacts and how many there are.
 */
const struct ph_contact *
ph_contacts(
	size_t *count)
{
	/* The array of the program. */
	*count = sizeof(data_contacts) / sizeof(data_contacts[0]);
	return data_contacts;
}
