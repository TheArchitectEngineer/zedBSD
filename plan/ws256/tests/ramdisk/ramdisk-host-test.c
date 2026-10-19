/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the memory disk on the riscv64 root image: the disk layer is
 * replaced by stubs, and the requests are built by hand as the disk layer
 * would hand them over (already mapped to the leaf's blocks).
 *
 *   ramdisk-host-test root.img
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <drivers/disklabel/disklabel.h>
#include <drivers/generic/ramdisk.h>
#include <kern/disk.h>
#include <kern/partition.h>

static struct disk the_disk;
static int created;
static int completed;
static int completed_error;
static size_t completed_bytes;
static uint32_t stats_kind;
static int failures;

static void
check(int condition, const char *what)
{
	if (condition) {
		printf("PASS %s\n", what);
	} else {
		printf("FAIL %s\n", what);
		failures++;
	}
}

struct disk *disk_alloc(void) { memset(&the_disk, 0, sizeof(the_disk)); return &the_disk; }
int disk_create(struct disk *disk) { (void)disk; created++; return 0; }
void disk_set_stats_kind(struct disk *disk, uint32_t kind) { (void)disk; stats_kind = kind; }

void
bio_complete(struct bio *bio, int error, size_t transferred)
{
	(void)bio;
	completed++;
	completed_error = error;
	completed_bytes = transferred;
}

static int submit(struct disk *disk, enum bio_op op, uint64_t block, uint32_t count, void *data);

int
disk_read(struct disk *disk, uint64_t block, uint32_t count, void *data)
{
	int error;

	error = submit(disk, BIO_READ, block, count, data);
	if (error != 0)
		return error;
	return completed_error;
}

static int
submit(struct disk *disk, enum bio_op op, uint64_t block, uint32_t count, void *data)
{
	struct bio bio;

	memset(&bio, 0, sizeof(bio));
	bio.b_op = op;
	bio.b_disk = disk;
	bio.b_leaf_disk = disk;
	bio.b_block = block;
	bio.b_mapped_block = block;
	bio.b_block_count = count;
	bio.b_data = data;
	completed = 0;
	return disk->d_ops->submit(disk, &bio);
}

int
main(int argc, char **argv)
{
	FILE *file;
	uint8_t *image;
	uint8_t sector[1024];
	uint8_t pattern[512];
	struct disk *disk;
	long size;
	uint32_t root_lba;
	uint32_t root_blocks;
	struct partition entries[4];
	int count;
	int error;

	if (argc != 2) {
		fprintf(stderr, "usage: %s root.img\n", argv[0]);
		return 2;
	}
	file = fopen(argv[1], "rb");
	if (file == NULL) {
		perror(argv[1]);
		return 2;
	}
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	image = malloc((size_t)size + 100);
	if (image == NULL || fread(image, 1, (size_t)size, file) != (size_t)size)
		return 2;
	fclose(file);

	check(drv_ramdisk_create("ram0", image, (uint64_t)size + 100, &disk) != 0, "a size that is not whole blocks is refused");
	check(drv_ramdisk_create("ram0", NULL, (uint64_t)size, &disk) != 0, "a missing range is refused");
	check(drv_ramdisk_create("a-name-longer-than-the-disk-record-holds", image, (uint64_t)size, &disk) != 0, "a name too long is refused");
	error = drv_ramdisk_create("ram0", image, (uint64_t)size, &disk);
	check(error == 0 && created == 1, "the image is published as a disk");
	check(strcmp(disk->d_name, "ram0") == 0, "the disk is named ram0");
	check(disk->d_block_size == 512 && disk->d_block_count == (uint64_t)size / 512, "the disk has the image's blocks");
	check(stats_kind != 0, "the disk is listed in the disk statistics");
	check(drv_ramdisk_create("ram1", image, (uint64_t)size, &disk) != 0, "a second ramdisk is refused");

	error = submit(disk, BIO_READ, 0, 2, sector);
	check(error == 0 && completed == 1 && completed_error == 0 && completed_bytes == 1024, "two blocks read and completed");
	check(sector[510] == 0x55 && sector[511] == 0xaa, "block 0 is an MBR");
	check(sector[0x1be + 4] == 0x0e && sector[0x1ce + 4] == 0xa5, "partition 1 is FAT16, partition 2 is the UFS root");
	memcpy(&root_lba, &sector[0x1ce + 8], 4);
	memcpy(&root_blocks, &sector[0x1ce + 12], 4);
	check((uint64_t)root_lba + root_blocks == disk->d_block_count, "the root partition ends at the end of the disk");
	check(memcmp(sector + 512, image + 512, 512) == 0, "block 1 read from the image");

	count = drv_partition_scheme_mbr.scan(&drv_partition_scheme_mbr, disk, entries, 4);
	check(count == 4, "the kernel's MBR scan reads the table");
	check(entries[0].p_start_block == 2048 && entries[0].p_block_count == 16384, "the kernel sees partition 1 at 2048, 16384 blocks");
	check(entries[1].p_start_block == root_lba && entries[1].p_block_count == root_blocks, "the kernel sees partition 2 as the root");
	check(entries[2].p_block_count == 0 && entries[3].p_block_count == 0, "the kernel sees no third or fourth partition");
	check(memcmp(image + (size_t)root_lba * 512 + 65536 + 1372, "\x19\x01\x54\x19", 4) == 0, "partition 2 holds a UFS superblock");

	memset(pattern, 0x5a, sizeof(pattern));
	error = submit(disk, BIO_WRITE, 7, 1, pattern);
	check(error == 0 && completed_error == 0 && completed_bytes == 512, "one block written");
	check(memcmp(image + 7 * 512, pattern, 512) == 0, "the write changed the memory");
	error = submit(disk, BIO_FLUSH, 0, 0, NULL);
	check(error == 0 && completed_error == 0 && completed_bytes == 0, "a flush completes with nothing moved");

	if (failures != 0) {
		printf("%d FAILED\n", failures);
		return 1;
	}
	printf("all PASS\n");
	return 0;
}
