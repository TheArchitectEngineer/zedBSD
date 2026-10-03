/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws073-p053 (BUG-163): writes files in place and fsyncs them, and checks
 * afterwards that each file holds one whole generation, so that a crash
 * cut inside an fsync shows whether the content that fsync covered reached
 * the disk.
 *
 *   syncprobe setup DIR N            make DIR/f1..fN (one block each, generation 0), each followed by a directory
 *                                    DIR/d1..dN, so that each file's block lies beside a directory's block
 *   syncprobe write DIR N SECONDS    write each file in turn in place with the next generation and fsync() it
 *   syncprobe check DIR N            read the files back
 *
 * Every sector of a file carries a stamp: a magic number, the file's
 * number, its generation and the sector's index.  check prints, for each
 * file, "SYNCPROBE file=K gen=G" when all its sectors carry generation G,
 * or "SYNCPROBE file=K torn" with the generations it found, then
 * "SYNCPROBE torn=T files=N".  write prints "SYNCPROBE wrote=W" at the end.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* The bytes of one file: one block of the volume. */
#define SYNC_FILE_BYTES	8192U

/* The bytes of one stamped sector. */
#define SYNC_SECTOR	512U

/* The magic number of a stamp ("SYNC"). */
#define SYNC_MAGIC	0x434e5953U

/* The most files a run takes. */
#define SYNC_FILES_MAX	64UL

static void sync_stamp(unsigned char *buffer, unsigned long file, unsigned long generation);
static int sync_write_file(const char *directory, unsigned long file, unsigned long generation, int create);
static int sync_setup(const char *directory, unsigned long files);
static int sync_write(const char *directory, unsigned long files, unsigned long seconds);
static int sync_check(const char *directory, unsigned long files);
static uint32_t sync_get32(const unsigned char *bytes);

/*
 * Runs one mode of the probe.
 */
int
main(
	int argc,
	char **argv)
{
	unsigned long files;
	unsigned long seconds;
	int same;

	/* Every mode names the folder and the number of files. */
	if (argc < 4) {
		fprintf(stderr, "usage: syncprobe setup|check DIR N, or syncprobe write DIR N SECONDS\n");
		return 2;
	}

	/* Takes the number of files, within the bound. */
	files = strtoul(argv[3], NULL, 10);
	if (files == 0 || files > SYNC_FILES_MAX) {
		fprintf(stderr, "syncprobe: N is 1 to %lu\n", SYNC_FILES_MAX);
		return 2;
	}

	/* The setup. */
	same = strcmp(argv[1], "setup");
	if (same == 0)
		return sync_setup(argv[2], files);

	/* The check. */
	same = strcmp(argv[1], "check");
	if (same == 0)
		return sync_check(argv[2], files);

	/* Anything but the writes, which also take a time, is a mistake. */
	same = strcmp(argv[1], "write");
	if (same != 0 || argc != 5) {
		fprintf(stderr, "syncprobe: unknown mode %s\n", argv[1]);
		return 2;
	}

	/* The seconds the writes go on for. */
	seconds = strtoul(argv[4], NULL, 10);

	/* The writes; their status is the probe's. */
	return sync_write(argv[2], files, seconds);
}

/* Stamps every sector of a file's buffer with the file and the generation. */
static void
sync_stamp(
	unsigned char *buffer,
	unsigned long file,
	unsigned long generation)
{
	unsigned char *sector;
	uint32_t words[4];
	unsigned index;

	/* Each sector: the stamp, then the generation's low byte as filler. */
	for (index = 0; index < SYNC_FILE_BYTES / SYNC_SECTOR; index++) {
		sector = buffer + index * SYNC_SECTOR;
		memset(sector, (int)(generation & 0xffU), SYNC_SECTOR);
		words[0] = SYNC_MAGIC;
		words[1] = (uint32_t)file;
		words[2] = (uint32_t)generation;
		words[3] = index;
		memcpy(sector, words, sizeof(words));
	}
}

/* Writes one file in place (or makes it) with a generation and fsyncs it; returns 0 or an errno value. */
static int
sync_write_file(
	const char *directory,
	unsigned long file,
	unsigned long generation,
	int create)
{
	static unsigned char buffer[SYNC_FILE_BYTES];
	char path[512];
	ssize_t written;
	int descriptor;
	int flags;
	int result;

	/* The file's path and its stamped content. */
	(void)snprintf(path, sizeof(path), "%s/f%lu", directory, file);
	sync_stamp(buffer, file, generation);

	/* Opens it, made the first time and never truncated after. */
	flags = O_WRONLY;
	if (create)
		flags |= O_CREAT | O_TRUNC;
	descriptor = open(path, flags, 0644);
	if (descriptor < 0)
		return errno;

	/* Writes the whole block from its start. */
	written = pwrite(descriptor, buffer, sizeof(buffer), 0);
	if (written != (ssize_t)sizeof(buffer)) {
		result = EIO;
		if (written < 0)
			result = errno;
		(void)close(descriptor);
		return result;
	}

	/* Makes it durable. */
	result = fsync(descriptor);
	if (result != 0) {
		result = errno;
		(void)close(descriptor);
		return result;
	}

	/* Closes it. */
	result = close(descriptor);
	if (result != 0)
		return errno;

	/* Succeeded: the file holds the generation. */
	return 0;
}

/* Makes the files, each followed by a directory; returns 0 or 1. */
static int
sync_setup(
	const char *directory,
	unsigned long files)
{
	char path[512];
	unsigned long file;
	int error;
	int result;

	/* One file, then one directory, so that their blocks lie side by side. */
	for (file = 1; file <= files; file++) {
		/* The file at generation 0. */
		error = sync_write_file(directory, file, 0, 1);
		if (error != 0) {
			fprintf(stderr, "syncprobe: f%lu: %s\n", file, strerror(error));
			return 1;
		}

		/* The directory after it. */
		(void)snprintf(path, sizeof(path), "%s/d%lu", directory, file);
		result = mkdir(path, 0755);
		if (result != 0) {
			fprintf(stderr, "syncprobe: %s: %s\n", path, strerror(errno));
			return 1;
		}
	}

	/* Reports the setup. */
	printf("SYNCPROBE setup files=%lu\n", files);

	/* Succeeded: the files and the directories are made. */
	return 0;
}

/* Writes the files in turn, each with the next generation, for some seconds; returns 0 or 1. */
static int
sync_write(
	const char *directory,
	unsigned long files,
	unsigned long seconds)
{
	unsigned long generation;
	unsigned long file;
	unsigned long wrote;
	time_t end;
	time_t now;
	int error;

	/* Writes until the time is up, a whole round of the files at a time. */
	end = time(NULL) + (time_t)seconds;
	wrote = 0;
	for (generation = 1; ; generation++) {
		/* Stops once the time is up. */
		now = time(NULL);
		if (now >= end)
			break;

		/* One round: every file at this generation. */
		for (file = 1; file <= files; file++) {
			error = sync_write_file(directory, file, generation, 0);
			if (error != 0) {
				fprintf(stderr, "syncprobe: f%lu: %s\n", file, strerror(error));
				return 1;
			}

			/* Counts the durable write. */
			wrote++;
		}
	}

	/* Reports how many writes were made durable. */
	printf("SYNCPROBE wrote=%lu\n", wrote);

	/* Succeeded. */
	return 0;
}

/* Reads a little-endian 32-bit word. */
static uint32_t
sync_get32(
	const unsigned char *bytes)
{
	/* Assembles the word from its bytes, lowest first. */
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

/* Reads the files back and reports each one's generation; returns 0 when none is torn, else 1. */
static int
sync_check(
	const char *directory,
	unsigned long files)
{
	static unsigned char buffer[SYNC_FILE_BYTES];
	const unsigned char *sector;
	char path[512];
	unsigned long file;
	unsigned long torn;
	unsigned long first;
	unsigned long generation;
	uint32_t magic;
	uint32_t number;
	uint32_t place;
	ssize_t got;
	unsigned index;
	int descriptor;
	int whole;

	/* Each file in turn. */
	torn = 0;
	for (file = 1; file <= files; file++) {
		/* Reads the file. */
		(void)snprintf(path, sizeof(path), "%s/f%lu", directory, file);
		descriptor = open(path, O_RDONLY);
		if (descriptor < 0) {
			printf("SYNCPROBE file=%lu missing\n", file);
			torn++;
			continue;
		}

		/* Reads its block. */
		memset(buffer, 0, sizeof(buffer));
		got = pread(descriptor, buffer, sizeof(buffer), 0);
		(void)close(descriptor);

		/* A short file is torn. */
		if (got != (ssize_t)sizeof(buffer)) {
			printf("SYNCPROBE file=%lu short=%ld\n", file, (long)got);
			torn++;
			continue;
		}

		/* Every sector carries the file, its index, and the first sector's generation. */
		whole = 1;
		first = sync_get32(buffer + 8);
		printf("SYNCPROBE file=%lu sectors", file);
		for (index = 0; index < SYNC_FILE_BYTES / SYNC_SECTOR; index++) {
			sector = buffer + index * SYNC_SECTOR;
			magic = sync_get32(sector);
			number = sync_get32(sector + 4);
			generation = sync_get32(sector + 8);
			place = sync_get32(sector + 12);

			/* A sector without the stamp shows as -1. */
			if (magic != SYNC_MAGIC)
				generation = (unsigned long)-1;
			printf(" %ld", (long)generation);

			/* A sector of another generation, file or place tears the file. */
			if (generation != first)
				whole = 0;
			else if (number != file)
				whole = 0;
			else if (place != index)
				whole = 0;
		}

		/* Ends the line of sectors. */
		printf("\n");

		/* Reports the file. */
		if (whole) {
			printf("SYNCPROBE file=%lu gen=%lu\n", file, first);
		} else {
			printf("SYNCPROBE file=%lu torn\n", file);
			torn++;
		}
	}

	/* Reports the count. */
	printf("SYNCPROBE torn=%lu files=%lu\n", torn, files);

	/* Reports a torn file in the status. */
	if (torn != 0)
		return 1;

	/* Succeeded: every file holds one whole generation. */
	return 0;
}
