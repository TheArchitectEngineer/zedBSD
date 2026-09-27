/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The network side of zdesktop-browser (plan/ws074/design.md §9).  The
 * first part is the URL: the WHATWG URL Standard's parser and serializer
 * (url.c), its hosts (host.c: domains, IPv4 and IPv6 addresses, the
 * punycode of internationalized domains), and data: URLs (data.c).
 *
 * A URL's parts are kept as they are serialized: ASCII strings, already
 * percent-encoded, which the caller owns through the URL and frees with
 * net_url_release.
 */

#ifndef ZDESKTOP_BROWSER_NET_H
#define ZDESKTOP_BROWSER_NET_H

#include "base/base.h"

/*
 * A parsed URL.
 *
 * scheme is in lower case without its colon.  host is NULL for a URL
 * without one, and the empty string for an empty host (file:///).  port
 * is -1 when there is none (or it is the scheme's default).  path is the
 * serialized path: the segments each after a slash, or, when opaque_path
 * is set, the opaque path as it is (mailto:, data:).  query and fragment
 * are NULL when the URL has none (they may be empty).
 */
struct net_url {
	char *scheme;
	char *username;
	char *password;
	char *host;
	int port;
	char *path;
	int opaque_path;
	char *query;
	char *fragment;
};

/*
 * The parts of a URL the URL interface reports (net_url_component).
 */
enum net_url_part {
	NET_URL_HREF,
	NET_URL_ORIGIN,
	NET_URL_PROTOCOL,
	NET_URL_USERNAME,
	NET_URL_PASSWORD,
	NET_URL_HOST,
	NET_URL_HOSTNAME,
	NET_URL_PORT,
	NET_URL_PATHNAME,
	NET_URL_SEARCH,
	NET_URL_HASH
};

/*
 * A data: URL's body: its MIME type (serialized, such as
 * "text/plain;charset=US-ASCII") and its bytes.
 */
struct net_data {
	struct wb_buffer mime;
	struct wb_buffer body;
};

/* URLs (url.c). */
int net_url_parse(const char *input, size_t length, const struct net_url *base, struct net_url *url);
void net_url_release(struct net_url *url);
int net_url_is_special(const struct net_url *url);
int net_url_default_port(const char *scheme);
int net_url_serialize(const struct net_url *url, int exclude_fragment, struct wb_buffer *out);
int net_url_component(const struct net_url *url, int part, struct wb_buffer *out);
int net_url_file_path(const struct net_url *url, struct wb_buffer *out);
int net_url_from_file_path(const char *path, struct wb_buffer *out);
int net_percent_decode(const char *text, size_t length, struct wb_buffer *out);

/* Hosts (host.c). */
int net_host_parse(const char *input, size_t length, int opaque, struct wb_buffer *out);

/* data: URLs (data.c). */
int net_data_parse(const struct net_url *url, struct net_data *data);
void net_data_release(struct net_data *data);

#endif
