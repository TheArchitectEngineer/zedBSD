/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Exercises production boot-slot publication with a small mount model. */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define KERN_BOOT_SOURCE_SLOT_COUNT 4U
#define KERN_SWAP_SOURCE_COUNT 4U
#define VFS_LEGACY_NULL_AUTOROOT 1

/* One modeled partition identity, with no allocated lifetime. */
struct disk {
	int identity;
};

/* One retained mount and the state its publication observes. */
struct mount {
	struct disk *m_disk;
	int private;
};

/* One configured slot of the common boot-source context. */
struct kern_boot_source_slot {
	unsigned configured;
	struct mount *mount;
	struct disk *disk;
};

/* Boot file references passed to the production publication function. */
struct kern_boot_parameters {
	const char *root;
	const char *data;
	const char *swap[4];
};

/* The production function consumes these retained slots during startup. */
static struct {
	struct kern_boot_source_slot slot[4];
} boot_sources;

/* The old ARM overlay owns a separate mount, absent for native UFS. */
#if defined(HAL_ARCH_ARM64)
static struct mount *vfs_legacy_boot_mount;
#endif

/* Publication counters are reset between scenarios by the single test thread. */
static unsigned published[4];

/* Counts adoption requests to verify that the existing mount is reused. */
static unsigned adopted;

static const char *kern_boot_parameters_overlay_root(const struct kern_boot_parameters *parameters);
static const char *kern_boot_parameters_overlay_data(const struct kern_boot_parameters *parameters);
static const char *kern_boot_parameters_swap(const struct kern_boot_parameters *parameters, unsigned index);
static void vfs_boot_reference_mark(const char *text, unsigned *references);
static int mount_is_private(struct mount *mountp);
static int mount_private_allow_adoption(struct mount *mountp);
static void vfs_publish_boot_slot(unsigned index, struct mount *mountp, struct disk *disk);

/* Returns the modeled root image reference. */
static const char *
kern_boot_parameters_overlay_root(
	const struct kern_boot_parameters *parameters)
{
	/* Supplies the reference without interpreting it. */
	return parameters->root;
}

/* Returns the modeled data image reference. */
static const char *
kern_boot_parameters_overlay_data(
	const struct kern_boot_parameters *parameters)
{
	/* Supplies the reference without interpreting it. */
	return parameters->data;
}

/* Returns one modeled swap reference. */
static const char *
kern_boot_parameters_swap(
	const struct kern_boot_parameters *parameters,
	unsigned index)
{
	/* Supplies the reference for this slot. */
	return parameters->swap[index];
}

/* Marks only the fixture's recognized boot1 reference. */
static void
vfs_boot_reference_mark(
	const char *text,
	unsigned *references)
{
	int matches;

	/* An absent reference cannot select a slot. */
	if (text == NULL)
		return;

	/* Keeps parsing out of the publication model. */
	matches = strcmp(text, "boot1:data.img");
	if (matches == 0)
		references[1] = 1;
}

/* Reports whether the fixture mount is still private. */
static int
mount_is_private(
	struct mount *mountp)
{
	/* Exposes the supplied ownership state. */
	return mountp->private;
}

/* Records adoption of the same retained mount. */
static int
mount_private_allow_adoption(
	struct mount *mountp)
{
	/* Requires the mount to remain privately owned. */
	assert(mountp->private);
	adopted++;

	/* Succeeded: no new mount was allocated. */
	return 0;
}

/* Records publication of a retained mount. */
static void
vfs_publish_boot_slot(
	unsigned index,
	struct mount *mountp,
	struct disk *disk)
{
	/* The publication must preserve partition ownership. */
	assert(mountp->m_disk == disk);
	published[index]++;
}

/* This generated include is the unchanged production function body. */
#include "boot0-publication-under-test.inc"

/* Runs startup publication scenarios without a guest. */
int
main(
	void)
{
	struct disk disk;
	struct mount mountp;
	struct kern_boot_parameters parameters;

	/* Models a retained, writable boot FAT beside the native UFS root. */
	memset(&parameters, 0, sizeof(parameters));
	mountp.m_disk = &disk;
	mountp.private = 1;
	boot_sources.slot[0].configured = 1;
	boot_sources.slot[0].mount = &mountp;
	boot_sources.slot[0].disk = &disk;
	vfs_publish_boot_filesystems(&parameters);
#if defined(HAL_BOARD_RPI4)
	assert(published[0] == 1);
#else
	assert(published[0] == 0);
#endif
	assert(adopted == 1);

	/* An unconfigured or absent mount must never be published. */
	memset(published, 0, sizeof(published));
	boot_sources.slot[0].configured = 0;
	vfs_publish_boot_filesystems(&parameters);
	assert(published[0] == 0);
	boot_sources.slot[0].configured = 1;
	boot_sources.slot[0].mount = NULL;
	vfs_publish_boot_filesystems(&parameters);
	assert(published[0] == 0);

	/* Existing file references still publish their configured slots. */
	boot_sources.slot[1].configured = 1;
	boot_sources.slot[1].mount = &mountp;
	boot_sources.slot[1].disk = &disk;
	parameters.data = "boot1:data.img";
	vfs_publish_boot_filesystems(&parameters);
	assert(published[1] == 1);

#if defined(HAL_ARCH_ARM64)
	/* The old overlay continues to publish its separately retained FAT. */
	vfs_legacy_boot_mount = &mountp;
	vfs_publish_boot_filesystems(&parameters);
	assert(published[0] == 1);
#endif

	/* Reports that the production publication decisions passed. */
	puts("boot0 production publication: PASS");
	return 0;
}
