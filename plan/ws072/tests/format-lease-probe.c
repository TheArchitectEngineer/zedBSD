/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws072-p001 (BUG-060): takes a formatter's lease on FILE the way mkfs does,
 * writes a sector, fsyncs, reads it back through a second descriptor, and
 * prints each step's result.  Build with the cross toolchain:
 *   build/amd64/packages/toolchain/bin/zedbsd-clang -O1 THIS -o format-lease-probe
 * Run in the guest: format-lease-probe FILE (a fully written regular file).
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

static void
step(const char *what, int rc)
{
	printf("%s rc=%d errno=%s\n", what, rc, rc < 0 ? strerror(errno) : "-");
	fflush(stdout);
}

int
main(int argc, char **argv)
{
	struct kern_file_format_reserve request;
	struct stat st;
	char sector[512];
	int fd;
	int reader;
	int rc;

	(void)argc;
	rc = lstat(argv[1], &st);
	step("lstat", rc);
	fd = open(argv[1], O_RDWR | O_NOFOLLOW | O_CLOEXEC);
	step("open", fd);
	memset(&request, 0, sizeof(request));
	request.version = KERN_FILE_FORMAT_VERSION;
	request.struct_size = sizeof(request);
	request.size_bytes = (uint64_t)st.st_size;
	rc = ioctl(fd, KERN_FILE_FORMAT_RESERVE, &request);
	step("reserve", rc);
	memset(sector, 0x5a, sizeof(sector));
	rc = (int)pwrite(fd, sector, sizeof(sector), 65536);
	step("pwrite", rc);
	rc = fsync(fd);
	step("fsync", rc);
	reader = open(argv[1], O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
	step("open-reader", reader);
	rc = (int)pread(reader, sector, sizeof(sector), 65536);
	step("pread", rc);
	rc = close(reader);
	step("close-reader", rc);
	rc = fstat(fd, &st);
	step("fstat", rc);
	rc = close(fd);
	step("close", rc);
	sync();
	printf("sync done\n");
	return 0;
}
