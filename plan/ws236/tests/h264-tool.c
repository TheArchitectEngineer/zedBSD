/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws236-p003: a host tool over rpi4-h264.c.  "units FILE" prints the
 * access units' offsets and sizes; "rewrite FILE OUT" writes FILE with
 * every SPS rewritten by drv_rpi4_h264_sps_no_reorder().
 */

#include "drivers/platform/rpi4/rpi4-h264.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *kern_memset(void *destination, int value, size_t bytes) { return memset(destination, value, bytes); }

static uint8_t *load(const char *path, size_t *bytes)
{
	FILE *file;
	uint8_t *data;
	long size;

	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)size);
	if (fread(data, 1, (size_t)size, file) != (size_t)size)
		return NULL;
	fclose(file);
	*bytes = (size_t)size;
	return data;
}

int main(int argc, char **argv)
{
	uint8_t *stream;
	uint8_t sps[DRV_RPI4_H264_SPS_MAX];
	size_t bytes;
	size_t offset;
	size_t end;
	size_t start;
	size_t stop;
	size_t written;
	size_t count;
	FILE *out;
	int error;

	if (argc < 3)
		return 2;
	stream = load(argv[2], &bytes);
	if (stream == NULL)
		return 2;
	if (strcmp(argv[1], "units") == 0) {
		offset = 0;
		count = 0;
		while (drv_rpi4_h264_access_unit(stream, bytes, offset, &end) == 0) {
			printf("unit %zu offset %zu bytes %zu\n", count, offset, end - offset);
			count++;
			offset = end;
		}
		printf("units %zu\n", count);
		return 0;
	}
	if (strcmp(argv[1], "rewrite") == 0 && argc == 4) {
		out = fopen(argv[3], "wb");
		offset = 0;
		while (drv_rpi4_h264_next_nal(stream, bytes, offset, &start, &stop) == 0) {
			if ((stream[start] & 0x1f) == 7) {
				error = drv_rpi4_h264_sps_no_reorder(stream + start, stop - start, sps, sizeof(sps), &written);
				if (error != 0) {
					printf("rewrite failed %d\n", error);
					return 1;
				}
				fwrite(sps, 1, written, out);
			} else {
				fwrite("\0\0\0\1", 1, 4, out);
				fwrite(stream + start, 1, stop - start, out);
			}
			offset = stop;
		}
		fclose(out);
		return 0;
	}
	return 2;
}
