/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * networkd's IPv6 (ws130-p006): each wired interface's IPv6 from net.conf
 * (on or off, its static addresses) and its link-local address (RFC 7217)
 * when it comes up, and the Router Advertisements the kernel publishes on
 * an AF_INET6 route socket: the SLAAC addresses (stable and temporary), the
 * default route, and the DNS servers (RDNSS, after the IPv4 ones in
 * resolv.conf, H5).
 */

#ifndef NETWORKD_IPV6_H
#define NETWORKD_IPV6_H

int networkd_ipv6_open(void);
int networkd_ipv6_events(int descriptor);
void networkd_ipv6_start(void);
int networkd_ipv6_link_local(const char *name);

#endif
