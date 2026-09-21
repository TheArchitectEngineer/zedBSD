/* WS032: the resolver interface, on the target.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib */
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <resolv.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

static void check(const char *what, int ok)
{
	printf("RES %-4s %s\n", ok ? "ok" : "FAIL", what);
	if (!ok)
		failures++;
}

/* The fingerprints the made-up server will report. */
static const unsigned char first_print[22] = {
	1, 1, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11,
	0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xa0, 0xb0,
	0xc0, 0xd0
};
static const unsigned char second_print[22] = {
	4, 2, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
	0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12,
	0x13, 0x14
};

/*
 * Answers one question with two records of the type asked for and one
 * signature over them, using a pointer back to the question's name so that
 * the reader has to follow the compression a real reply uses.
 */
static void serve(int fd)
{
	unsigned char request[512], reply[512];
	struct sockaddr_in from;
	socklen_t from_length;
	ssize_t count;
	size_t question, at;
	unsigned type;
	int served;

	for (served = 0; served < 4; served++) {
		from_length = sizeof(from);
		count = recvfrom(fd, request, sizeof(request), 0,
				 (struct sockaddr *)&from, &from_length);
		if (count < 12)
			return;

		/* The question runs from the header to its type and class. */
		question = 12;
		while (question < (size_t)count && request[question] != 0)
			question += 1U + request[question];
		question += 5U;
		if (question > (size_t)count)
			return;
		type = (unsigned)((request[question - 4] << 8) |
				  request[question - 3]);

		memcpy(reply, request, question);
		reply[2] = 0x85;	/* An answer, authoritative, recursion. */
		reply[3] = 0xa0;	/* Recursion available, and checked. */
		reply[6] = 0;
		reply[7] = 3;		/* Three records follow. */
		reply[8] = 0; reply[9] = 0;
		reply[10] = 0; reply[11] = 0;
		at = question;

#define PUT_HEADER(rrtype, length)					\
		do {							\
			reply[at++] = 0xc0; reply[at++] = 0x0c;		\
			reply[at++] = (unsigned char)((rrtype) >> 8);	\
			reply[at++] = (unsigned char)((rrtype) & 0xff);	\
			reply[at++] = 0; reply[at++] = 1;		\
			reply[at++] = 0; reply[at++] = 0;		\
			reply[at++] = 0x0e; reply[at++] = 0x10;		\
			reply[at++] = (unsigned char)((length) >> 8);	\
			reply[at++] = (unsigned char)((length) & 0xff);	\
		} while (0)

		PUT_HEADER(type, sizeof(first_print));
		memcpy(reply + at, first_print, sizeof(first_print));
		at += sizeof(first_print);
		PUT_HEADER(type, sizeof(second_print));
		memcpy(reply + at, second_print, sizeof(second_print));
		at += sizeof(second_print);

		/* A signature, which says which type it covers. */
		PUT_HEADER(46, 6);
		reply[at++] = (unsigned char)(type >> 8);
		reply[at++] = (unsigned char)(type & 0xff);
		reply[at++] = 's'; reply[at++] = 'i';
		reply[at++] = 'g'; reply[at++] = '!';

		(void)sendto(fd, reply, at, 0, (struct sockaddr *)&from,
			     from_length);
	}
}

int main(void)
{
	unsigned char message[512];
	unsigned char answer[1024];
	char name[NS_MAXDNAME];
	struct sockaddr_in bound;
	struct rrsetinfo *set;
	socklen_t length;
	pid_t child;
	int fd, rc;

	/* The parts that need no server at all. */
	rc = res_init();
	check("the resolver starts up", rc == 0 && _res.nscount >= 1 &&
	      (_res.options & RES_INIT) != 0);

	rc = res_mkquery(QUERY, "www.example.com", C_IN, T_A, NULL, 0, NULL,
			 message, (int)sizeof(message));
	check("a question is written in the form the protocol carries",
	      rc == 12 + 17 + 4 &&
	      message[12] == 3 && memcmp(message + 13, "www", 3) == 0 &&
	      message[16] == 7 && message[24] == 3 && message[28] == 0 &&
	      ns_get16(message + 29) == (unsigned)T_A &&
	      ns_get16(message + 31) == (unsigned)C_IN);

	check("the numbers go in and come out the same way",
	      (ns_put16(0x1234U, message), ns_get16(message) == 0x1234U) &&
	      (ns_put32(0x89abcdefUL, message),
	       ns_get32(message) == 0x89abcdefUL));

	/* A name written out, then the same name reached through a pointer. */
	{
		unsigned char packet[64];

		memset(packet, 0, sizeof(packet));
		packet[12] = 3; memcpy(packet + 13, "foo", 3);
		packet[16] = 7; memcpy(packet + 17, "example", 7);
		packet[24] = 0;
		packet[25] = 0xc0; packet[26] = 0x0c;

		rc = dn_expand(packet, packet + sizeof(packet), packet + 12,
			       name, (int)sizeof(name));
		check("a name written out is read back",
		      rc == 13 && strcmp(name, "foo.example") == 0);
		rc = dn_expand(packet, packet + sizeof(packet), packet + 25,
			       name, (int)sizeof(name));
		check("a name reached through a pointer is read back",
		      rc == 2 && strcmp(name, "foo.example") == 0);
		check("skipping a name reports its length",
		      dn_skipname(packet + 12, packet + sizeof(packet)) == 13);

		/* A pointer that leads forward would let a reply loop. */
		packet[25] = 0xc0; packet[26] = 25;
		check("a pointer that does not lead backwards is refused",
		      dn_expand(packet, packet + sizeof(packet), packet + 25,
				name, (int)sizeof(name)) == -1);
		packet[25] = 0xc0; packet[26] = 40;
		check("a pointer into the middle of nothing is refused",
		      dn_expand(packet, packet + sizeof(packet), packet + 25,
				name, (int)sizeof(name)) == -1);
	}

	/* A server of our own, so the whole path can be walked. */
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	memset(&bound, 0, sizeof(bound));
	bound.sin_family = AF_INET;
	bound.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	bound.sin_port = 0;
	if (fd < 0 || bind(fd, (struct sockaddr *)&bound, sizeof(bound)) != 0) {
		printf("RES FAIL cannot make a server to ask\n");
		return 1;
	}
	length = sizeof(bound);
	(void)getsockname(fd, (struct sockaddr *)&bound, &length);
	printf("RES info asking a server on port %u\n",
	       (unsigned)ntohs(bound.sin_port));

	child = fork();
	if (child == 0) {
		serve(fd);
		_exit(0);
	}
	close(fd);

	_res.nscount = 1;
	_res.nsaddr_list[0] = bound;
	_res.retry = 1;
	_res.retrans = 3;

	rc = res_query("host.example.", C_IN, T_SSHFP, answer,
		       (int)sizeof(answer));
	check("a query reaches the server and the reply comes back",
	      rc > 12 && ns_get16(answer + 6) == 3U);

	rc = getrrsetbyname("host.example.", C_IN, T_SSHFP, 0, &set);
	check("a whole set of records is reported", rc == ERRSET_SUCCESS);
	if (rc == ERRSET_SUCCESS) {
		check("the records of the type asked for are all there",
		      set->rri_nrdatas == 2 &&
		      set->rri_rdatas[0].rdi_length == sizeof(first_print) &&
		      memcmp(set->rri_rdatas[0].rdi_data, first_print,
			     sizeof(first_print)) == 0 &&
		      memcmp(set->rri_rdatas[1].rdi_data, second_print,
			     sizeof(second_print)) == 0);
		check("the signature over them is reported separately",
		      set->rri_nsigs == 1 &&
		      set->rri_sigs[0].rdi_length == 6 &&
		      memcmp(set->rri_sigs[0].rdi_data + 2, "sig!", 4) == 0);
		check("the name is read through the reply's compression",
		      set->rri_name != NULL &&
		      strcmp(set->rri_name, "host.example") == 0);
		check("the time to live and the class are reported",
		      set->rri_ttl == 3600U && set->rri_rdclass == C_IN &&
		      set->rri_rdtype == T_SSHFP);
		check("the server's word that it checked them is carried",
		      (set->rri_flags & RRSET_VALIDATED) != 0);
		freerrset(set);
	} else {
		check("the records of the type asked for are all there", 0);
		check("the signature over them is reported separately", 0);
		check("the name is read through the reply's compression", 0);
		check("the time to live and the class are reported", 0);
		check("the server's word that it checked them is carried", 0);
	}

	check("arguments that make no sense are refused",
	      getrrsetbyname(NULL, C_IN, T_A, 0, &set) == ERRSET_INVAL &&
	      getrrsetbyname("x", C_IN, T_A, 99, &set) == ERRSET_INVAL);

	/* The server waits for more questions, so it is told to stop. */
	(void)kill(child, SIGTERM);
	(void)waitpid(child, NULL, 0);

	/* With nobody listening the call must give up, not wait for ever. */
	_res.nsaddr_list[0].sin_port = htons(1);
	_res.retry = 1;
	_res.retrans = 1;
	h_errno = 0;
	check("a server that does not answer is given up on",
	      res_query("host.example.", C_IN, T_A, answer,
			(int)sizeof(answer)) == -1 && h_errno == TRY_AGAIN);

	printf("RES verdict: %s (%d failures)\n", failures ? "FAIL" : "PASS",
	       failures);
	return failures != 0;
}
