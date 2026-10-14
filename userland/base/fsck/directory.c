/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The directory passes of fsck (BUG-312).
 *
 * Pass 2 walks the directories from the root down.  A directory is a
 * whole number of 512-byte record blocks; each record is an inode number,
 * the record's length, an entry type and a name, and the records of a
 * record block fill it exactly.  The first record block starts with "."
 * (the directory itself) and ".." (its parent).  A record that does not
 * hold together ends its record block: the rest becomes one empty record.
 * An entry naming a free inode, a second name of a directory, or a "." or
 * ".." elsewhere is emptied.  The walk counts the names of every inode.
 *
 * Pass 3 connects what the walk did not reach: a directory, or a file
 * that still has a link, gets a name "#<inode>" in /lost+found (made when
 * the volume has none); a file with no link left (unlinked while open
 * when the machine stopped) is freed.
 */

#include "fsck.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The bytes a record needs for a name of this length: the header, and the name rounded to four. */
#define FSCK_RECORD_BYTES(length)	(8U + (((uint32_t)(length) + 3U) & ~3U))

/* The record header: inode number, record length, type and name length. */
#define FSCK_RECORD_INO		0U
#define FSCK_RECORD_RECLEN	4U
#define FSCK_RECORD_TYPE	6U
#define FSCK_RECORD_NAMLEN	7U
#define FSCK_RECORD_NAME	8U

/* The entry type of a directory. */
#define FSCK_TYPE_DIR		4U

/* The longest name fsck writes ("#" and an inode number). */
#define FSCK_NAME_MAX		16U

static int fsck_walk(struct fsck_volume *volume, uint32_t first, uint32_t *queue);
static int fsck_scan_directory(struct fsck_volume *volume, uint32_t directory, uint32_t *queue, uint32_t *tail);
static void fsck_scan_chunk(struct fsck_volume *volume, uint32_t directory, uint8_t *chunk, int first, int *dirty, uint32_t *queue, uint32_t *tail);
static uint32_t fsck_dot_entry(struct fsck_volume *volume, uint32_t directory, uint8_t *chunk, uint32_t pos, uint32_t reclen, const char *dots, uint32_t number, int *dirty);
static void fsck_put_record(struct fsck_volume *volume, uint8_t *chunk, uint32_t pos, uint32_t number, uint32_t reclen, uint8_t type, const char *name);
static int fsck_record_valid(const struct fsck_volume *volume, const uint8_t *chunk, uint32_t pos);
static uint32_t fsck_lost_found(struct fsck_volume *volume);
static uint32_t fsck_find_entry(struct fsck_volume *volume, uint32_t directory, const char *name);
static int fsck_add_entry(struct fsck_volume *volume, uint32_t directory, const char *name, uint32_t number, uint8_t type);
static uint64_t fsck_allocate_block(struct fsck_volume *volume);
static uint32_t fsck_allocate_inode(struct fsck_volume *volume);
static uint32_t fsck_dotdot(struct fsck_volume *volume, uint32_t directory);

/*
 * Pass 2: walks the directories from the root down.  Returns 0, or an
 * I/O error.
 */
int
fsck_directories(
	struct fsck_volume *volume)
{
	uint32_t *queue;
	int error;

	/* The queue of directories to scan; each directory enters it once. */
	queue = malloc((size_t)volume->inode_count * sizeof(uint32_t));
	if (queue == NULL)
		return ENOMEM;

	/* The root is its own parent. */
	volume->parent[UFS_ROOT_INO] = UFS_ROOT_INO;
	volume->state[UFS_ROOT_INO] |= FSCK_INODE_REACHED;
	error = fsck_walk(volume, UFS_ROOT_INO, queue);
	free(queue);

	/* Reports an I/O error. */
	if (error != 0)
		return error;

	/* Succeeded: what the root reaches is counted. */
	return 0;
}

/*
 * Pass 3: connects what the walk did not reach.  Directories whose ".."
 * does not name another unreached directory go first, so that a lost
 * subtree keeps its shape.  Returns 0, or an I/O error.
 */
int
fsck_connect(
	struct fsck_volume *volume)
{
	char name[FSCK_NAME_MAX];
	uint32_t *queue;
	uint32_t number;
	uint32_t parent;
	uint32_t lost;
	unsigned round;
	uint8_t kind;
	uint8_t *raw;
	int error;

	queue = malloc((size_t)volume->inode_count * sizeof(uint32_t));
	if (queue == NULL)
		return ENOMEM;

	/* Two rounds of directories, then the files. */
	error = 0;
	for (round = 0; round < 3U && error == 0; round++) {
		for (number = UFS_ROOT_INO + 1U; number < volume->inode_count && error == 0; number++) {
			if ((volume->state[number] & FSCK_INODE_REACHED) != 0)
				continue;
			kind = volume->state[number] & FSCK_INODE_KIND_MASK;
			if (kind == FSCK_INODE_FREE)
				continue;

			/* Directories: the tops of lost subtrees first, then the rest. */
			if (kind == FSCK_INODE_DIR && round < 2U) {
				parent = fsck_dotdot(volume, number);
				if (round == 0U && parent < volume->inode_count && parent != number &&
				    (volume->state[parent] & FSCK_INODE_KIND_MASK) == FSCK_INODE_DIR &&
				    (volume->state[parent] & FSCK_INODE_REACHED) == 0)
					continue;
				fsck_problem(volume, 1, "directory %u is not reached from the root; it is moved to /lost+found/#%u", number, number);

				/* Without -y the subtree is walked as if it were moved, so that it is reported once. */
				lost = parent;
				if (lost >= volume->inode_count)
					lost = UFS_ROOT_INO;
				if (volume->repair) {
					lost = fsck_lost_found(volume);
					(void)snprintf(name, sizeof(name), "#%u", number);
					if (lost == 0 || fsck_add_entry(volume, lost, name, number, FSCK_TYPE_DIR) != 0) {
						fsck_problem(volume, 0, "directory %u could not be given a name in /lost+found", number);
						continue;
					}
				}
				volume->references[number]++;
				volume->parent[number] = lost;
				volume->state[number] |= FSCK_INODE_REACHED;
				error = fsck_walk(volume, number, queue);
				continue;
			}
			if (kind == FSCK_INODE_DIR || round < 2U)
				continue;

			/* A file nothing names: freed when no link is left, else moved. */
			raw = fsck_inode(volume, number);
			if (raw == NULL) {
				error = volume->io_error;
				break;
			}
			if (fsck_get16(volume, raw, UFS_DI_NLINK) == 0) {
				fsck_problem(volume, 1, "inode %u was unlinked but is still allocated; it is freed", number);
				fsck_clear_inode(volume, number);
				continue;
			}
			fsck_problem(volume, 1, "inode %u is not named by any directory; it is moved to /lost+found/#%u", number, number);
			if (volume->repair) {
				lost = fsck_lost_found(volume);
				(void)snprintf(name, sizeof(name), "#%u", number);
				if (lost == 0 || fsck_add_entry(volume, lost, name, number, volume->dtype[number]) != 0) {
					fsck_problem(volume, 0, "inode %u could not be given a name in /lost+found", number);
					continue;
				}
			}
			volume->references[number]++;
			volume->state[number] |= FSCK_INODE_REACHED;
		}
	}
	free(queue);

	/* Reports an I/O error. */
	if (error != 0)
		return error;

	/* Succeeded: everything left is reached. */
	return 0;
}

/* Walks the directories below one that was just reached: 0, or an I/O error. */
static int
fsck_walk(
	struct fsck_volume *volume,
	uint32_t first,
	uint32_t *queue)
{
	uint32_t head;
	uint32_t tail;
	int error;

	/* Breadth first: a directory's parent is known before it is scanned. */
	head = 0;
	tail = 0;
	queue[tail++] = first;
	while (head < tail) {
		error = fsck_scan_directory(volume, queue[head], queue, &tail);
		if (error != 0)
			return error;
		head++;
	}

	/* Succeeded: the subtree is walked. */
	return 0;
}

/*
 * Scans one directory's record blocks.  A hole ends the directory there.
 * Returns 0, or an I/O error.
 */
static int
fsck_scan_directory(
	struct fsck_volume *volume,
	uint32_t directory,
	uint32_t *queue,
	uint32_t *tail)
{
	uint8_t *raw;
	uint64_t size;
	uint64_t offset;
	uint64_t logical;
	uint64_t fragment;
	int dirty;
	int error;

	raw = fsck_inode(volume, directory);
	if (raw == NULL)
		return volume->io_error;
	size = fsck_get64(volume, raw, UFS_DI_SIZE);
	size -= size % UFS_DIRBLKSIZ;

	/* Each block of the directory, and each record block in it. */
	for (offset = 0; offset < size; offset += volume->super.bsize) {
		logical = offset / volume->super.bsize;
		fragment = fsck_file_block(volume, directory, logical);
		if (fragment == UINT64_MAX)
			return volume->io_error != 0 ? volume->io_error : EIO;

		/* A hole: the directory ends before it (the first block cannot be a hole: it names dot). */
		if (fragment == 0) {
			if (offset == 0) {
				fsck_problem(volume, 0, "directory %u has no first block", directory);
				return 0;
			}
			fsck_problem(volume, 1, "directory %u has a hole at byte %llu; it is shortened to it", directory, (unsigned long long)offset);
			raw = fsck_inode(volume, directory);
			if (raw == NULL)
				return volume->io_error;
			if (volume->repair) {
				fsck_put64(volume, raw, UFS_DI_SIZE, offset);
				fsck_inode_dirty(volume);
			}
			return 0;
		}

		/* The block's record blocks. */
		error = fsck_read_block(volume, fragment, volume->data);
		if (error != 0)
			return error;
		dirty = 0;
		for (logical = 0; logical < volume->super.bsize && offset + logical < size; logical += UFS_DIRBLKSIZ)
			fsck_scan_chunk(volume, directory, volume->data + logical, offset + logical == 0, &dirty, queue, tail);
		if (dirty) {
			error = fsck_write_block(volume, fragment, volume->data);
			if (error != 0)
				return error;
		}
	}

	/* Succeeded: the directory is scanned. */
	return 0;
}

/*
 * Scans one record block: the dot entries when it is the first, then each
 * entry.  Directories it names that are not yet reached join the queue.
 */
static void
fsck_scan_chunk(
	struct fsck_volume *volume,
	uint32_t directory,
	uint8_t *chunk,
	int first,
	int *dirty,
	uint32_t *queue,
	uint32_t *tail)
{
	const char *name;
	uint32_t pos;
	uint32_t reclen;
	uint32_t number;
	uint32_t record;
	uint8_t namlen;
	uint8_t kind;
	uint8_t wanted;

	pos = 0;
	record = 0;
	while (pos < UFS_DIRBLKSIZ) {
		/* A record that does not hold together ends the record block. */
		if (!fsck_record_valid(volume, chunk, pos)) {
			fsck_problem(volume, 1, "directory %u has a damaged entry at byte %u of a record block; the rest of the record block is emptied", directory, pos);
			if (volume->repair) {
				fsck_put_record(volume, chunk, pos, 0, UFS_DIRBLKSIZ - pos, 0, "");
				*dirty = 1;
			}
			reclen = UFS_DIRBLKSIZ - pos;
		} else {
			reclen = fsck_get16(volume, chunk, pos + FSCK_RECORD_RECLEN);
		}

		/* The first two records of the first record block are "." and "..". */
		if (first && record < 2U) {
			if (record == 0U)
				reclen = fsck_dot_entry(volume, directory, chunk, pos, reclen, ".", directory, dirty);
			else
				reclen = fsck_dot_entry(volume, directory, chunk, pos, reclen, "..", volume->parent[directory], dirty);
			pos += reclen;
			record++;
			continue;
		}

		/* An empty record names nothing. */
		number = fsck_get32(volume, chunk, pos + FSCK_RECORD_INO);
		namlen = chunk[pos + FSCK_RECORD_NAMLEN];
		name = (const char *)chunk + pos + FSCK_RECORD_NAME;
		if (number == 0 || !fsck_record_valid(volume, chunk, pos)) {
			pos += reclen;
			record++;
			continue;
		}

		/* A dot entry anywhere else, or a free or unknown inode. */
		kind = FSCK_INODE_FREE;
		if (number < volume->inode_count && number >= UFS_ROOT_INO)
			kind = volume->state[number] & FSCK_INODE_KIND_MASK;
		if ((namlen == 1 && name[0] == '.') || (namlen == 2 && name[0] == '.' && name[1] == '.') || kind == FSCK_INODE_FREE) {
			fsck_problem(volume, 1, "directory %u has an entry \"%.*s\" naming inode %u, which it cannot name; it is removed", directory, namlen, name, number);
			if (volume->repair) {
				fsck_put32(volume, chunk, pos + FSCK_RECORD_INO, 0);
				*dirty = 1;
			}
			pos += reclen;
			record++;
			continue;
		}

		/* A directory has one name; a second is removed. */
		if (kind == FSCK_INODE_DIR && (volume->state[number] & FSCK_INODE_REACHED) != 0) {
			fsck_problem(volume, 1, "directory %u has a second name \"%.*s\" in directory %u; it is removed", number, namlen, name, directory);
			if (volume->repair) {
				fsck_put32(volume, chunk, pos + FSCK_RECORD_INO, 0);
				*dirty = 1;
			}
			pos += reclen;
			record++;
			continue;
		}

		/* The entry's type follows the inode's kind. */
		wanted = volume->dtype[number];
		if (chunk[pos + FSCK_RECORD_TYPE] != wanted) {
			fsck_problem(volume, 1, "directory %u's entry \"%.*s\" has type %u, but inode %u's kind is %u; corrected",
			    directory, namlen, name, chunk[pos + FSCK_RECORD_TYPE], number, wanted);
			if (volume->repair) {
				chunk[pos + FSCK_RECORD_TYPE] = wanted;
				*dirty = 1;
			}
		}

		/* Counted, and a directory joins the walk. */
		volume->references[number]++;
		if (kind == FSCK_INODE_DIR) {
			volume->parent[number] = directory;
			queue[(*tail)++] = number;
		}
		volume->state[number] |= FSCK_INODE_REACHED;
		pos += reclen;
		record++;
	}

	/* The first record block holds both dot entries. */
	if (first && record < 2U)
		fsck_problem(volume, 0, "directory %u's first record block has no room for \"..\"", directory);
}

/*
 * Checks a dot entry: the record must be "." or ".." naming the inode
 * given; an empty record with room becomes one ("." leaves the rest of
 * the record empty, for ".." to follow).  The name it gives is counted.
 * Returns the record's length.
 */
static uint32_t
fsck_dot_entry(
	struct fsck_volume *volume,
	uint32_t directory,
	uint8_t *chunk,
	uint32_t pos,
	uint32_t reclen,
	const char *dots,
	uint32_t number,
	int *dirty)
{
	uint32_t found;
	uint8_t namlen;
	size_t length;

	length = strlen(dots);
	found = fsck_get32(volume, chunk, pos + FSCK_RECORD_INO);
	namlen = chunk[pos + FSCK_RECORD_NAMLEN];

	/* The name is the dot entry's: only its inode can be wrong. */
	if (namlen == length && memcmp(chunk + pos + FSCK_RECORD_NAME, dots, length) == 0 && found != 0) {
		if (found != number) {
			fsck_problem(volume, 1, "directory %u's \"%s\" names inode %u instead of %u; corrected", directory, dots, found, number);
			if (volume->repair) {
				fsck_put32(volume, chunk, pos + FSCK_RECORD_INO, number);
				*dirty = 1;
			}
		}
		if (chunk[pos + FSCK_RECORD_TYPE] != FSCK_TYPE_DIR && volume->repair) {
			chunk[pos + FSCK_RECORD_TYPE] = FSCK_TYPE_DIR;
			*dirty = 1;
		}
		volume->references[number]++;
		return reclen;
	}

	/* An empty record with room is made the dot entry; "." leaves room after it. */
	if (found == 0 && reclen >= FSCK_RECORD_BYTES(length)) {
		fsck_problem(volume, 1, "directory %u has no \"%s\"; it is written", directory, dots);
		volume->references[number]++;
		if (!volume->repair)
			return reclen;
		*dirty = 1;
		if (length == 1U && reclen >= 2U * FSCK_RECORD_BYTES(2)) {
			fsck_put_record(volume, chunk, pos + FSCK_RECORD_BYTES(1), 0, reclen - FSCK_RECORD_BYTES(1), 0, "");
			reclen = FSCK_RECORD_BYTES(1);
		}
		fsck_put_record(volume, chunk, pos, number, reclen, FSCK_TYPE_DIR, dots);
		return reclen;
	}

	/* Succeeded in learning that the entry cannot be made here. */
	fsck_problem(volume, 0, "directory %u's record %s is \"%.*s\", not \"%s\"", directory, (length == 1U) ? "1" : "2", namlen, (const char *)chunk + pos + FSCK_RECORD_NAME, dots);
	return reclen;
}

/* Writes a record: header and name, the name padded with zeroes to its record bytes. */
static void
fsck_put_record(
	struct fsck_volume *volume,
	uint8_t *chunk,
	uint32_t pos,
	uint32_t number,
	uint32_t reclen,
	uint8_t type,
	const char *name)
{
	size_t length;

	length = strlen(name);

	/* The header, then the name. */
	fsck_put32(volume, chunk, pos + FSCK_RECORD_INO, number);
	fsck_put16(volume, chunk, pos + FSCK_RECORD_RECLEN, (uint16_t)reclen);
	chunk[pos + FSCK_RECORD_TYPE] = type;
	chunk[pos + FSCK_RECORD_NAMLEN] = (uint8_t)length;
	memset(chunk + pos + FSCK_RECORD_NAME, 0, reclen - FSCK_RECORD_NAME);
	memcpy(chunk + pos + FSCK_RECORD_NAME, name, length);
}

/* Reports whether the record at a position holds together inside its record block. */
static int
fsck_record_valid(
	const struct fsck_volume *volume,
	const uint8_t *chunk,
	uint32_t pos)
{
	uint32_t reclen;
	uint32_t number;
	uint8_t namlen;
	uint8_t index;

	/* Room for a header. */
	if (pos > UFS_DIRBLKSIZ - FSCK_RECORD_NAME)
		return 0;
	number = fsck_get32(volume, chunk, pos + FSCK_RECORD_INO);
	reclen = fsck_get16(volume, chunk, pos + FSCK_RECORD_RECLEN);
	namlen = chunk[pos + FSCK_RECORD_NAMLEN];

	/* A length in whole words inside the record block, holding the name. */
	if (reclen < FSCK_RECORD_NAME || (reclen % 4U) != 0 || pos + reclen > UFS_DIRBLKSIZ)
		return 0;
	if (FSCK_RECORD_NAME + (uint32_t)namlen > reclen)
		return 0;

	/* A used record has a name without a slash or a zero byte. */
	if (number != 0) {
		if (namlen == 0)
			return 0;
		for (index = 0; index < namlen; index++) {
			if (chunk[pos + FSCK_RECORD_NAME + index] == '/' || chunk[pos + FSCK_RECORD_NAME + index] == '\0')
				return 0;
		}
	}

	/* Succeeded: the record holds together. */
	return 1;
}

/*
 * Finds /lost+found, or makes it: a directory of one record block in a
 * new block, named in the root.  Returns its inode, or 0 when it cannot be
 * made.
 */
static uint32_t
fsck_lost_found(
	struct fsck_volume *volume)
{
	uint8_t *block;
	uint8_t *raw;
	uint64_t fragment;
	uint32_t number;
	uint64_t now;
	int error;

	/* Found or made before. */
	if (volume->lost_found != 0)
		return volume->lost_found;

	/* A directory named lost+found in the root. */
	number = fsck_find_entry(volume, UFS_ROOT_INO, "lost+found");
	if (number != 0 && number < volume->inode_count && (volume->state[number] & FSCK_INODE_KIND_MASK) == FSCK_INODE_DIR) {
		volume->lost_found = number;
		return number;
	}

	/* A new inode and block. */
	number = fsck_allocate_inode(volume);
	fragment = fsck_allocate_block(volume);
	if (number == 0 || fragment == 0) {
		fprintf(stderr, "fsck: %s: no free inode or block for /lost+found\n", volume->name);
		return 0;
	}

	/* The block: "." and "..", the rest of the record block one record. */
	block = calloc(1, volume->super.bsize);
	if (block == NULL)
		return 0;
	fsck_put_record(volume, block, 0, number, FSCK_RECORD_BYTES(1), FSCK_TYPE_DIR, ".");
	fsck_put_record(volume, block, FSCK_RECORD_BYTES(1), UFS_ROOT_INO, UFS_DIRBLKSIZ - FSCK_RECORD_BYTES(1), FSCK_TYPE_DIR, "..");
	error = fsck_write_block(volume, fragment, block);
	free(block);
	if (error != 0) {
		volume->io_error = error;
		return 0;
	}

	/* The inode: a directory of one record block, owned by root, mode 0700. */
	raw = fsck_inode(volume, number);
	if (raw == NULL)
		return 0;
	now = (uint64_t)time(NULL);
	memset(raw, 0, UFS_DINODE_SIZE);
	fsck_put16(volume, raw, UFS_DI_MODE, (uint16_t)(FSCK_IFDIR | 0700U));
	fsck_put16(volume, raw, UFS_DI_NLINK, 2);
	fsck_put32(volume, raw, UFS_DI_BLKSIZE, volume->super.bsize);
	fsck_put64(volume, raw, UFS_DI_SIZE, UFS_DIRBLKSIZ);
	fsck_put64(volume, raw, UFS_DI_BLOCKS, volume->super.bsize / UFS_SECTOR_SIZE);
	fsck_put64(volume, raw, UFS_DI_ATIME, now);
	fsck_put64(volume, raw, UFS_DI_MTIME, now);
	fsck_put64(volume, raw, UFS_DI_CTIME, now);
	fsck_put64(volume, raw, UFS_DI_BIRTHTIME, now);
	fsck_put32(volume, raw, UFS_DI_GEN, (uint32_t)now);
	fsck_put64(volume, raw, UFS_DI_DB, fragment);
	fsck_inode_dirty(volume);
	volume->state[number] = FSCK_INODE_DIR | FSCK_INODE_REACHED;
	volume->dtype[number] = FSCK_TYPE_DIR;
	volume->parent[number] = UFS_ROOT_INO;

	/* Its name in the root, and the names it gives itself and the root. */
	if (fsck_add_entry(volume, UFS_ROOT_INO, "lost+found", number, FSCK_TYPE_DIR) != 0) {
		fprintf(stderr, "fsck: %s: the root directory has no room for lost+found\n", volume->name);
		fsck_clear_inode(volume, number);
		return 0;
	}
	volume->references[number] += 2U;
	volume->references[UFS_ROOT_INO]++;
	printf("fsck: %s: /lost+found made (inode %u)\n", volume->name, number);

	/* Succeeded: the directory exists. */
	volume->lost_found = number;
	return number;
}

/* Finds a name in a directory: its inode, or 0. */
static uint32_t
fsck_find_entry(
	struct fsck_volume *volume,
	uint32_t directory,
	const char *name)
{
	uint8_t *raw;
	uint64_t size;
	uint64_t offset;
	uint64_t fragment;
	uint32_t pos;
	uint32_t reclen;
	size_t length;
	uint8_t *chunk;

	raw = fsck_inode(volume, directory);
	if (raw == NULL)
		return 0;
	size = fsck_get64(volume, raw, UFS_DI_SIZE);
	length = strlen(name);

	/* Each record block, each record. */
	for (offset = 0; offset + UFS_DIRBLKSIZ <= size; offset += UFS_DIRBLKSIZ) {
		if ((offset % volume->super.bsize) == 0) {
			fragment = fsck_file_block(volume, directory, offset / volume->super.bsize);
			if (fragment == 0 || fragment == UINT64_MAX)
				return 0;
			if (fsck_read_block(volume, fragment, volume->data) != 0)
				return 0;
		}
		chunk = volume->data + offset % volume->super.bsize;
		for (pos = 0; pos < UFS_DIRBLKSIZ; pos += reclen) {
			if (!fsck_record_valid(volume, chunk, pos))
				break;
			reclen = fsck_get16(volume, chunk, pos + FSCK_RECORD_RECLEN);
			if (fsck_get32(volume, chunk, pos + FSCK_RECORD_INO) != 0 && chunk[pos + FSCK_RECORD_NAMLEN] == length &&
			    memcmp(chunk + pos + FSCK_RECORD_NAME, name, length) == 0)
				return fsck_get32(volume, chunk, pos + FSCK_RECORD_INO);
		}
	}

	/* Succeeded in learning the name is not there. */
	return 0;
}

/*
 * Adds a name to a directory: into the first record with room after its
 * own name, else into a new record block in the last block, else into a
 * new block.  Returns 0, ENOSPC, or an I/O error.
 */
static int
fsck_add_entry(
	struct fsck_volume *volume,
	uint32_t directory,
	const char *name,
	uint32_t number,
	uint8_t type)
{
	uint8_t *block;
	uint8_t *raw;
	uint64_t size;
	uint64_t offset;
	uint64_t fragment;
	uint64_t logical;
	uint64_t blocks;
	uint32_t pos;
	uint32_t reclen;
	uint32_t used;
	uint32_t needed;
	int error;

	block = malloc(volume->super.bsize);
	if (block == NULL)
		return ENOMEM;
	raw = fsck_inode(volume, directory);
	if (raw == NULL) {
		free(block);
		return volume->io_error;
	}
	size = fsck_get64(volume, raw, UFS_DI_SIZE);
	needed = FSCK_RECORD_BYTES(strlen(name));

	/* A record with room after its own name. */
	fragment = 0;
	for (offset = 0; offset + UFS_DIRBLKSIZ <= size; offset += UFS_DIRBLKSIZ) {
		if ((offset % volume->super.bsize) == 0) {
			fragment = fsck_file_block(volume, directory, offset / volume->super.bsize);
			if (fragment == 0 || fragment == UINT64_MAX)
				break;
			error = fsck_read_block(volume, fragment, block);
			if (error != 0) {
				free(block);
				return error;
			}
		}
		for (pos = 0; pos < UFS_DIRBLKSIZ; pos += reclen) {
			if (!fsck_record_valid(volume, block + offset % volume->super.bsize, pos))
				break;
			reclen = fsck_get16(volume, block + offset % volume->super.bsize, pos + FSCK_RECORD_RECLEN);
			used = 0;
			if (fsck_get32(volume, block + offset % volume->super.bsize, pos + FSCK_RECORD_INO) != 0)
				used = FSCK_RECORD_BYTES(block[offset % volume->super.bsize + pos + FSCK_RECORD_NAMLEN]);
			if (reclen - used < needed)
				continue;
			if (used != 0)
				fsck_put16(volume, block + offset % volume->super.bsize, pos + FSCK_RECORD_RECLEN, (uint16_t)used);
			fsck_put_record(volume, block + offset % volume->super.bsize, pos + used, number, reclen - used, type, name);
			error = fsck_write_block(volume, fragment, block);
			free(block);
			return error;
		}
	}

	/* A new record block: in the last block when it has room, else in a new block. */
	logical = size / volume->super.bsize;
	if ((size % volume->super.bsize) != 0) {
		fragment = fsck_file_block(volume, directory, logical);
		if (fragment == 0 || fragment == UINT64_MAX || fsck_read_block(volume, fragment, block) != 0) {
			free(block);
			return EIO;
		}
	} else {
		if (logical >= UFS_NDADDR) {
			free(block);
			return ENOSPC;
		}
		fragment = fsck_allocate_block(volume);
		if (fragment == 0) {
			free(block);
			return ENOSPC;
		}
		memset(block, 0, volume->super.bsize);
		raw = fsck_inode(volume, directory);
		if (raw == NULL) {
			free(block);
			return volume->io_error;
		}
		blocks = fsck_get64(volume, raw, UFS_DI_BLOCKS);
		fsck_put64(volume, raw, UFS_DI_DB + logical * 8U, fragment);
		fsck_put64(volume, raw, UFS_DI_BLOCKS, blocks + volume->super.bsize / UFS_SECTOR_SIZE);
		fsck_inode_dirty(volume);
	}
	fsck_put_record(volume, block, (uint32_t)(size % volume->super.bsize), number, UFS_DIRBLKSIZ, type, name);
	error = fsck_write_block(volume, fragment, block);
	free(block);
	if (error != 0)
		return error;

	/* The directory is a record block longer. */
	raw = fsck_inode(volume, directory);
	if (raw == NULL)
		return volume->io_error;
	fsck_put64(volume, raw, UFS_DI_SIZE, size + UFS_DIRBLKSIZ);
	fsck_inode_dirty(volume);

	/* Succeeded: the name is written. */
	return 0;
}

/* Takes the lowest whole block no inode owns, for fsck's own use: its fragment, or 0. */
static uint64_t
fsck_allocate_block(
	struct fsck_volume *volume)
{
	uint64_t start;
	uint32_t group;
	uint32_t local;
	uint32_t part;
	uint32_t fragments;
	int taken;

	/* Each group's data area, block by block. */
	for (group = 0; group < volume->super.ncg; group++) {
		start = fsck_group_start(volume, group);
		fragments = fsck_group_fragments(volume, group);
		local = (volume->super.dblkno + volume->super.frag - 1U) / volume->super.frag * volume->super.frag;
		for (; local + volume->super.frag <= fragments; local += volume->super.frag) {
			taken = 0;
			for (part = 0; part < volume->super.frag; part++) {
				if (fsck_bit(volume->owned, start + local + part))
					taken = 1;
			}
			if (taken)
				continue;
			for (part = 0; part < volume->super.frag; part++)
				fsck_set_bit(volume->owned, start + local + part, 1);
			return start + local;
		}
	}

	/* Succeeded in learning the volume is full. */
	return 0;
}

/* Takes the lowest free inode above the root: its number, or 0. */
static uint32_t
fsck_allocate_inode(
	struct fsck_volume *volume)
{
	uint32_t number;

	/* Each inode after the root. */
	for (number = UFS_ROOT_INO + 1U; number < volume->inode_count; number++) {
		if ((volume->state[number] & FSCK_INODE_KIND_MASK) == FSCK_INODE_FREE)
			return number;
	}

	/* Succeeded in learning every inode is used. */
	return 0;
}

/* Reports the inode a directory's ".." names, or UINT32_MAX when it cannot be read. */
static uint32_t
fsck_dotdot(
	struct fsck_volume *volume,
	uint32_t directory)
{
	uint64_t fragment;
	uint32_t reclen;

	/* The first block's first record block. */
	fragment = fsck_file_block(volume, directory, 0);
	if (fragment == 0 || fragment == UINT64_MAX)
		return UINT32_MAX;
	if (fsck_read_block(volume, fragment, volume->data) != 0)
		return UINT32_MAX;

	/* The second record, when the first holds together. */
	if (!fsck_record_valid(volume, volume->data, 0))
		return UINT32_MAX;
	reclen = fsck_get16(volume, volume->data, FSCK_RECORD_RECLEN);
	if (!fsck_record_valid(volume, volume->data, reclen))
		return UINT32_MAX;

	/* Succeeded: the parent it names. */
	return fsck_get32(volume, volume->data, reclen + FSCK_RECORD_INO);
}
