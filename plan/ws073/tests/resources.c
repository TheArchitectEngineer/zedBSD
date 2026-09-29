/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * BUG-051 probe: prints the kernel's live object counts
 * (KERN_SYSTEM_GET_RESOURCES on /dev/system) on one line, to follow the
 * open files of the system (the pool holds 2048) across SSH sessions.
 */

#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <uapi/system.h>

/*
 * Prints one snapshot of the live kernel objects.
 */
int
main(
	void)
{
	struct system_resource_info r;
	int fd;

	fd = open("/dev/system", O_RDONLY);
	if (fd < 0 || ioctl(fd, KERN_SYSTEM_GET_RESOURCES, &r) != 0) {
		perror("resources");
		return 1;
	}
	printf("process %llu thread %llu filedesc %llu file %llu pipe %llu socket %llu inode %llu vmspace %llu\n",
	       (unsigned long long)r.process, (unsigned long long)r.thread, (unsigned long long)r.filedesc,
	       (unsigned long long)r.file, (unsigned long long)r.pipe, (unsigned long long)r.socket,
	       (unsigned long long)r.inode, (unsigned long long)r.vmspace);
	return 0;
}
