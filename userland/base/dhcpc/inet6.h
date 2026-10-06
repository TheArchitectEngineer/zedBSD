/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * `dhcpc -6` (ws130-p007, plan/ws130/phase001/phase.md section 5): DHCPv6
 * on one interface, once.  Without -i, an address (IA_NA) by Solicit,
 * Advertise, Request and Reply, or by Renew and Reply when the interface
 * has a lease from before; with -i, only the DNS servers and search list
 * by Information-Request and Reply.  It records when to run again
 * (/var/db/dhcpc/IF.dhcp6: T1, or the information refresh time), which
 * networkd reads.
 */

#ifndef DHCPC_INET6_H
#define DHCPC_INET6_H

int dhcpc_inet6(const char *interface, int information, int resolver, unsigned timeout_seconds, int verbose);

#endif
