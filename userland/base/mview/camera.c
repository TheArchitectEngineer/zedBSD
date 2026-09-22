/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Computes the orbit camera and the clip and normal transforms it implies.
 */

#include "model.h"

#include <math.h>
#include <string.h>

/* The vertical field of view, in degrees, of every projection. */
#define CAMERA_FOV_DEGREES	45.0f

/* The margin kept around the bounding sphere by the initial fit. */
#define CAMERA_FIT_MARGIN	1.05f

/* Pitch stops short of the poles so the view never flips over. */
#define CAMERA_PITCH_LIMIT	89.0f

/* One wheel notch or zoom key scales the viewing distance by this factor. */
#define CAMERA_ZOOM_STEP	1.1f

/* The viewer may come this close, relative to the model radius... */
#define CAMERA_NEAR_LIMIT	0.05f

/* ...and go this far away. */
#define CAMERA_FAR_LIMIT	20.0f

static float camera_radians(float degrees);
static void camera_rotation(const struct mview_camera *camera, float rotation[9]);

/*
 * Places the camera so that the whole bounding sphere is visible.
 *
 * The view starts from the front: the model's +Z axis points at the viewer.
 * The fitted distance is also the one a reset returns to.
 */
void
mview_camera_fit(
	struct mview_camera *camera,
	const float minimum[3],
	const float maximum[3],
	uint32_t width,
	uint32_t height)
{
	float extent[3];
	float half_angle;
	float horizontal;
	float aspect;
	float vertical_distance;
	float horizontal_distance;
	uint32_t axis;

	/* The orbit turns about the centre of the axis-aligned bounds. */
	memset(camera, 0, sizeof(*camera));
	for (axis = 0U; axis < 3U; axis++) {
		camera->center[axis] = 0.5f * (minimum[axis] + maximum[axis]);
		extent[axis] = maximum[axis] - minimum[axis];
	}

	/* The bounding sphere encloses the box; an empty box still gets a usable size. */
	camera->radius = 0.5f * sqrtf(extent[0] * extent[0] + extent[1] * extent[1] + extent[2] * extent[2]);
	if (!(camera->radius > 1.0e-6f))
		camera->radius = 1.0f;

	/* A degenerate window is treated as square rather than dividing by zero. */
	aspect = 1.0f;
	if (width != 0U && height != 0U)
		aspect = (float)width / (float)height;

	/* The sphere must fit both the vertical and the horizontal field of view. */
	half_angle = camera_radians(0.5f * CAMERA_FOV_DEGREES);
	horizontal = atanf(tanf(half_angle) * aspect);
	vertical_distance = camera->radius / sinf(half_angle);
	horizontal_distance = camera->radius / sinf(horizontal);

	/* The narrower of the two directions decides how far back the viewer stands. */
	camera->initial_distance = vertical_distance;
	if (horizontal_distance > camera->initial_distance)
		camera->initial_distance = horizontal_distance;

	camera->initial_distance *= CAMERA_FIT_MARGIN;

	/* The first frame and every reset use exactly these parameters. */
	mview_camera_reset(camera);

	/* Succeeded: the camera frames the whole model from the front. */
	return;
}

/*
 * Restores the initial front view.
 *
 * Every parameter is assigned from a constant or from the fitted distance, so
 * the frame after a reset is identical to the first frame.
 */
void
mview_camera_reset(
	struct mview_camera *camera)
{
	/* The front view has no rotation and no pan. */
	camera->yaw = 0.0f;
	camera->pitch = 0.0f;
	camera->pan[0] = 0.0f;
	camera->pan[1] = 0.0f;

	/* The fitted distance shows the whole model again. */
	camera->distance = camera->initial_distance;

	/* Succeeded: the camera is back at its initial view. */
	return;
}

/*
 * Turns the model by the given yaw and pitch, in degrees.
 *
 * Yaw wraps into (-180, 180] so logged values stay small; pitch is clamped.
 */
void
mview_camera_orbit(
	struct mview_camera *camera,
	float yaw,
	float pitch)
{
	/* Accumulates the turn about the vertical axis and keeps it in one revolution. */
	camera->yaw += yaw;
	while (camera->yaw > 180.0f)
		camera->yaw -= 360.0f;

	/* The other side of the wrap mirrors the first. */
	while (camera->yaw <= -180.0f)
		camera->yaw += 360.0f;

	/* Accumulates the tilt and stops it short of looking straight down or up. */
	camera->pitch += pitch;
	if (camera->pitch > CAMERA_PITCH_LIMIT)
		camera->pitch = CAMERA_PITCH_LIMIT;

	/* The lower limit mirrors the upper one. */
	if (camera->pitch < -CAMERA_PITCH_LIMIT)
		camera->pitch = -CAMERA_PITCH_LIMIT;

	/* Succeeded: the rotation holds the accumulated, bounded angles. */
	return;
}

/*
 * Shifts the model in the view plane by a pointer movement in pixels.
 *
 * One pixel moves the model by one pixel's worth of the view-plane height at
 * the orbit centre, so the point under the cursor follows the cursor.
 */
void
mview_camera_pan(
	struct mview_camera *camera,
	float dx,
	float dy,
	uint32_t height)
{
	float visible;
	float scale;

	/* A zero-height window has no pixel size and cannot pan. */
	if (height == 0U)
		return;

	/* The height of the view plane through the orbit centre, in model units. */
	visible = 2.0f * camera->distance * tanf(camera_radians(0.5f * CAMERA_FOV_DEGREES));
	scale = visible / (float)height;

	/* Screen x grows rightward like view x; screen y grows downward, unlike view y. */
	camera->pan[0] += dx * scale;
	camera->pan[1] -= dy * scale;

	/* Succeeded: the pan holds the accumulated view-plane shift. */
	return;
}

/*
 * Moves the viewer by whole zoom steps: positive notches move away.
 *
 * The distance is clamped relative to the model radius so the model can
 * neither swallow the view nor vanish.
 */
void
mview_camera_zoom(
	struct mview_camera *camera,
	int notches)
{
	float limit;

	/* Moving away multiplies the distance once per step. */
	while (notches > 0) {
		camera->distance *= CAMERA_ZOOM_STEP;
		notches--;
	}

	/* Moving closer divides it once per step. */
	while (notches < 0) {
		camera->distance /= CAMERA_ZOOM_STEP;
		notches++;
	}

	/* The viewer never enters the model's immediate surface. */
	limit = camera->radius * CAMERA_NEAR_LIMIT;
	if (camera->distance < limit)
		camera->distance = limit;

	/* The model never shrinks to a dot. */
	limit = camera->radius * CAMERA_FAR_LIMIT;
	if (camera->distance > limit)
		camera->distance = limit;

	/* Succeeded: the distance holds the bounded zoom. */
	return;
}

/*
 * Fills the clip and normal transforms for the current view.
 *
 * A model point p is moved to view space as R (p - centre) + (pan, -distance),
 * then projected with a right-handed perspective into Vulkan clip space, whose
 * y axis points down and whose depth runs from 0 to 1.  All matrices are
 * written column by column.
 */
void
mview_camera_push(
	const struct mview_camera *camera,
	uint32_t width,
	uint32_t height,
	struct mview_push *push)
{
	float rotation[9];
	float translation[3];
	float focal;
	float aspect;
	float near_plane;
	float far_plane;
	float depth_scale;
	float depth_offset;
	uint32_t column;
	uint32_t row;

	/* The rotation is the combined pitch and yaw, column-major. */
	camera_rotation(camera, rotation);

	/* The translation carries the rotated centre to the pan offset in front of the viewer. */
	for (row = 0U; row < 3U; row++) {
		translation[row] = 0.0f;
		for (column = 0U; column < 3U; column++)
			translation[row] -= rotation[column * 3U + row] * camera->center[column];
	}

	translation[0] += camera->pan[0];
	translation[1] += camera->pan[1];
	translation[2] -= camera->distance;

	/* A degenerate window is treated as square rather than dividing by zero. */
	aspect = 1.0f;
	if (width != 0U && height != 0U)
		aspect = (float)width / (float)height;

	/*
	 * The depth range brackets the bounding sphere; a pan cannot move the
	 * model along the view axis, so the sphere stays inside it.
	 */
	far_plane = camera->distance + 2.0f * camera->radius;
	near_plane = camera->distance - 2.0f * camera->radius;
	if (near_plane < camera->distance * 0.01f)
		near_plane = camera->distance * 0.01f;

	/* The perspective scale and the depth mapping onto Vulkan's 0..1 range. */
	focal = 1.0f / tanf(camera_radians(0.5f * CAMERA_FOV_DEGREES));
	depth_scale = far_plane / (near_plane - far_plane);
	depth_offset = near_plane * far_plane / (near_plane - far_plane);

	/*
	 * Composes projection and view: each clip column is the projection of one
	 * view-matrix column.  Clip w is the negated view z.
	 */
	memset(push->clip, 0, sizeof(push->clip));
	for (column = 0U; column < 3U; column++) {
		push->clip[column * 4U + 0U] = focal / aspect * rotation[column * 3U + 0U];
		push->clip[column * 4U + 1U] = -focal * rotation[column * 3U + 1U];
		push->clip[column * 4U + 2U] = depth_scale * rotation[column * 3U + 2U];
		push->clip[column * 4U + 3U] = -rotation[column * 3U + 2U];
	}

	/* The fourth column projects the translation, which carries a w of one. */
	push->clip[12] = focal / aspect * translation[0];
	push->clip[13] = -focal * translation[1];
	push->clip[14] = depth_scale * translation[2] + depth_offset;
	push->clip[15] = -translation[2];

	/* Normals only turn with the model; each column is padded to four floats. */
	memset(push->normal, 0, sizeof(push->normal));
	for (column = 0U; column < 3U; column++) {
		for (row = 0U; row < 3U; row++)
			push->normal[column * 4U + row] = rotation[column * 3U + row];
	}

	/* Succeeded: the push block holds the transforms of the current view. */
	return;
}

/* Converts an angle from degrees to radians. */
static float
camera_radians(
	float degrees)
{
	/* Reports the angle in radians. */
	return degrees * 3.14159265358979f / 180.0f;
}

/* Builds the column-major rotation Rx(pitch) Ry(yaw). */
static void
camera_rotation(
	const struct mview_camera *camera,
	float rotation[9])
{
	float cy;
	float sy;
	float cp;
	float sp;

	/* Samples the two angles once. */
	cy = cosf(camera_radians(camera->yaw));
	sy = sinf(camera_radians(camera->yaw));
	cp = cosf(camera_radians(camera->pitch));
	sp = sinf(camera_radians(camera->pitch));

	/* The model's x axis turns toward -z as yaw grows. */
	rotation[0] = cy;
	rotation[1] = sp * sy;
	rotation[2] = -cp * sy;

	/* The model's y axis tilts toward the viewer as pitch grows. */
	rotation[3] = 0.0f;
	rotation[4] = cp;
	rotation[5] = sp;

	/* The model's z axis, its front, turns toward +x with yaw and -y with pitch. */
	rotation[6] = sy;
	rotation[7] = -sp * cy;
	rotation[8] = cp * cy;

	/* Succeeded: the rotation holds both angles. */
	return;
}
