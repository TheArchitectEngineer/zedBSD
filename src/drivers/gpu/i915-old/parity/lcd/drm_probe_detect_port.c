/*
 * Copyright (c) 2006-2008 Intel Corporation
 * Copyright (c) 2007 Dave Airlie <airlied@linux.ie>
 *
 * DRM core CRTC related functions
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 *
 * Authors:
 *      Keith Packard
 *	Eric Anholt <eric@anholt.net>
 *      Dave Airlie <airlied@linux.ie>
 *      Jesse Barnes <jesse.barnes@intel.com>
 */

/*
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/drm_probe_helper.c
 * (sha256 05cedd4c3bd94a7a451d525433754952d8e48fa1dd9c8b918a0912247181e9c6) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept: drm_helper_probe_detect_ctx, drm_helper_probe_detect;
 *  - the includes are replaced by: hpd_compat.h;
 */

#include "hpd_compat.h"

/* zedBSD: forward declarations of the static functions of this file (generated; the keep-list is not in call order) */
static enum drm_connector_status
drm_helper_probe_detect_ctx(struct drm_connector *connector, bool force);

static enum drm_connector_status
drm_helper_probe_detect_ctx(struct drm_connector *connector, bool force)
{
	const struct drm_connector_helper_funcs *funcs = connector->helper_private;
	struct drm_modeset_acquire_ctx ctx;
	int ret;

	drm_modeset_acquire_init(&ctx, 0);

retry:
	ret = drm_modeset_lock(&connector->dev->mode_config.connection_mutex, &ctx);
	if (!ret) {
		if (funcs->detect_ctx)
			ret = funcs->detect_ctx(connector, &ctx, force);
		else if (connector->funcs->detect)
			ret = connector->funcs->detect(connector, force);
		else
			ret = connector_status_connected;
	}

	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry;
	}

	if (WARN_ON(ret < 0))
		ret = connector_status_unknown;

	if (ret != connector->status)
		connector->epoch_counter += 1;

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	return ret;
}

/**
 * drm_helper_probe_detect - probe connector status
 * @connector: connector to probe
 * @ctx: acquire_ctx, or NULL to let this function handle locking.
 * @force: Whether destructive probe operations should be performed.
 *
 * This function calls the detect callbacks of the connector.
 * This function returns &drm_connector_status, or
 * if @ctx is set, it might also return -EDEADLK.
 */
int
drm_helper_probe_detect(struct drm_connector *connector,
			struct drm_modeset_acquire_ctx *ctx,
			bool force)
{
	const struct drm_connector_helper_funcs *funcs = connector->helper_private;
	struct drm_device *dev = connector->dev;
	int ret;

	if (!ctx)
		return drm_helper_probe_detect_ctx(connector, force);

	ret = drm_modeset_lock(&dev->mode_config.connection_mutex, ctx);
	if (ret)
		return ret;

	if (funcs->detect_ctx)
		ret = funcs->detect_ctx(connector, ctx, force);
	else if (connector->funcs->detect)
		ret = connector->funcs->detect(connector, force);
	else
		ret = connector_status_connected;

	if (ret != connector->status)
		connector->epoch_counter += 1;

	return ret;
}

