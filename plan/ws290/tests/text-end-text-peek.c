/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The PC/AT text layer (src/drivers/platform/pcat/graphics/text.c) built
 * whole, with readers of the state a scene checks but no call shows: the
 * pointer the layer draws through, its end mark, and the result of the end
 * it asks for itself when KERN_TEST_TEXT_END_SECONDS is defined.
 */

#include "../../../src/drivers/platform/pcat/graphics/text.c"

#include "text-end-stubs.h"

/* Reports the pointer the layer draws through. */
const volatile uint32_t *
text_end_peek_pixels(void)
{
	/* The pointer itself. */
	return text_pixels;
}

/* Reports whether the layer has left the display. */
int
text_end_peek_ended(void)
{
	/* The mark, as a fatal stop reads it. */
	return __atomic_load_n(&text_ended, __ATOMIC_ACQUIRE);
}

/* Reports whether the layer's own end has returned (0 without it). */
int
text_end_peek_test_done(void)
{
#ifdef KERN_TEST_TEXT_END_SECONDS
	/* The mark the thread sets. */
	return text_end_test_done;
#else
	/* No such end in this build. */
	return 0;
#endif
}

/* Reports what the layer's own end returned (-1 without it). */
int
text_end_peek_test_result(void)
{
#ifdef KERN_TEST_TEXT_END_SECONDS
	/* The result the thread kept. */
	return text_end_test_result;
#else
	/* No such end in this build. */
	return -1;
#endif
}
