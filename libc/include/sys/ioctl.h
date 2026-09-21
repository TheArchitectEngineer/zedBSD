/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

#ifndef LIBC_SYS_IOCTL_H
#define LIBC_SYS_IOCTL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>
#include <stddef.h>

/*
 * The terminal requests, and the shapes they are asked with.
 *
 * They are defined with the terminal interface, which is where they belong,
 * but portable software asks for them here: <sys/ioctl.h> is where every
 * Unix has kept them, and a program wanting the size of its window includes
 * this and expects TIOCGWINSZ and struct winsize to be in it.  Nothing new
 * is declared by this; it is the same definitions reached by the name such
 * software already looks under.
 */
#include <uapi/termios.h>

#define KERN_IOC_VOID  0x00000000UL
#define KERN_IOC_OUT   0x40000000UL
#define KERN_IOC_IN    0x80000000UL
#define KERN_IOC_INOUT (KERN_IOC_IN | KERN_IOC_OUT)
#define KERN_IOC(dir, group, nr, size) \
	((unsigned long)(dir) | (((unsigned long)(size) & 0x1fffUL) << 16) | \
	 ((unsigned long)(group) << 8) | (unsigned long)(nr))
#define _IO(g, n)       KERN_IOC(KERN_IOC_VOID, (g), (n), 0)
#define _IOR(g, n, t)   KERN_IOC(KERN_IOC_OUT, (g), (n), sizeof(t))
#define _IOW(g, n, t)   KERN_IOC(KERN_IOC_IN, (g), (n), sizeof(t))
#define _IOWR(g, n, t)  KERN_IOC(KERN_IOC_INOUT, (g), (n), sizeof(t))

#if __ZEDBSD_LEGACY_VISIBLE
int ioctl(int descriptor, unsigned long request, ...);
#endif

#ifdef __cplusplus
}
#endif

#endif
