/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

#include "src/libc/heap.h"

#include <stddef.h>
#include <errno.h>
#include <string.h>

/* The longest "Unknown error N" description, with its terminator. */
#define ERROR_UNKNOWN_SIZE 32

/*
 * One error number and the description strerror gives it.
 *
 * The descriptions follow the wording that other systems print, so that a
 * diagnostic reads the same everywhere.
 */
struct error_description {
	int number;
	const char *text;
};

/*
 * The description of every error number of <uapi/errno.h>, in its order.
 *
 * The table is constant for the life of the program.  An error number
 * added to <uapi/errno.h> needs a row here;
 * plan/ws001/tests/strerror-host-test.sh checks that every one has one.
 */
static const struct error_description error_descriptions[] = {
	{ EDOM, "Numerical argument out of domain" },
	{ ERANGE, "Numerical result out of range" },
	{ EINVAL, "Invalid argument" },
	{ ENOMEM, "Cannot allocate memory" },
	{ EIO, "Input/output error" },
	{ ENOENT, "No such file or directory" },
	{ EINTR, "Interrupted system call" },
	{ ENOSPC, "No space left on device" },
	{ EROFS, "Read-only file system" },
	{ EOVERFLOW, "Value too large for defined data type" },
	{ ENAMETOOLONG, "File name too long" },
	{ ENXIO, "No such device or address" },
	{ ENODEV, "No such device" },
	{ ENOTDIR, "Not a directory" },
	{ EISDIR, "Is a directory" },
	{ EEXIST, "File exists" },
	{ EBUSY, "Device or resource busy" },
	{ ENOTEMPTY, "Directory not empty" },
	{ EBADF, "Bad file descriptor" },
	{ ENOSYS, "Function not implemented" },
	{ EOPNOTSUPP, "Operation not supported" },
	{ ENOEXEC, "Exec format error" },
	{ EFAULT, "Bad address" },
	{ EAGAIN, "Resource temporarily unavailable" },
	{ EACCES, "Permission denied" },
	{ ESRCH, "No such process" },
	{ ECHILD, "No child processes" },
	{ E2BIG, "Argument list too long" },
	{ ENFILE, "Too many open files in system" },
	{ EMSGSIZE, "Message too long" },
	{ ENOBUFS, "No buffer space available" },
	{ ENETDOWN, "Network is down" },
	{ ENETUNREACH, "Network is unreachable" },
	{ EPROTONOSUPPORT, "Protocol not supported" },
	{ EAFNOSUPPORT, "Address family not supported by protocol" },
	{ EADDRINUSE, "Address already in use" },
	{ EADDRNOTAVAIL, "Cannot assign requested address" },
	{ EISCONN, "Transport endpoint is already connected" },
	{ ENOTCONN, "Transport endpoint is not connected" },
	{ ECONNREFUSED, "Connection refused" },
	{ ECONNRESET, "Connection reset by peer" },
	{ ETIMEDOUT, "Connection timed out" },
	{ EHOSTUNREACH, "No route to host" },
	{ EPIPE, "Broken pipe" },
	{ EDESTADDRREQ, "Destination address required" },
	{ EMFILE, "Too many open files" },
	{ EPERM, "Operation not permitted" },
	{ EXDEV, "Invalid cross-device link" },
	{ ESPIPE, "Illegal seek" },
	{ ELOOP, "Too many levels of symbolic links" },
	{ EFBIG, "File too large" },
	{ ENOTTY, "Inappropriate ioctl for device" },
	{ EINPROGRESS, "Operation now in progress" },
	{ EALREADY, "Operation already in progress" },
	{ ECONNABORTED, "Software caused connection abort" },
	{ ENOPROTOOPT, "Protocol not available" },
	{ EMLINK, "Too many links" },
	{ EDEADLK, "Resource deadlock avoided" },
	{ ECANCELED, "Operation canceled" },
	{ ENOTSOCK, "Socket operation on non-socket" },
	{ EILSEQ, "Invalid or incomplete multibyte or wide character" },
	{ ENODATA, "No data available" },
	{ EDQUOT, "Disk quota exceeded" },
	{ ENOMSG, "No message of desired type" },
	{ EIDRM, "Identifier removed" },
	{ ESTALE, "Stale file handle" },
	{ EPROTO, "Protocol error" },
	{ EPFNOSUPPORT, "Protocol family not supported" },
	{ ETXTBSY, "Text file busy" },
	{ EBADMSG, "Bad message" },
	{ EMULTIHOP, "Multihop attempted" },
	{ ENETRESET, "Network dropped connection on reset" },
	{ ENOLCK, "No locks available" },
	{ ENOLINK, "Link has been severed" },
	{ ENOSR, "Out of streams resources" },
	{ ENOSTR, "Device not a stream" },
	{ ENOTRECOVERABLE, "State not recoverable" },
	{ EOWNERDEAD, "Owner died" },
	{ EPROTOTYPE, "Protocol wrong type for socket" },
	{ ESOCKTNOSUPPORT, "Socket type not supported" },
	{ ETIME, "Timer expired" },
};

/*
 * The description strerror returns for a number that is not an error
 * number.  POSIX lets strerror reuse its result, so it is rewritten by the
 * next such call.
 */
static char error_unknown[ERROR_UNKNOWN_SIZE];

static const char *error_text(int error);
static void error_format_unknown(int error, char *buffer, size_t size);

void *
memcpy(void *destination, const void *source, size_t count)
{
	unsigned char *dst = destination;
	const unsigned char *src = source;

	while (count-- != 0)
		*dst++ = *src++;
	return destination;
}

void *
memmove(void *destination, const void *source, size_t count)
{
	unsigned char *dst = destination;
	const unsigned char *src = source;

	if (dst < src) {
		while (count-- != 0)
			*dst++ = *src++;
	} else if (dst > src) {
		dst += count;
		src += count;
		while (count-- != 0)
			*--dst = *--src;
	}
	return destination;
}

void *
memset(void *destination, int value, size_t count)
{
	unsigned char *dst = destination;

	while (count-- != 0)
		*dst++ = (unsigned char)value;
	return destination;
}

void *
memchr(const void *memory, int character, size_t count)
{
	const unsigned char *bytes = memory;
	unsigned char wanted = (unsigned char)character;

	while (count-- != 0) {
		if (*bytes == wanted)
			return (void *)bytes;
		bytes++;
	}
	return NULL;
}

int
memcmp(const void *left, const void *right, size_t count)
{
	const unsigned char *a = left;
	const unsigned char *b = right;

	while (count-- != 0) {
		if (*a != *b)
			return *a < *b ? -1 : 1;
		a++;
		b++;
	}
	return 0;
}

size_t
strlen(const char *string)
{
	const char *end = string;
	while (*end != '\0')
		end++;
	return (size_t)(end - string);
}

size_t
strnlen(const char *string, size_t maximum)
{
	size_t length = 0;
	while (length < maximum && string[length] != '\0')
		length++;
	return length;
}

int
strcmp(const char *left, const char *right)
{
	while (*left != '\0' && *left == *right) {
		left++;
		right++;
	}
	return *(const unsigned char *)left - *(const unsigned char *)right;
}

int
strncmp(const char *left, const char *right, size_t count)
{
	while (count != 0 && *left != '\0' && *left == *right) {
		left++;
		right++;
		count--;
	}
	if (count == 0)
		return 0;
	return *(const unsigned char *)left - *(const unsigned char *)right;
}

char *
strcpy(char *destination, const char *source)
{
	char *result = destination;
	while ((*destination++ = *source++) != '\0')
		;
	return result;
}

char *
strncpy(char *destination, const char *source, size_t count)
{
	char *result = destination;
	while (count != 0 && *source != '\0') {
		*destination++ = *source++;
		count--;
	}
	while (count-- != 0)
		*destination++ = '\0';
	return result;
}

char *
strcat(char *destination, const char *source)
{
	strcpy(destination + strlen(destination), source);
	return destination;
}

char *
strncat(char *destination, const char *source, size_t count)
{
	char *tail = destination + strlen(destination);
	while (count-- != 0 && *source != '\0')
		*tail++ = *source++;
	*tail = '\0';
	return destination;
}

char *
strchr(const char *string, int character)
{
	char wanted = (char)character;
	for (;;) {
		if (*string == wanted)
			return (char *)string;
		if (*string++ == '\0')
			return NULL;
	}
}

char *
strrchr(const char *string, int character)
{
	const char *last = NULL;
	char wanted = (char)character;
	for (;;) {
		if (*string == wanted)
			last = string;
		if (*string++ == '\0')
			return (char *)last;
	}
}

char *
strstr(const char *haystack, const char *needle)
{
	size_t length = strlen(needle);
	if (length == 0)
		return (char *)haystack;
	while (*haystack != '\0') {
		if (strncmp(haystack, needle, length) == 0)
			return (char *)haystack;
		haystack++;
	}
	return NULL;
}

char *
strdup(const char *string)
{
	return heap_strdup_active(string);
}

/*
 * Describes an error number.
 *
 * A number that is not an error number gets "Unknown error" and the number.
 */
char *
strerror(
	int error)
{
	const char *text;

	/* Looks the number up. */
	text = error_text(error);
	if (text == NULL) {
		error_format_unknown(error, error_unknown, sizeof(error_unknown));
		return error_unknown;
	}

	/* Succeeded: the description, which the caller must not change. */
	return (char *)text;
}

/*
 * Describes an error number into the caller's buffer.
 *
 * POSIX's reentrant form: the result is an errno value rather than a
 * pointer.  EINVAL says the number is not an error number, though the
 * buffer still receives "Unknown error" and the number; ERANGE says the
 * buffer was too small, in which case what fits is still left terminated.
 */
int
strerror_r(
	int error,
	char *buffer,
	size_t size)
{
	char unknown[ERROR_UNKNOWN_SIZE];
	const char *text;
	size_t length;
	int result;

	/* Rejects a buffer that cannot even hold a terminator. */
	if (buffer == NULL || size == 0)
		return ERANGE;

	/* Looks the number up; an unknown one is described and refused. */
	result = 0;
	text = error_text(error);
	if (text == NULL) {
		error_format_unknown(error, unknown, sizeof(unknown));
		text = unknown;
		result = EINVAL;
	}

	/* Reports a buffer too small, having filled what fits. */
	length = strlen(text);
	if (length >= size) {
		memcpy(buffer, text, size - 1U);
		buffer[size - 1U] = '\0';
		return ERANGE;
	}

	/* Copies the whole description. */
	memcpy(buffer, text, length + 1U);

	/* Reports an unknown number after describing it. */
	if (result != 0)
		return result;

	/* Succeeded: the buffer holds the description. */
	return 0;
}

/* Finds the description of an error number, or NULL for another number. */
static const char *
error_text(
	int error)
{
	size_t index;
	size_t count;

	/* Searches the table; it is short and read rarely. */
	count = sizeof(error_descriptions) / sizeof(error_descriptions[0]);
	for (index = 0; index < count; index++) {
		if (error_descriptions[index].number == error)
			return error_descriptions[index].text;
	}

	/* Not an error number. */
	return NULL;
}

/* Writes "Unknown error" and a number in decimal into a buffer. */
static void
error_format_unknown(
	int error,
	char *buffer,
	size_t size)
{
	static const char prefix[] = "Unknown error ";
	char digits[16];
	unsigned int magnitude;
	size_t count;
	size_t length;

	/* The digits of the magnitude, least significant first. */
	magnitude = (unsigned int)error;
	if (error < 0)
		magnitude = 0U - (unsigned int)error;
	count = 0;
	do {
		digits[count] = (char)('0' + magnitude % 10U);
		count++;
		magnitude /= 10U;
	} while (magnitude != 0U);

	/* The prefix, the sign, then the digits most significant first. */
	length = 0;
	while (prefix[length] != '\0' && length + 1U < size) {
		buffer[length] = prefix[length];
		length++;
	}

	/* A negative number keeps its sign. */
	if (error < 0 && length + 1U < size) {
		buffer[length] = '-';
		length++;
	}

	/* The digits, as many as fit. */
	while (count > 0 && length + 1U < size) {
		count--;
		buffer[length] = digits[count];
		length++;
	}

	/* Terminates the description. */
	buffer[length] = '\0';
}
