/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * An output's angle changed while it is shown (WS217, ws217-p002b; the
 * design is plan/ws217/phase002b/phase.md, section 3).
 *
 * Two steps are kept apart (review-4 S12): the user's choice of an angle
 * (kwl_output_select_rotation: the display's mask, the choice and
 * displays.conf), and the change of the angle shown
 * (kwl_output_apply_rotation: how it is shown and what follows), which a
 * game's pause of the turn (ws217-p012) also takes without touching the
 * choice.  An opening of the output shows the choice by itself
 * (compose_rotation_choose, kwl_output_rotated).
 *
 * The turn between angles the compositor turns exchanges the images of
 * the logical size: the new turn image and the look's wallpaper are made
 * first, so that a failure changes nothing; then, with the render thread
 * parked and the device idle, the old ones go and the optional caches (the
 * scene's copy, the layers' pictures, the backdrop) are made again at the
 * new size.  A change of the scanout's angle (the display's hardware, 180)
 * opens the output again (ws217-p010).
 */

#include "kwl.h"
#include "compose.h"
#include "scanout-rules.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static int output_rotate_turn(struct kwl_server *server, const struct kwl_rotate_state *after);
static const char *output_rotate_heads(const struct kwl_compose *compose);
static void output_rotate_caches(struct kwl_server *server, unsigned scene_kept, unsigned resized);
static const char *output_rotate_mode_name(unsigned mode);

/*
 * Takes the user's choice of an output's angle: refused outside the
 * display's mask; saved, and shown now when the output is open (or at its
 * next opening).  *applied says whether the angle is shown now, *saved
 * whether displays.conf was written (0) or its errno.  Returns 0, EINVAL
 * for a number that is not an angle, ENOTSUP for an angle the display may
 * not take (or a head's, ws217-p005), or the error of showing it (the
 * choice then goes back to what it was).
 */
int
kwl_output_select_rotation(
	struct kwl_server *server,
	unsigned slot,
	unsigned rotation,
	unsigned *applied,
	int *saved)
{
	struct kwl_compose *compose;
	unsigned previous;
	unsigned decision;
	unsigned open;
	unsigned scanning_out;
	unsigned stored;
	const char *heads;
	int valid;
	int error;

	/* Nothing applied or written yet. */
	*applied = 0U;
	*saved = -1;
	compose = server->compose;

	/* An angle, of the anchor (a head's is ws217-p005), outside the login screen (D9). */
	valid = kwl_rotate_valid(rotation);
	if (!valid)
		return EINVAL;
	if (compose == NULL || slot != KWL_PLANE_ANCHOR || server->greeter)
		return ENOTSUP;

	/* What the choice does, against the mask, the choice and the angle shown now. */
	open = 0U;
	if (compose->output_open && !compose->output_lost)
		open = 1U;
	scanning_out = 0U;
	if (server->scanout != NULL)
		scanning_out = 1U;
	decision = kwl_rotate_select(rotation, compose->rotations, compose->rotation_selected, compose->rotation, open, scanning_out);
	if (decision == KWL_ROTATE_SELECT_REFUSE) {
		printf("KWL OUTPUT rotate refused key=%s rotation=%u reason=mask mask=%u why=%u\n", compose->display_name, rotation,
		    compose->rotations, compose->rotate_why);
		return ENOTSUP;
	}

	/*
	 * The same choice shown changes nothing on the display; displays.conf
	 * is written when it says another angle (a test's policy showed 0 and
	 * kept the line, T1-583), so the choice is the one kept.
	 */
	if (decision == KWL_ROTATE_SELECT_NOTHING) {
		*applied = 1U;
		stored = kwl_displays_rotation(&compose->config, compose->display_name);
		if (stored == rotation)
			return 0;
		error = kwl_displays_set_rotation(&compose->config, compose->display_name, rotation);
		if (error != 0)
			return error;
		*saved = kwl_heads_config_save(server);
		printf("KWL OUTPUT rotate key=%s selected=%u saved=%d applied=1 reason=kept\n", compose->display_name, rotation, *saved);
		kwl_displays_tell(server);
		return 0;
	}

	/*
	 * Not beside an extended display shown (review M4): the heads' places
	 * are checked only when they open, and the anchor's new size would
	 * leave a gap or an overlap until ws217-p005 places them again.  Nor
	 * while a mirror shows the picture (ws217-p003 review M6): a device on
	 * the mirror's display would map by the anchor's angle until
	 * ws217-p006 maps it by the display it is on.
	 */
	heads = output_rotate_heads(compose);
	if (heads != NULL) {
		printf("KWL OUTPUT rotate refused key=%s rotation=%u reason=%s\n", compose->display_name, rotation, heads);
		return ENOTSUP;
	}

	/* The choice, kept for displays.conf (a closed output's next opening shows it). */
	previous = compose->rotation_selected;
	compose->rotation_selected = rotation;
	error = kwl_displays_set_rotation(&compose->config, compose->display_name, rotation);
	if (error != 0) {
		compose->rotation_selected = previous;
		return error;
	}

	/* A closed output: saved, shown at its next opening. */
	if (decision == KWL_ROTATE_SELECT_SAVE) {
		*saved = kwl_heads_config_save(server);
		printf("KWL OUTPUT rotate key=%s selected=%u saved=%d applied=0 reason=closed\n", compose->display_name, rotation, *saved);
		kwl_displays_tell(server);
		return 0;
	}

	/* A client's buffer on the display: saved, the display given back to the desktop, whose opening shows it. */
	if (decision == KWL_ROTATE_SELECT_LEAVE) {
		*saved = kwl_heads_config_save(server);
		kwl_scanout_leave(server, KWL_SCANOUT_ROTATED);
		printf("KWL OUTPUT rotate key=%s selected=%u saved=%d applied=0 reason=scanout\n", compose->display_name, rotation, *saved);
		kwl_displays_tell(server);
		return 0;
	}

	/* Shown now; a failure puts the choice back as it was. */
	error = kwl_output_apply_rotation(server, slot, rotation, KWL_ROTATE_MODE_USER);
	if (error != 0) {
		compose->rotation_selected = previous;
		(void)kwl_displays_set_rotation(&compose->config, compose->display_name, previous);
		return error;
	}

	/* Succeeded: shown now, saved with the turn (SAVE), and Settings told the new angle and size. */
	*applied = 1U;
	*saved = compose->rotate_saved;
	kwl_displays_tell(server);
	return 0;
}

/*
 * Changes the angle an open output shows (the anchor's; a head's is
 * ws217-p005), by mode (KWL_ROTATE_MODE_*): the turn's images exchanged,
 * the pointer kept on the same physical point, the desktop fitted to a new
 * logical size, the clients told, and the user's change of choice saved.
 * Returns 0, ENOTSUP for an angle the display may not take or one that
 * needs the output opened again (the scanout's 180, ws217-p010), ENODEV
 * without an open output, or the error of making the new images (nothing
 * changed then).
 */
int
kwl_output_apply_rotation(
	struct kwl_server *server,
	unsigned slot,
	unsigned rotation,
	unsigned mode)
{
	struct kwl_compose *compose;
	struct kwl_rotate_state after;
	char names[64];
	int32_t pointer[4];
	uint64_t started;
	unsigned steps;
	unsigned before_rotation;
	int error;

	/* An open output's anchor. */
	compose = server->compose;
	if (compose == NULL || slot != KWL_PLANE_ANCHOR)
		return ENOTSUP;
	if (!compose->output_open || compose->output_lost)
		return ENODEV;

	/* How the angle is shown on this display. */
	memset(&after, 0, sizeof(after));
	error = kwl_rotate_plan(rotation, compose->rotations, compose->hw180, &after.scanout, &after.turn);
	if (error != 0)
		return error;

	/* What the output shows after, against what it last opened or turned to (review-4 C7). */
	kwl_rotate_size(rotation, compose->mode_width, compose->mode_height, &after.width, &after.height);
	after.selected = compose->rotation_selected;
	after.rotation = rotation;
	steps = kwl_rotate_steps(&compose->opened, &after, mode);
	if (steps == 0U)
		return 0;

	/* Another angle of the display's hardware opens the output again: not in this build yet (ws217-p010). */
	if ((steps & KWL_ROTATE_STEP_REOPEN) != 0U) {
		printf("KWL OUTPUT rotate refused key=%s rotation=%u reason=reopen\n", compose->display_name, rotation);
		return ENOTSUP;
	}

	/* Another turn: the images exchanged (nothing changes when the new ones cannot be made). */
	started = kwl_milliseconds();
	before_rotation = compose->rotation;
	if ((steps & KWL_ROTATE_STEP_TURN) != 0U) {
		error = output_rotate_turn(server, &after);
		if (error != 0)
			return error;
	}

	/* The same angle (a choice saved alone): the size is set as it is. */
	if ((steps & KWL_ROTATE_STEP_TURN) == 0U)
		kwl_output_size_set(server, compose->mode_width, compose->mode_height, rotation);

	/*
	 * The pointer first, from its old logical place to the same point of
	 * the panel at the new angle (review S1: a clamp to the new size first
	 * would move it); then the desktop fitted to the new size (the windows,
	 * an arrangement whose windows no longer fit ending as at an output's
	 * move, review-4 M21; the look's images were exchanged with the turn),
	 * and the clients told.
	 */
	pointer[0] = -1;
	pointer[1] = -1;
	pointer[2] = -1;
	pointer[3] = -1;
	if ((steps & KWL_ROTATE_STEP_ROTATED) != 0U)
		kwl_output_pointer_turn(server, before_rotation, rotation, pointer);
	if ((steps & KWL_ROTATE_STEP_RESIZED) != 0U) {
		kwl_output_fit(server, 0U);
		kwl_outputs_changed(server);
	}

	/* The input thread computes the motions over the new angle (the pointer's warp gave it already when it ran). */
	kwl_input_geometry(server);

	/* Everything is drawn again; the user's change of choice is written. */
	server->dirty = 1;
	compose->rotate_saved = -1;
	if ((steps & KWL_ROTATE_STEP_SAVE) != 0U) {
		compose->rotate_saved = kwl_heads_config_save(server);
		if (compose->rotate_saved != 0)
			printf("KWL OUTPUT rotate save errno=%d\n", compose->rotate_saved);
	}

	/* Succeeded: the output shows the angle from now on, and the tests read it. */
	compose->opened = after;
	compose->opened_display = compose->display;
	kwl_output_steps_name(steps, names, sizeof(names));
	printf("KWL OUTPUT rotate key=%s rotation=%u selected=%u scanout=%u turn=%u width=%u height=%u mode=%ux%u steps=%s by=%s "
	    "pointer=%d,%d>%d,%d ms=%llu\n",
	    compose->display_name, after.rotation, after.selected, after.scanout, after.turn, after.width, after.height, compose->mode_width,
	    compose->mode_height, names, output_rotate_mode_name(mode), pointer[0], pointer[1], pointer[2], pointer[3],
	    (unsigned long long)(kwl_milliseconds() - started));
	return 0;
}

/*
 * Changes which angles the displays may take (a test's policy, WS217: the
 * test image's capture request): the anchor's mask and reasons are worked
 * out again, and a choice the new mask does not allow is shown as 0, as an
 * opening does (displays.conf's line kept).
 */
void
kwl_output_policy_set(
	struct kwl_server *server,
	unsigned policy)
{
	struct kwl_compose *compose;
	const char *name;
	unsigned why;
	unsigned bit;
	int error;

	/* The policy, for the next opening too. */
	server->rotate_policy = policy;
	compose = server->compose;
	if (compose == NULL)
		return;

	/* The anchor's angles under it. */
	compose->rotations = kwl_rotate_mask(compose->hw180, compose->turn_default, KWL_TURN_BUILD, policy, &why);
	compose->rotate_why = why;
	name = "default";
	if (policy == KWL_ROTATE_POLICY_ANY)
		name = "any";
	printf("KWL OUTPUT rotate policy=%s mask=%u why=%u turn_default=%u\n", name, compose->rotations, why, compose->turn_default);

	/* A choice still allowed stays; Settings is told the new angles. */
	bit = kwl_rotate_bit(compose->rotation_selected);
	if ((compose->rotations & bit) != 0U) {
		kwl_displays_tell(server);
		return;
	}

	/* Otherwise 0, as an opening chooses for an angle the display cannot take (nothing saved). */
	compose->rotation_selected = KWL_ROTATE_0;
	if (!compose->output_open || compose->output_lost) {
		kwl_displays_tell(server);
		return;
	}
	error = kwl_output_apply_rotation(server, KWL_PLANE_ANCHOR, KWL_ROTATE_0, KWL_ROTATE_MODE_POLICY);
	if (error != 0)
		printf("KWL OUTPUT rotate policy errno=%d\n", error);

	/* Settings is told the new angles and size. */
	kwl_displays_tell(server);
}

/*
 * Gives what Settings is told of a display's angle (ws217-p004): for the
 * desktop's anchor (anchor 1), the angle the user chose, the angles it may
 * take and why not, the others but the chosen one held while another
 * display is shown beside it; for any other display (a head, or one shown
 * nowhere), 0 alone until a head's angle (ws217-p005), with that reason.
 */
void
kwl_output_rotation_report(
	struct kwl_server *server,
	unsigned anchor,
	unsigned *rotation,
	unsigned *mask,
	unsigned *why)
{
	struct kwl_compose *compose;
	const char *heads;
	unsigned alone;

	/* Another display than the anchor: unturned, its angle comes with the heads'. */
	compose = server->compose;
	if (compose == NULL || !anchor) {
		*rotation = KWL_ROTATE_0;
		*mask = KWL_ROTATION_0;
		*why = KWL_ROTATE_WHY_ALONE;
		return;
	}

	/* The anchor, alone or beside the heads shown. */
	heads = output_rotate_heads(compose);
	alone = 1U;
	if (heads != NULL)
		alone = 0U;

	/* Succeeded: the choice, and the angles it may take now. */
	*rotation = compose->rotation_selected;
	kwl_rotate_report(compose->rotation_selected, compose->rotations, compose->rotate_why, alone, mask, why);
}

/*
 * Exchanges the turn's images for another angle the compositor turns (or
 * none).  Another logical size makes a new turn image and the look's
 * wallpaper first (a failure changes nothing), and with the render and
 * cursor threads parked and the device idle the old ones go and the
 * size-bound caches are made again.  The same size (90 and 270, 0 and a
 * turned 180) keeps every image of the size, the turn image too (its
 * pixels are logical), and only the swapchain's images are drawn whole
 * (review M5).  Returns 0 or the errno of making the new images.
 */
static int
output_rotate_turn(
	struct kwl_server *server,
	const struct kwl_rotate_state *after)
{
	struct kwl_compose *compose;
	struct kwl_turn fresh;
	struct kwl_glass_size glass;
	unsigned scene_kept;
	unsigned resized;
	unsigned reuse;
	VkResult result;
	int error;

	/* Whether the logical size changes, and whether the turn image of the size can serve the new angle. */
	compose = server->compose;
	memset(&fresh, 0, sizeof(fresh));
	memset(&glass, 0, sizeof(glass));
	resized = 0U;
	if (after->width != server->width || after->height != server->height)
		resized = 1U;
	reuse = 0U;
	if (!resized && after->turn != KWL_ROTATE_0 && compose->turn.image != VK_NULL_HANDLE)
		reuse = 1U;

	/* A new turn image, when the new angle is turned by the compositor and none of the size serves. */
	if (after->turn != KWL_ROTATE_0 && !reuse) {
		result = kwl_turn_open(compose, after->width, after->height, after->turn, &fresh);
		if (result != VK_SUCCESS) {
			printf("KWL OUTPUT rotate refused key=%s rotation=%u reason=turn result=%d\n", compose->display_name, after->rotation, (int)result);
			return ENOMEM;
		}
	}

	/* The look's wallpaper at the new logical size, when the size changes. */
	if (resized) {
		error = kwl_glass_size_prepare(server, after->width, after->height, &glass);
		if (error != 0) {
			printf("KWL OUTPUT rotate refused key=%s rotation=%u reason=glass errno=%d\n", compose->display_name, after->rotation, error);
			kwl_turn_close(compose, &fresh);
			return error;
		}
	}

	/*
	 * The fingers and the pens end what they were doing while their places
	 * still mean the old points (ws217-p003): the angle changes now.
	 */
	kwl_input_rotated(server);

	/*
	 * No frame, nor the cursor's plane (its thread submits on the same
	 * queue, review M2), may use the old images while they go; the cursor
	 * thread goes on at the next kwl_cursor_plane_update.
	 */
	kwl_compose_park(server);
	kwl_cursor_plane_hold(server);
	(void)vkDeviceWaitIdle(compose->device);

	/* The new angle and logical size; the turn's images can be made again. */
	kwl_output_size_set(server, compose->mode_width, compose->mode_height, after->rotation);
	compose->scanout_rotation = after->scanout;
	compose->turn_rotation = after->turn;
	compose->rotate_why &= ~KWL_ROTATE_WHY_RESOURCE;

	/* The same turn image at the new angle: its pixels stay, only the turn pass turns them otherwise. */
	if (reuse) {
		compose->turn.rotation = after->turn;
		memset(compose->image_frames, 0, sizeof(compose->image_frames));
		kwl_compose_unpark(server);
		return 0;
	}

	/* The turn image exchanged (or none); the frames are drawn into it, or into the swapchain's images again. */
	kwl_turn_close(compose, &compose->turn);
	compose->turn = fresh;
	compose->target_width = compose->output.width;
	compose->target_height = compose->output.height;
	if (compose->turn.image != VK_NULL_HANDLE) {
		compose->target_width = compose->turn.width;
		compose->target_height = compose->turn.height;
	}

	/* The look's wallpaper exchanged. */
	kwl_glass_size_swap(server, &glass);

	/* The caches of another size made again; every image is drawn whole next. */
	scene_kept = 0U;
	if (compose->scene.buffer != VK_NULL_HANDLE)
		scene_kept = 1U;
	output_rotate_caches(server, scene_kept, resized);

	/* Succeeded: the render thread goes on with the new images. */
	kwl_compose_unpark(server);
	return 0;
}

/*
 * Makes the caches of the drawn-into size again after a turn to another
 * size (the device idle): the scene's copy (when the output had one), the
 * layers' pictures and the backdrop (made at its next use, with its own
 * pass); a failure leaves each unused, as at the output's opening.  The
 * same size keeps them.  Every image is drawn whole next.
 */
static void
output_rotate_caches(
	struct kwl_server *server,
	unsigned scene_kept,
	unsigned resized)
{
	struct kwl_compose *compose;
	VkResult result;

	/* The scene's copy, the layers' pictures and the backdrop at the new size. */
	compose = server->compose;
	if (resized) {
		kwl_scene_close(&compose->scene, compose->device);
		if (scene_kept) {
			result = kwl_scene_open(&compose->scene, compose->physical, compose->device, compose->target_width, compose->target_height);
			if (result != VK_SUCCESS)
				printf("KWL SCENE unavailable result=%d\n", (int)result);
		}

		/* The layers' pictures, and the backdrop made at its next use. */
		kwl_layers_close(compose);
		kwl_layers_open(server);
		kwl_backdrop_destroy(compose);
	}

	/* No image holds the new picture yet. */
	memset(compose->image_frames, 0, sizeof(compose->image_frames));
	compose->turn.frame = 0U;
	compose->scene.valid = 0U;
}

/*
 * Names why the anchor may not turn beside the other displays shown: a
 * head open in the extended mode ("heads") or in the mirror ("mirror");
 * NULL when the output is alone.
 */
static const char *
output_rotate_heads(
	const struct kwl_compose *compose)
{
	unsigned index;
	int open;

	/* Any head open. */
	open = 0;
	for (index = 0U; index < KWL_HEADS; index++) {
		if (compose->heads[index].open)
			open = 1;
	}

	/* None: the output is alone. */
	if (!open)
		return NULL;

	/* The extended mode's heads, or the mirror's. */
	if (compose->display_mode == KWL_DISPLAYS_EXTENDED)
		return "heads";

	/* Succeeded: a mirror shows the picture. */
	return "mirror";
}

/* Names who changed the angle, for the log. */
static const char *
output_rotate_mode_name(
	unsigned mode)
{
	/* Each mode by its word. */
	switch (mode) {
	case KWL_ROTATE_MODE_USER:
		return "user";
	case KWL_ROTATE_MODE_SESSION_PAUSE:
		return "pause";
	case KWL_ROTATE_MODE_SESSION_RESUME:
		return "resume";
	case KWL_ROTATE_MODE_POLICY:
		return "policy";
	default:
		break;
	}

	/* An opening's change. */
	return "open";
}
