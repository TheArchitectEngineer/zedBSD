/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws210-p014 F1: the host test of the lighting's part in libkeiland's
 * widgets' input (ui/ui.c).  A sidebar of rows (kl_sidebar_item) and a
 * still picture (kl_ui_hit_still) are drawn; the pointer then crosses
 * several rows before the next frame, and the part kl_ui_take_damage
 * gives is checked to hold only the row lit when the frame shown was
 * drawn and the row lit now.  Each part drawn alone, clipped, must leave
 * the same pixels as the whole frame drawn again.
 *
 *     host-kl-ui-lit FONT FALLBACK
 *
 * F3: kl_ui_take_frame gives the two rows as two parts, not the rows
 * between, and parts the application adds (kl_ui_damage); each part is
 * drawn clipped to it in turn and the frame must again equal the whole.
 *
 * Prints "PASS name" or "FAIL name ..." for each check; exits with 1 when
 * one failed.
 */

#include <keiland/keiland.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The window's size. */
#define TEST_WIDTH		480
#define TEST_HEIGHT		400

/* The sidebar's rows: how many, where they start and how tall each is. */
#define TEST_ROWS		10
#define TEST_ROW_X		10
#define TEST_ROW_Y		10
#define TEST_ROW_WIDTH		200
#define TEST_ROW_HEIGHT		30

/* The ids of the widgets. */
#define TEST_ID_ROW		1U
#define TEST_ID_PICTURE		2U

/*
 * One frame's drawing surface and what draws on it, kept together for
 * the checks.
 */
struct test_scene {
	struct kl_canvas canvas;
	struct kl_style style;
	struct kl_text text;
	struct kl_ui *ui;
	uint32_t *pixels;
	uint32_t *whole;
	uint64_t now;
};

/* How many checks failed so far. */
static int test_failures;

int main(int argc, char **argv);
static void test_draw(struct test_scene *scene);
static void test_frame(struct test_scene *scene, const struct kl_rect *part);
static void test_frame_parts(struct test_scene *scene, const struct kl_rect *parts, unsigned count);
static void test_take_frame(struct test_scene *scene);
static int test_take(struct test_scene *scene, struct kl_rect *part);
static void test_row(int index, struct kl_rect *row);
static int test_contains(const struct kl_rect *outer, const struct kl_rect *inner);
static void test_same_as_whole(struct test_scene *scene, const char *name);
static void test_check(int passed, const char *name, const struct kl_rect *part);

/*
 * Runs the checks.
 */
int
main(
	int argc,
	char **argv)
{
	struct test_scene scene;
	struct kl_rect part;
	struct kl_rect first;
	struct kl_rect last;
	struct kl_rect between;
	int taken;
	int passed;
	int changed;
	int index;
	int error;

	/* The fonts. */
	if (argc != 3) {
		fprintf(stderr, "usage: host-kl-ui-lit FONT FALLBACK\n");
		return 2;
	}
	memset(&scene, 0, sizeof(scene));
	error = kl_text_open(&scene.text, argv[1], argv[2]);
	if (error != 0) {
		fprintf(stderr, "host-kl-ui-lit: fonts error=%d\n", error);
		return 2;
	}

	/* The frame, its copy drawn whole, and the canvas. */
	scene.pixels = calloc((size_t)TEST_WIDTH * TEST_HEIGHT, sizeof(scene.pixels[0]));
	if (scene.pixels == NULL)
		return 2;
	scene.whole = calloc((size_t)TEST_WIDTH * TEST_HEIGHT, sizeof(scene.whole[0]));
	if (scene.whole == NULL)
		return 2;
	error = kl_canvas_init(&scene.canvas, scene.pixels, TEST_WIDTH, TEST_WIDTH, TEST_HEIGHT);
	if (error != 0)
		return 2;

	/* The input and the style. */
	scene.ui = kl_ui_create();
	if (scene.ui == NULL)
		return 2;
	scene.style.canvas = &scene.canvas;
	scene.style.text = &scene.text;
	scene.style.theme = kl_theme_default();
	scene.style.glass = 0;

	/* The first frames, with the pointer on the first row. */
	(void)kl_ui_pointer_motion(scene.ui, TEST_ROW_X + 20, TEST_ROW_Y + 15);
	test_frame(&scene, NULL);
	(void)kl_ui_take_damage(scene.ui, &part);
	test_frame(&scene, NULL);

	/* The pointer crosses every row down to the last before a frame: the part holds the first and the last rows only. */
	for (index = 1; index < TEST_ROWS; index++)
		(void)kl_ui_pointer_motion(scene.ui, TEST_ROW_X + 20, TEST_ROW_Y + index * TEST_ROW_HEIGHT + 15);
	taken = test_take(&scene, &part);
	test_row(0, &first);
	test_row(TEST_ROWS - 1, &last);
	passed = 0;
	if (taken && test_contains(&part, &first) && test_contains(&part, &last))
		passed = 1;
	test_check(passed, "trail-ends", &part);
	test_frame(&scene, &part);
	test_same_as_whole(&scene, "trail-pixels");

	/* Down and back up to the row lit when the frame was drawn: one pixel, not the rows crossed. */
	for (index = TEST_ROWS - 2; index >= 4; index--)
		(void)kl_ui_pointer_motion(scene.ui, TEST_ROW_X + 20, TEST_ROW_Y + index * TEST_ROW_HEIGHT + 15);
	(void)kl_ui_pointer_motion(scene.ui, TEST_ROW_X + 20, TEST_ROW_Y + (TEST_ROWS - 1) * TEST_ROW_HEIGHT + 15);
	taken = test_take(&scene, &part);
	passed = 0;
	if (taken && part.width * part.height <= 1)
		passed = 1;
	test_check(passed, "back-to-drawn", &part);
	test_frame(&scene, &part);
	test_same_as_whole(&scene, "back-pixels");

	/* Out of the sidebar over the still picture and into the third row: the last and the third rows, not the picture. */
	(void)kl_ui_pointer_motion(scene.ui, 350, 300);
	(void)kl_ui_pointer_motion(scene.ui, TEST_ROW_X + 20, TEST_ROW_Y + 2 * TEST_ROW_HEIGHT + 15);
	taken = test_take(&scene, &part);
	test_row(2, &first);
	test_row(TEST_ROWS - 1, &last);
	passed = 0;
	if (taken &&
	    test_contains(&part, &first) &&
	    test_contains(&part, &last) &&
	    part.x + part.width <= 300)
		passed = 1;
	test_check(passed, "past-still", &part);
	test_frame(&scene, &part);
	test_same_as_whole(&scene, "past-still-pixels");

	/* Two rows apart, the third and the fifth, by way of the fourth: the part is the third to the fifth, not more. */
	(void)kl_ui_pointer_motion(scene.ui, TEST_ROW_X + 20, TEST_ROW_Y + 3 * TEST_ROW_HEIGHT + 15);
	(void)kl_ui_pointer_motion(scene.ui, TEST_ROW_X + 20, TEST_ROW_Y + 4 * TEST_ROW_HEIGHT + 15);
	taken = test_take(&scene, &part);
	test_row(2, &first);
	test_row(4, &last);
	between.x = first.x - 16;
	between.y = first.y - 16;
	between.width = first.width + 32;
	between.height = last.y + last.height - first.y + 32;
	passed = 0;
	if (taken && test_contains(&between, &part) && test_contains(&part, &first) && test_contains(&part, &last))
		passed = 1;
	test_check(passed, "two-rows", &part);
	test_frame(&scene, &part);
	test_same_as_whole(&scene, "two-rows-pixels");

	/* By way of the far widget to the seventh row: the fifth and the seventh rows, not the far widget passed over. */
	(void)kl_ui_pointer_motion(scene.ui, 300, 25);
	(void)kl_ui_pointer_motion(scene.ui, TEST_ROW_X + 20, TEST_ROW_Y + 6 * TEST_ROW_HEIGHT + 15);
	taken = test_take(&scene, &part);
	test_row(4, &first);
	test_row(6, &last);
	passed = 0;
	if (taken &&
	    test_contains(&part, &first) &&
	    test_contains(&part, &last) &&
	    part.x + part.width <= 250)
		passed = 1;
	test_check(passed, "detour", &part);
	test_frame(&scene, &part);
	test_same_as_whole(&scene, "detour-pixels");

	/* The pointer leaves: the row lit when drawn goes dark alone. */
	changed = kl_ui_pointer_leave(scene.ui);
	taken = test_take(&scene, &part);
	passed = 0;
	if (changed && taken && test_contains(&part, &last) && part.height < 3 * TEST_ROW_HEIGHT)
		passed = 1;
	test_check(passed, "leave", &part);
	test_frame(&scene, &part);
	test_same_as_whole(&scene, "leave-pixels");

	/* Over the still picture from outside: nothing to draw. */
	changed = kl_ui_pointer_motion(scene.ui, 350, 300);
	taken = test_take(&scene, &part);
	passed = 0;
	if (!changed && !taken)
		passed = 1;
	test_check(passed, "still-only", &part);

	/* The parts of a frame (F3). */
	test_take_frame(&scene);

	/* The result. */
	kl_ui_destroy(scene.ui);
	kl_text_close(&scene.text);
	free(scene.pixels);
	free(scene.whole);
	if (test_failures != 0)
		return 1;

	/* Succeeded: every check passed. */
	return 0;
}

/* Draws the scene: the ground, the sidebar's rows and the still picture. */
static void
test_draw(
	struct test_scene *scene)
{
	struct kl_rect ground;
	struct kl_rect row;
	struct kl_rect picture;
	char label[32];
	int index;

	/* The ground. */
	ground.x = 0;
	ground.y = 0;
	ground.width = TEST_WIDTH;
	ground.height = TEST_HEIGHT;
	kl_canvas_fill(&scene->canvas, &ground, KL_RGBA(0xf4f6fa, 0xff));

	/* The rows, the first chosen. */
	for (index = 0; index < TEST_ROWS; index++) {
		test_row(index, &row);
		(void)snprintf(label, sizeof(label), "Row %d", index);
		(void)kl_sidebar_item(scene->ui, &scene->style, TEST_ID_ROW, (uint32_t)index, &row, KL_ICON_TODAY, label, index == 0);
	}

	/* A widget far to the right of the rows, lit under the pointer. */
	row.x = 260;
	row.y = 10;
	row.width = 200;
	row.height = TEST_ROW_HEIGHT;
	(void)kl_sidebar_item(scene->ui, &scene->style, TEST_ID_ROW, 100U, &row, KL_ICON_TODAY, "Far", 0);

	/* The still picture: a press may drag it, it draws the same lit. */
	picture.x = 250;
	picture.y = 200;
	picture.width = 200;
	picture.height = 180;
	kl_canvas_round(&scene->canvas, (float)picture.x, (float)picture.y, (float)picture.width, (float)picture.height, 8.0f, KL_RGBA(0x3366aa, 0xff));
	(void)kl_ui_hit_still(scene->ui, TEST_ID_PICTURE, 0U, &picture);
}

/* Draws one frame, all of it or clipped to a part. */
static void
test_frame(
	struct test_scene *scene,
	const struct kl_rect *part)
{
	/* The frame, within the part when there is one. */
	scene->now += 16000U;
	if (part != NULL)
		kl_canvas_clip_push(&scene->canvas, part);
	kl_ui_begin(scene->ui, scene->now);
	test_draw(scene);
	(void)kl_ui_end(scene->ui, scene->now);
	if (part != NULL)
		kl_canvas_clip_pop(&scene->canvas);
}

/* Draws one frame clipped to each of some parts in turn, the input's frame begun once for each. */
static void
test_frame_parts(
	struct test_scene *scene,
	const struct kl_rect *parts,
	unsigned count)
{
	unsigned index;

	/* Each part, as an application draws a frame of several parts. */
	for (index = 0; index < count; index++)
		test_frame(scene, &parts[index]);
}

/*
 * Checks kl_ui_take_frame: the rows crossed between two frames are not
 * drawn, only the row lit when drawn and the one lit now, as two parts;
 * the application's parts join them; nothing is drawn when the lighting
 * came back; the whole window when asked.
 */
static void
test_take_frame(
	struct test_scene *scene)
{
	struct kl_rect parts[KL_FRAME_PARTS_MAX];
	struct kl_rect first;
	struct kl_rect last;
	struct kl_rect added;
	unsigned count;
	unsigned index;
	int need;
	int passed;
	int found_first;
	int found_last;

	/* A frame with the pointer on the first row. */
	(void)kl_ui_pointer_motion(scene->ui, TEST_ROW_X + 20, TEST_ROW_Y + 15);
	test_frame(scene, NULL);
	(void)kl_ui_take_frame(scene->ui, parts, KL_FRAME_PARTS_MAX, &count);

	/* Down every row to the last before a frame: two parts, the first and the last rows, nothing between. */
	for (index = 1; index < TEST_ROWS; index++)
		(void)kl_ui_pointer_motion(scene->ui, TEST_ROW_X + 20, TEST_ROW_Y + (int)index * TEST_ROW_HEIGHT + 15);
	need = kl_ui_take_frame(scene->ui, parts, KL_FRAME_PARTS_MAX, &count);
	test_row(0, &first);
	test_row(TEST_ROWS - 1, &last);
	found_first = 0;
	found_last = 0;
	for (index = 0; index < count; index++) {
		if (test_contains(&parts[index], &first) && parts[index].height < 2 * TEST_ROW_HEIGHT)
			found_first = 1;
		if (test_contains(&parts[index], &last) && parts[index].height < 2 * TEST_ROW_HEIGHT)
			found_last = 1;
	}
	passed = 0;
	if (need == KL_FRAME_PARTS && count == 2U && found_first && found_last)
		passed = 1;
	test_check(passed, "frame-two-parts", &parts[0]);
	test_frame_parts(scene, parts, count);
	test_same_as_whole(scene, "frame-two-parts-pixels");

	/* The application's part far from the lighting's joins it as a part of its own. */
	(void)kl_ui_pointer_motion(scene->ui, TEST_ROW_X + 20, TEST_ROW_Y + 5 * TEST_ROW_HEIGHT + 15);
	added.x = 300;
	added.y = 40;
	added.width = 60;
	added.height = 20;
	kl_ui_damage(scene->ui, &added);
	need = kl_ui_take_frame(scene->ui, parts, KL_FRAME_PARTS_MAX, &count);
	passed = 0;
	if (need == KL_FRAME_PARTS && count == 3U)
		passed = 1;
	test_check(passed, "frame-added", &parts[0]);
	test_frame_parts(scene, parts, count);
	test_same_as_whole(scene, "frame-added-pixels");

	/* Away and back to the row lit when drawn: nothing to draw. */
	(void)kl_ui_pointer_motion(scene->ui, TEST_ROW_X + 20, TEST_ROW_Y + 7 * TEST_ROW_HEIGHT + 15);
	(void)kl_ui_pointer_motion(scene->ui, TEST_ROW_X + 20, TEST_ROW_Y + 5 * TEST_ROW_HEIGHT + 15);
	need = kl_ui_take_frame(scene->ui, parts, KL_FRAME_PARTS_MAX, &count);
	passed = 0;
	if (need == KL_FRAME_NONE && count == 0U)
		passed = 1;
	test_check(passed, "frame-none", NULL);

	/* Fewer places than parts: the parts beyond merged into the last one given. */
	(void)kl_ui_pointer_motion(scene->ui, TEST_ROW_X + 20, TEST_ROW_Y + 9 * TEST_ROW_HEIGHT + 15);
	need = kl_ui_take_frame(scene->ui, parts, 1U, &count);
	test_row(5, &first);
	test_row(9, &last);
	passed = 0;
	if (need == KL_FRAME_PARTS && count == 1U && test_contains(&parts[0], &first) && test_contains(&parts[0], &last))
		passed = 1;
	test_check(passed, "frame-capacity", &parts[0]);
	test_frame_parts(scene, parts, count);
	test_same_as_whole(scene, "frame-capacity-pixels");

	/* The whole window asked for. */
	kl_ui_damage_whole(scene->ui);
	need = kl_ui_take_frame(scene->ui, parts, KL_FRAME_PARTS_MAX, &count);
	passed = 0;
	if (need == KL_FRAME_WHOLE && count == 0U)
		passed = 1;
	test_check(passed, "frame-whole", NULL);
}

/* Takes the part the lighting needs drawn again; 0 when there is none (or the whole window). */
static int
test_take(
	struct test_scene *scene,
	struct kl_rect *part)
{
	int taken;

	/* The part, cleared first so that a failure shows none. */
	memset(part, 0, sizeof(*part));
	taken = kl_ui_take_damage(scene->ui, part);

	/* Reports whether there is a part. */
	return taken;
}

/* Gives the rectangle of one of the sidebar's rows. */
static void
test_row(
	int index,
	struct kl_rect *row)
{
	/* One below the other. */
	row->x = TEST_ROW_X;
	row->y = TEST_ROW_Y + index * TEST_ROW_HEIGHT;
	row->width = TEST_ROW_WIDTH;
	row->height = TEST_ROW_HEIGHT;
}

/* Tells whether a rectangle holds another wholly (1) or not (0). */
static int
test_contains(
	const struct kl_rect *outer,
	const struct kl_rect *inner)
{
	/* Each edge of the inner one inside the outer one. */
	if (inner->x < outer->x)
		return 0;
	if (inner->y < outer->y)
		return 0;
	if (inner->x + inner->width > outer->x + outer->width)
		return 0;
	if (inner->y + inner->height > outer->y + outer->height)
		return 0;

	/* Reports that it holds it. */
	return 1;
}

/*
 * Checks that the frame drawn by parts is the frame drawn whole: the
 * pixels are kept, the whole frame drawn again, compared and put back.
 */
static void
test_same_as_whole(
	struct test_scene *scene,
	const char *name)
{
	size_t bytes;
	int differs;

	/* The frame drawn by parts, kept; the whole frame drawn over it. */
	bytes = (size_t)TEST_WIDTH * TEST_HEIGHT * sizeof(scene->pixels[0]);
	memcpy(scene->whole, scene->pixels, bytes);
	test_frame(scene, NULL);
	differs = memcmp(scene->whole, scene->pixels, bytes);

	/* The result. */
	test_check(differs == 0, name, NULL);
}

/* Prints a check's result, with the part when there is one. */
static void
test_check(
	int passed,
	const char *name,
	const struct kl_rect *part)
{
	/* A failure is counted and shows the part. */
	if (!passed) {
		test_failures++;
		if (part != NULL)
			printf("FAIL %s part=%d,%d %dx%d\n", name, part->x, part->y, part->width, part->height);
		else
			printf("FAIL %s\n", name);
		return;
	}

	/* A pass. */
	if (part != NULL)
		printf("PASS %s part=%d,%d %dx%d\n", name, part->x, part->y, part->width, part->height);
	else
		printf("PASS %s\n", name);
}

/* The appearance libkeiland's theme asks for: the light one (the host test has no compositor to ask). */
unsigned
kl_appearance_get(
	const struct kl_appearance *appearance)
{
	/* The light appearance. */
	(void)appearance;
	return KL_APPEARANCE_LIGHT;
}
