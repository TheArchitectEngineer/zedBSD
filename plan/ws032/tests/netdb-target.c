/*
 * WS032: checks the host and service databases and chroot, on the target.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 */
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int checks;
static int failures;

static void
report(const char *name, int ok, const char *detail)
{
	checks++;
	if (!ok)
		failures++;
	printf("NETDB %s %s%s%s\n", ok ? "ok  " : "FAIL", name,
	    detail != NULL ? ": " : "", detail != NULL ? detail : "");
}

static void
check_services(void)
{
	const struct servent *entry;
	char detail[80];
	unsigned counted;

	/* A name resolves to the port and protocol the file records. */
	entry = getservbyname("ssh", "tcp");
	if (entry == NULL) {
		report("getservbyname-ssh", 0, "not found");
	} else if (ntohs((uint16_t)entry->s_port) != 22 ||
		   strcmp(entry->s_proto, "tcp") != 0) {
		snprintf(detail, sizeof(detail), "port %u proto %s",
		    (unsigned)ntohs((uint16_t)entry->s_port), entry->s_proto);
		report("getservbyname-ssh", 0, detail);
	} else {
		report("getservbyname-ssh", 1, NULL);
	}

	/* A further name for an entry finds the same one. */
	entry = getservbyname("www", "tcp");
	report("getservbyname-alias",
	    entry != NULL && ntohs((uint16_t)entry->s_port) == 80, NULL);

	/* The protocol is part of the key. */
	entry = getservbyname("ssh", "udp");
	report("getservbyname-protocol",
	    entry != NULL && ntohs((uint16_t)entry->s_port) == 22 &&
	    strcmp(entry->s_proto, "udp") == 0, NULL);

	/* A name that is not in the file is not invented. */
	entry = getservbyname("no-such-service", "tcp");
	report("getservbyname-absent", entry == NULL, NULL);

	/* The reverse direction works too. */
	entry = getservbyport((int)htons(80), "tcp");
	report("getservbyport-80",
	    entry != NULL && strcmp(entry->s_name, "http") == 0, NULL);

	/* The whole file can be walked. */
	counted = 0;
	setservent(1);
	while ((entry = getservent()) != NULL)
		counted++;
	endservent();
	snprintf(detail, sizeof(detail), "%u entries", counted);
	report("getservent-walk", counted >= 10, detail);
}

static void
check_hosts(void)
{
	const struct hostent *entry;
	struct in_addr address;
	char detail[80];

	/* An address in text form is its own answer. */
	entry = gethostbyname("127.0.0.1");
	if (entry == NULL || entry->h_addr_list[0] == NULL) {
		report("gethostbyname-literal", 0, "not resolved");
	} else {
		memcpy(&address, entry->h_addr_list[0], sizeof(address));
		snprintf(detail, sizeof(detail), "%s",
		    inet_ntoa(address));
		report("gethostbyname-literal",
		    address.s_addr == htonl(INADDR_LOOPBACK) &&
		    entry->h_addrtype == AF_INET &&
		    entry->h_length == (int)sizeof(struct in_addr), detail);
	}

	/* A name with no answer reports through h_errno, not errno. */
	h_errno = 0;
	entry = gethostbyname("host.invalid.zedbsd.test");
	snprintf(detail, sizeof(detail), "h_errno %d", h_errno);
	report("gethostbyname-unknown", entry == NULL && h_errno != 0, detail);
}

static void
check_chroot(void)
{
	char detail[80];
	pid_t child;
	int status;

	/* The change is made in a child, so the test keeps its own root. */
	child = fork();
	if (child < 0) {
		snprintf(detail, sizeof(detail), "fork errno %d", errno);
		report("chroot", 0, detail);
		return;
	}
	if (child == 0) {
		/* /etc exists, so it can serve as a new root. */
		if (chroot("/etc") != 0)
			_exit(10 + (errno & 0x3f));

		/* What was /etc/services is now /services. */
		if (access("/services", R_OK) != 0)
			_exit(2);

		/* And the old path is no longer reachable. */
		if (access("/etc/services", R_OK) == 0)
			_exit(3);
		_exit(0);
	}
	if (waitpid(child, &status, 0) != child) {
		report("chroot", 0, "no child status");
		return;
	}
	if (!WIFEXITED(status)) {
		report("chroot", 0, "child did not exit");
		return;
	}
	snprintf(detail, sizeof(detail), "child exit %d", WEXITSTATUS(status));
	report("chroot", WEXITSTATUS(status) == 0, detail);
}

int
main(void)
{
	check_services();
	check_hosts();
	check_chroot();
	printf("NETDB verdict: %s (%d/%d)\n",
	    failures == 0 ? "PASS" : "FAIL", checks - failures, checks);
	return failures == 0 ? 0 : 1;
}
