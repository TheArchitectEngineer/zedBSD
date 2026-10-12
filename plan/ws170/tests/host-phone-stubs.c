/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of Phone's view (host-phone.c) links phone/media.c and the
 * preview's client for the view's drafts and pictures; these stand in for
 * what those reach that the test does not use: an MMS written out, a
 * picture made in this process, and a picture decoded into an image.
 */

#include "userland/desktop/phone/phone.h"
#include "userland/desktop/libmms/mms.h"
#include "userland/desktop/preview/preview.h"

/*
 * Writes no MMS: the test sends none.  Returns -1.
 */
int
mms_write_parts(
	FILE *output,
	const struct mms_part *parts,
	size_t count,
	const char *caption)
{
	(void)output;
	(void)parts;
	(void)count;
	(void)caption;

	/* The test writes no MMS. */
	return -1;
}

/*
 * Makes no picture: the test asks the preview for none.  Returns -1.
 */
int
preview_make(
	int input,
	int output,
	const struct preview_request *request)
{
	(void)input;
	(void)output;
	(void)request;

	/* The test makes no picture. */
	return -1;
}

/*
 * Takes no picture into an image: the test shows no received media.
 */
void
ph_preview_adopt(
	struct preview_picture *picture,
	struct kl_image *image)
{
	(void)picture;
	(void)image;
}
