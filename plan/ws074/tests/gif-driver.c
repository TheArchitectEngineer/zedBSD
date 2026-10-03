/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The libgif-compat test driver (ws074-p051).  The same source is built
 * three ways: for the host with the library's sources, for the host
 * against the real giflib (-DGIF_DRIVER_GIFLIB, the reference), and for
 * zedBSD against libgif-compat.so:
 *
 *   host-gif [--callback] IN.gif OUT
 *
 * opens IN.gif (by its name, or with --callback through DGifOpen and a
 * reading callback), reads it with DGifSlurp and prints what a program
 * sees: the screen and its colour map, each image's description, colour
 * map, extension blocks and graphic control block, the extensions after
 * the last image, and the result of DGifSlurp.  OUT gets every image's
 * raster, one after another.  Two builds agree when their outputs are
 * equal byte for byte.
 */

#ifdef GIF_DRIVER_GIFLIB
#include <gif_lib.h>
#else
#include <compat/gif_lib.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_read(GifFileType *gif, GifByteType *buffer, int count);
static void test_print_map(const char *label, const ColorMapObject *map);
static void test_print_blocks(const char *label, const ExtensionBlock *blocks, int count);

/* Reads the command line, the file, and prints it. */
int
main(
	int argc,
	char **argv)
{
	GifFileType *gif;
	GraphicsControlBlock gcb;
	SavedImage *image;
	FILE *input;
	FILE *output;
	const char *in;
	const char *out;
	int callback;
	int error;
	int status;
	int index;
	int differs;

	/* The option, then the two files. */
	callback = 0;
	in = NULL;
	out = NULL;
	for (index = 1; index < argc; index++) {
		differs = strcmp(argv[index], "--callback");
		if (differs == 0) {
			callback = 1;
		} else if (in == NULL) {
			in = argv[index];
		} else {
			out = argv[index];
		}
	}

	/* Both files are needed. */
	if (in == NULL || out == NULL) {
		fprintf(stderr, "usage: host-gif [--callback] IN.gif OUT\n");
		return 2;
	}

	/* The file, by name or through the callback. */
	input = NULL;
	error = 0;
	if (callback) {
		input = fopen(in, "rb");
		if (input == NULL) {
			printf("open: cannot read %s\n", in);
			return 2;
		}

		/* The file through the callback. */
		gif = DGifOpen(input, test_read, &error);
	} else {
		gif = DGifOpenFileName(in, &error);
	}

	/* A file that is not a GIF. */
	if (gif == NULL) {
		printf("open: error %d %s\n", error, GifErrorString(error));
		if (input != NULL)
			fclose(input);
		return 2;
	}

	/* The screen. */
	printf("screen: %d %d resolution=%d background=%d aspect=%d\n", gif->SWidth, gif->SHeight, gif->SColorResolution, gif->SBackGroundColor, gif->AspectByte);
	test_print_map("screen", gif->SColorMap);

	/* Every record, and what the reading said. */
	status = DGifSlurp(gif);
	if (status == GIF_OK) {
		printf("slurp: ok images=%d\n", gif->ImageCount);
	} else {
		printf("slurp: error %d %s images=%d\n", gif->Error, GifErrorString(gif->Error), gif->ImageCount);
	}

	/* The file the rasters go to. */
	output = fopen(out, "wb");
	if (output == NULL) {
		printf("error: cannot write %s\n", out);
		DGifCloseFile(gif, &error);
		return 2;
	}

	/* Each image, and its raster into OUT. */
	for (index = 0; index < gif->ImageCount; index++) {
		image = &gif->SavedImages[index];
		printf("image %d: %d %d %d %d interlace=%d\n", index, image->ImageDesc.Left, image->ImageDesc.Top, image->ImageDesc.Width, image->ImageDesc.Height, (int)image->ImageDesc.Interlace);
		test_print_map("image", image->ImageDesc.ColorMap);
		test_print_blocks("image", image->ExtensionBlocks, image->ExtensionBlockCount);

		/* Its graphic control block, when it has one. */
		status = DGifSavedExtensionToGCB(gif, index, &gcb);
		if (status == GIF_OK)
			printf("gcb %d: disposal=%d input=%d delay=%d transparent=%d\n", index, gcb.DisposalMode, (int)gcb.UserInputFlag, gcb.DelayTime, gcb.TransparentColor);

		/* Its raster. */
		if (image->RasterBits != NULL)
			fwrite(image->RasterBits, 1, (size_t)image->ImageDesc.Width * (size_t)image->ImageDesc.Height, output);
	}

	/* Every raster is written. */
	fclose(output);

	/* The extensions after the last image, then the end. */
	test_print_blocks("file", gif->ExtensionBlocks, gif->ExtensionBlockCount);
	status = DGifCloseFile(gif, &error);
	printf("close: %d %d\n", status, error);
	if (input != NULL)
		fclose(input);

	/* Succeeded: the file is printed. */
	return 0;
}

/* The reading callback: bytes from the stdio stream in UserData. */
static int
test_read(
	GifFileType *gif,
	GifByteType *buffer,
	int count)
{
	size_t got;

	/* The bytes asked for (giflib takes a shorter answer as the end of the file). */
	got = fread(buffer, 1, (size_t)count, (FILE *)gif->UserData);

	/* Succeeded: the bytes given. */
	return (int)got;
}

/* Prints a colour map: its size and colours in hexadecimal. */
static void
test_print_map(
	const char *label,
	const ColorMapObject *map)
{
	int index;

	/* No map. */
	if (map == NULL) {
		printf("%s map: none\n", label);
		return;
	}

	/* Its size and colours. */
	printf("%s map: %d bits=%d sorted=%d ", label, map->ColorCount, map->BitsPerPixel, (int)map->SortFlag);
	for (index = 0; index < map->ColorCount; index++)
		printf("%02x%02x%02x", map->Colors[index].Red, map->Colors[index].Green, map->Colors[index].Blue);
	printf("\n");
}

/* Prints extension blocks: each one's function, size and bytes in hexadecimal. */
static void
test_print_blocks(
	const char *label,
	const ExtensionBlock *blocks,
	int count)
{
	int index;
	int byte;

	/* Each block. */
	for (index = 0; index < count; index++) {
		printf("%s extension: 0x%02x %d ", label, blocks[index].Function, blocks[index].ByteCount);
		for (byte = 0; byte < blocks[index].ByteCount; byte++)
			printf("%02x", blocks[index].Bytes[byte]);
		printf("\n");
	}
}
