/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The printers (ws145-p003, plan/ws145/design.md section 4; keiland-backend.h):
 * the same on every system.
 *
 * The settings file holds the printers, a line each, the next number and
 * the default:
 *
 *   # Keiland printers
 *   next-id 3
 *   printer 1 ipp 192.168.1.20 631 /ipp/print Office Printer
 *   printer 2 lpd 192.168.1.30 515 lp 192.168.1.30 (LPD)
 *   default 1
 *
 * A change reads the file again under its lock (the file beside it named
 * .lock), changes what it changes and writes it through a file renamed
 * over it, so that two sessions of the user do not undo each other; a file
 * another session changed is read again when its time changes.
 *
 * The jobs go to keiland-printd (the daemon), started with posix_spawn
 * when there is something to do, its socket on its descriptor 3; it is
 * told JOB (with the document's descriptor), CANCEL, NAME and BYE, and
 * answers a line each (printd.h).  The socket does not block: what cannot
 * be sent waits in a queue.  The jobs not ended are at most 16; the last
 * 16 ended are kept for the lists.
 */

#include "keiland-backend.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <dirent.h>
#include <unistd.h>

/* The longest line of the daemon's protocol, and the most lines waiting to be sent. */
#define PRINT_LINE_MAX		1024U
#define PRINT_QUEUE_MAX		64U

/* The most jobs not ended, the results waiting, and the names asked. */
#define PRINT_ACTIVE_MAX	16U
#define PRINT_RESULTS_MAX	32U
#define PRINT_NAMES_MAX		16U

/* The default ports and paths. */
#define PRINT_IPP_PORT		631U
#define PRINT_LPD_PORT		515U
#define PRINT_IPP_PATH		"/ipp/print"
#define PRINT_LPD_QUEUE		"lp"

/* The environment's variables handed to the daemon at most. */
#define PRINT_ENVIRONMENT_MAX	64U

/* The environment, for the daemon. */
extern char **environ;

/* A line waiting to be sent: its bytes, and the descriptor that goes with it (-1 for none) and the job it is. */
struct print_line {
	char text[PRINT_LINE_MAX];
	size_t length;
	int fd;
	uint32_t job;
};

/* A job and what the backend holds for it: its document until the daemon took it, and whether its JOB line went. */
struct print_job {
	struct kl_backend_print_job job;
	int fd;
	int sent;
	uint64_t order;
};

/* An answer waiting to be taken. */
struct print_result {
	uint32_t request;
	int error;
	unsigned saved;
};

/* A name asked of the daemon for a printer added. */
struct print_name {
	uint32_t seq;
	uint32_t printer;
};

/*
 * The printers: the settings file and its time when read, the printers
 * and the default, the jobs, the answers, the daemon (its pid, socket,
 * spool, the bytes of a line, the commands sent), the lines waiting, the
 * names asked, and the next numbers.
 */
struct kl_backend_print {
	char config[512];
	char runtime[512];
	char program[512];
	struct timespec config_time;
	struct kl_backend_printer printers[KL_BACKEND_PRINTERS_MAX];
	size_t printer_count;
	uint32_t next_id;
	struct print_job jobs[KL_BACKEND_PRINT_JOBS_MAX];
	size_t job_count;
	struct print_result results[PRINT_RESULTS_MAX];
	size_t result_count;
	pid_t daemon;
	int socket;
	char spool[512];
	char input[PRINT_LINE_MAX];
	size_t input_length;
	unsigned long commands;
	struct print_line queue[PRINT_QUEUE_MAX];
	size_t queue_count;
	struct print_name names[PRINT_NAMES_MAX];
	size_t name_count;
	uint32_t next_request;
	uint32_t next_job;
	uint32_t next_seq;
	uint64_t next_order;
	unsigned changed;
};

static int print_load(struct kl_backend_print *print);
static int print_save(struct kl_backend_print *print);
static int print_lock(const struct kl_backend_print *print);
static void print_unlock(int fd);
static struct kl_backend_printer *print_printer(struct kl_backend_print *print, uint32_t id);
static struct print_job *print_find_job(struct kl_backend_print *print, uint32_t job);
static struct print_job *print_new_job(struct kl_backend_print *print);
static void print_end_job(struct kl_backend_print *print, struct print_job *job, unsigned state, const char *detail);
static void print_result(struct kl_backend_print *print, uint32_t request, int error, unsigned saved);
static int print_start(struct kl_backend_print *print);
static void print_stopped(struct kl_backend_print *print);
static void print_send(struct kl_backend_print *print, int fd, uint32_t job, const char *format, ...) __attribute__((format(printf, 4, 5)));
static void print_flush(struct kl_backend_print *print);
static void print_read(struct kl_backend_print *print);
static void print_line(struct kl_backend_print *print, char *line);
static void print_job_state(struct kl_backend_print *print, uint32_t number, const char *state, const char *detail);
static void print_named(struct kl_backend_print *print, uint32_t seq, char *rest);
static void print_remove_spool(const char *dir);
static int print_host_ok(const char *host);
static void print_copy(char *to, size_t size, const char *from);

/*
 * Starts the printers from the settings file.
 */
struct kl_backend_print *
kl_backend_print_open(
	const char *config_path,
	const char *runtime_dir,
	const char *program)
{
	struct kl_backend_print *print;
	const char *runtime;

	/* The state, no daemon yet. */
	print = calloc(1, sizeof(*print));
	if (print == NULL)
		return NULL;
	print->socket = -1;
	print->daemon = -1;
	print->next_id = 1;
	print->next_request = 1;
	print->next_job = 1;
	print->next_seq = 1;
	print_copy(print->config, sizeof(print->config), config_path);
	print_copy(print->program, sizeof(print->program), program);
	runtime = runtime_dir;
	if (runtime == NULL)
		runtime = getenv("XDG_RUNTIME_DIR");
	if (runtime != NULL)
		print_copy(print->runtime, sizeof(print->runtime), runtime);

	/* The printers kept. */
	(void)print_load(print);
	return print;
}

/*
 * Stops the printers: the daemon's socket shut (it ends and removes its
 * spool), the documents held closed.
 */
void
kl_backend_print_close(
	struct kl_backend_print *print)
{
	size_t index;

	/* Nothing open. */
	if (print == NULL)
		return;

	/* The documents and the lines waiting. */
	for (index = 0; index < print->job_count; index++) {
		if (print->jobs[index].fd >= 0)
			(void)close(print->jobs[index].fd);
	}
	for (index = 0; index < print->queue_count; index++) {
		if (print->queue[index].fd >= 0)
			(void)close(print->queue[index].fd);
	}

	/* The daemon's socket: its end ends the daemon. */
	if (print->socket >= 0)
		(void)close(print->socket);
	free(print);
}

/*
 * Tells whether the daemon's program can be run.
 */
int
kl_backend_print_can(
	const struct kl_backend_print *print)
{
	int status;

	/* The program, executable. */
	if (print == NULL)
		return 0;
	status = access(print->program, X_OK);
	return status == 0;
}

/*
 * Reads what the daemon said, sends what waits, and reads the settings
 * file again when another session changed it.
 */
int
kl_backend_print_update(
	struct kl_backend_print *print,
	unsigned *changed)
{
	struct stat status;
	int error;

	/* The daemon's lines, and the lines waiting for it. */
	*changed = 0;
	if (print == NULL)
		return 0;
	if (print->socket >= 0)
		print_read(print);
	if (print->socket >= 0)
		print_flush(print);

	/* The settings file changed by another session. */
	error = stat(print->config, &status);
	if (error == 0 && (status.st_mtim.tv_sec != print->config_time.tv_sec || status.st_mtim.tv_nsec != print->config_time.tv_nsec)) {
		(void)print_load(print);
		print->changed |= KL_BACKEND_PRINT_CHANGED_LIST;
	}

	/* What changed since the last update. */
	*changed = print->changed;
	print->changed = 0;
	return 0;
}

/*
 * Copies the printers.
 */
size_t
kl_backend_print_printers(
	const struct kl_backend_print *print,
	struct kl_backend_printer *list,
	size_t capacity)
{
	size_t count;

	/* As many as fit. */
	count = print->printer_count;
	if (count > capacity)
		count = capacity;
	memcpy(list, print->printers, count * sizeof(list[0]));
	return count;
}

/*
 * Copies the jobs, oldest first.
 */
size_t
kl_backend_print_jobs(
	const struct kl_backend_print *print,
	struct kl_backend_print_job *list,
	size_t capacity)
{
	size_t count;
	size_t index;

	/* As many as fit. */
	count = 0;
	for (index = 0; index < print->job_count && count < capacity; index++) {
		list[count] = print->jobs[index].job;
		count++;
	}
	return count;
}

/*
 * Adds a printer (its path or queue "" for the protocol's usual one); an
 * IPP printer's name is asked of the daemon.
 */
int
kl_backend_print_add(
	struct kl_backend_print *print,
	unsigned protocol,
	const char *host,
	unsigned port,
	const char *path,
	uint32_t *request)
{
	struct kl_backend_printer *printer;
	size_t index;
	int same_host;
	int lock;
	int error;
	int saved;

	/* A protocol, a host, a port. */
	if (protocol != KL_BACKEND_PRINTER_IPP && protocol != KL_BACKEND_PRINTER_LPD)
		return EINVAL;
	if (!print_host_ok(host) || port == 0U || port > 65535U)
		return EINVAL;
	if (path != NULL && (strlen(path) >= KL_BACKEND_PRINTER_PATH_MAX || strchr(path, ' ') != NULL))
		return EINVAL;

	/* The file read again under its lock. */
	*request = print->next_request;
	print->next_request++;
	lock = print_lock(print);
	(void)print_load(print);

	/* Room, and not the same printer twice. */
	error = 0;
	if (print->printer_count == KL_BACKEND_PRINTERS_MAX)
		error = EBUSY;
	for (index = 0; index < print->printer_count && error == 0; index++) {
		same_host = strcmp(print->printers[index].host, host);
		if (same_host == 0 && print->printers[index].port == port && print->printers[index].protocol == protocol)
			error = EINVAL;
	}
	if (error != 0) {
		print_unlock(lock);
		print_result(print, *request, error, 0);
		return 0;
	}

	/* The printer, the first one the default. */
	printer = &print->printers[print->printer_count];
	memset(printer, 0, sizeof(*printer));
	printer->id = print->next_id;
	print->next_id++;
	printer->protocol = protocol;
	print_copy(printer->host, sizeof(printer->host), host);
	printer->port = port;
	if (path != NULL && path[0] != '\0')
		print_copy(printer->path, sizeof(printer->path), path);
	else if (protocol == KL_BACKEND_PRINTER_IPP)
		print_copy(printer->path, sizeof(printer->path), PRINT_IPP_PATH);
	else
		print_copy(printer->path, sizeof(printer->path), PRINT_LPD_QUEUE);
	if (protocol == KL_BACKEND_PRINTER_IPP)
		(void)snprintf(printer->name, sizeof(printer->name), "%.60s (IPP)", host);
	else
		(void)snprintf(printer->name, sizeof(printer->name), "%.60s (LPD)", host);
	printer->is_default = print->printer_count == 0U;
	print->printer_count++;

	/* Written, then answered. */
	saved = print_save(print) == 0;
	print_unlock(lock);
	print_result(print, *request, 0, (unsigned)saved);
	print->changed |= KL_BACKEND_PRINT_CHANGED_LIST;

	/* An IPP printer's name, asked of the daemon. */
	if (protocol == KL_BACKEND_PRINTER_IPP && print->name_count < PRINT_NAMES_MAX) {
		error = print_start(print);
		if (error == 0) {
			print->names[print->name_count].seq = print->next_seq;
			print->names[print->name_count].printer = printer->id;
			print->name_count++;
			print_send(print, -1, 0, "NAME %lu %s %u", (unsigned long)print->next_seq, host, port);
			print->next_seq++;
		}
	}
	return 0;
}

/*
 * Removes a printer; its jobs not ended are cancelled.
 */
int
kl_backend_print_remove(
	struct kl_backend_print *print,
	uint32_t printer,
	uint32_t *request)
{
	struct kl_backend_printer *found;
	uint32_t ignored;
	size_t index;
	int was_default;
	int lock;
	int saved;

	/* The file read again under its lock, and the printer. */
	*request = print->next_request;
	print->next_request++;
	lock = print_lock(print);
	(void)print_load(print);
	found = print_printer(print, printer);
	if (found == NULL) {
		print_unlock(lock);
		print_result(print, *request, EINVAL, 0);
		return 0;
	}

	/* Taken out; the default goes to the smallest number left. */
	was_default = found->is_default;
	index = (size_t)(found - print->printers);
	memmove(&print->printers[index], &print->printers[index + 1U], (print->printer_count - index - 1U) * sizeof(print->printers[0]));
	print->printer_count--;
	if (was_default && print->printer_count > 0U) {
		found = &print->printers[0];
		for (index = 1; index < print->printer_count; index++) {
			if (print->printers[index].id < found->id)
				found = &print->printers[index];
		}
		found->is_default = 1;
	}

	/* Written, then answered. */
	saved = print_save(print) == 0;
	print_unlock(lock);
	print_result(print, *request, 0, (unsigned)saved);
	print->changed |= KL_BACKEND_PRINT_CHANGED_LIST;

	/* Its jobs not ended, cancelled. */
	for (index = 0; index < print->job_count; index++) {
		if (print->jobs[index].job.printer == printer && print->jobs[index].job.state < KL_BACKEND_PRINT_DONE)
			(void)kl_backend_print_cancel(print, print->jobs[index].job.job, &ignored);
	}
	return 0;
}

/*
 * Makes a printer the default.
 */
int
kl_backend_print_set_default(
	struct kl_backend_print *print,
	uint32_t printer,
	uint32_t *request)
{
	struct kl_backend_printer *found;
	size_t index;
	int lock;
	int saved;

	/* The file read again under its lock, and the printer. */
	*request = print->next_request;
	print->next_request++;
	lock = print_lock(print);
	(void)print_load(print);
	found = print_printer(print, printer);
	if (found == NULL) {
		print_unlock(lock);
		print_result(print, *request, EINVAL, 0);
		return 0;
	}

	/* It alone the default. */
	for (index = 0; index < print->printer_count; index++)
		print->printers[index].is_default = 0;
	found->is_default = 1;

	/* Written, then answered. */
	saved = print_save(print) == 0;
	print_unlock(lock);
	print_result(print, *request, 0, (unsigned)saved);
	print->changed |= KL_BACKEND_PRINT_CHANGED_LIST;
	return 0;
}

/*
 * Prints a document (the descriptor is the backend's from here): a job
 * queued and its JOB line sent to the daemon (started when it is not
 * running).  printer 0 is the default.
 */
int
kl_backend_print_submit(
	struct kl_backend_print *print,
	uint32_t printer,
	const char *title,
	int fd,
	uint32_t *request,
	uint32_t *job)
{
	struct kl_backend_printer *found;
	struct print_job *made;
	size_t active;
	size_t index;
	int error;

	/* The printer: the one named, or the default. */
	*request = print->next_request;
	print->next_request++;
	*job = 0;
	found = NULL;
	for (index = 0; index < print->printer_count; index++) {
		if ((printer == 0U && print->printers[index].is_default) || print->printers[index].id == printer)
			found = &print->printers[index];
	}
	if (found == NULL) {
		(void)close(fd);
		print_result(print, *request, EINVAL, 1);
		return 0;
	}

	/* Not more jobs than are held at once. */
	active = 0;
	for (index = 0; index < print->job_count; index++) {
		if (print->jobs[index].job.state < KL_BACKEND_PRINT_DONE)
			active++;
	}
	if (active >= PRINT_ACTIVE_MAX) {
		(void)close(fd);
		print_result(print, *request, EBUSY, 1);
		return 0;
	}

	/* The job, queued with its document. */
	made = print_new_job(print);
	made->job.printer = found->id;
	made->job.state = KL_BACKEND_PRINT_QUEUED;
	print_copy(made->job.title, sizeof(made->job.title), title);
	made->fd = fd;
	*job = made->job.job;
	print_result(print, *request, 0, 1);
	print->changed |= KL_BACKEND_PRINT_CHANGED_LIST;

	/* To the daemon. */
	error = print_start(print);
	if (error != 0) {
		print_end_job(print, made, KL_BACKEND_PRINT_FAILED, "daemon");
		return 0;
	}
	print_send(print, fd, made->job.job, "JOB %lu %s %s %u %s %s", (unsigned long)made->job.job,
	    found->protocol == KL_BACKEND_PRINTER_IPP ? "ipp" : "lpd", found->host, found->port, found->path, title);
	return 0;
}

/*
 * Cancels a job: one whose line has not gone is ended at once; the others
 * are asked of the daemon, which tells how they ended.
 */
int
kl_backend_print_cancel(
	struct kl_backend_print *print,
	uint32_t job,
	uint32_t *request)
{
	struct print_job *found;
	size_t index;

	/* The job, not ended. */
	*request = print->next_request;
	print->next_request++;
	found = print_find_job(print, job);
	if (found == NULL || found->job.state >= KL_BACKEND_PRINT_DONE) {
		print_result(print, *request, EINVAL, 1);
		return 0;
	}

	/* Its line still waiting: taken out of the queue, the job cancelled. */
	for (index = 0; index < print->queue_count; index++) {
		if (print->queue[index].job != job)
			continue;
		if (print->queue[index].fd >= 0)
			(void)close(print->queue[index].fd);
		memmove(&print->queue[index], &print->queue[index + 1U], (print->queue_count - index - 1U) * sizeof(print->queue[0]));
		print->queue_count--;
		found->fd = -1;
		print_end_job(print, found, KL_BACKEND_PRINT_CANCELLED, "");
		print_result(print, *request, 0, 1);
		return 0;
	}

	/* Asked of the daemon. */
	print_send(print, -1, 0, "CANCEL %lu", (unsigned long)job);
	print_result(print, *request, 0, 1);
	return 0;
}

/*
 * Takes the oldest answer.
 */
int
kl_backend_print_take_result(
	struct kl_backend_print *print,
	uint32_t *request,
	int *error,
	unsigned *saved)
{
	/* None. */
	if (print->result_count == 0U)
		return 0;

	/* The first, the rest moved up. */
	*request = print->results[0].request;
	*error = print->results[0].error;
	*saved = print->results[0].saved;
	memmove(&print->results[0], &print->results[1], (print->result_count - 1U) * sizeof(print->results[0]));
	print->result_count--;
	return 1;
}

/* Reads the settings file (none: no printers).  Returns 0 or an errno value. */
static int
print_load(
	struct kl_backend_print *print)
{
	struct kl_backend_printer *printer;
	struct stat status;
	unsigned long id;
	unsigned long port;
	char line[512];
	char protocol[8];
	char host[KL_BACKEND_PRINTER_HOST_MAX];
	char path[KL_BACKEND_PRINTER_PATH_MAX];
	FILE *file;
	int consumed;
	int fields;
	size_t index;

	/* The file, and its time. */
	print->printer_count = 0;
	file = fopen(print->config, "r");
	if (file == NULL)
		return errno;
	if (fstat(fileno(file), &status) == 0)
		print->config_time = status.st_mtim;

	/* Each line; one that is not understood is passed over. */
	while (fgets(line, sizeof(line), file) != NULL) {
		line[strcspn(line, "\r\n")] = '\0';
		if (sscanf(line, "next-id %lu", &id) == 1) {
			if (id > print->next_id)
				print->next_id = (uint32_t)id;
			continue;
		}
		if (sscanf(line, "default %lu", &id) == 1) {
			for (index = 0; index < print->printer_count; index++)
				print->printers[index].is_default = print->printers[index].id == (uint32_t)id;
			continue;
		}
		consumed = 0;
		fields = sscanf(line, "printer %lu %7s %63s %lu %63s %n", &id, protocol, host, &port, path, &consumed);
		if (fields != 5 || consumed == 0 || print->printer_count == KL_BACKEND_PRINTERS_MAX || id == 0UL)
			continue;

		/* A printer. */
		printer = &print->printers[print->printer_count];
		memset(printer, 0, sizeof(*printer));
		printer->id = (uint32_t)id;
		printer->protocol = strcmp(protocol, "lpd") == 0 ? KL_BACKEND_PRINTER_LPD : KL_BACKEND_PRINTER_IPP;
		print_copy(printer->host, sizeof(printer->host), host);
		printer->port = (unsigned)port;
		print_copy(printer->path, sizeof(printer->path), path);
		print_copy(printer->name, sizeof(printer->name), line + consumed);
		if (printer->id >= print->next_id)
			print->next_id = printer->id + 1U;
		print->printer_count++;
	}

	/* Read. */
	(void)fclose(file);
	return 0;
}

/* Writes the settings file through a file renamed over it.  Returns 0 or an errno value. */
static int
print_save(
	struct kl_backend_print *print)
{
	struct stat status;
	char temporary[600];
	char directory[512];
	char *slash;
	FILE *file;
	size_t index;
	int error;

	/* The folder, made when it is not there. */
	print_copy(directory, sizeof(directory), print->config);
	slash = strrchr(directory, '/');
	if (slash != NULL) {
		*slash = '\0';
		(void)mkdir(directory, 0700);
	}

	/* The new file. */
	(void)snprintf(temporary, sizeof(temporary), "%s.new", print->config);
	file = fopen(temporary, "w");
	if (file == NULL)
		return errno;
	fprintf(file, "# Keiland printers\nnext-id %lu\n", (unsigned long)print->next_id);
	for (index = 0; index < print->printer_count; index++) {
		fprintf(file, "printer %lu %s %s %u %s %s\n", (unsigned long)print->printers[index].id,
		    print->printers[index].protocol == KL_BACKEND_PRINTER_LPD ? "lpd" : "ipp", print->printers[index].host,
		    print->printers[index].port, print->printers[index].path, print->printers[index].name);
	}
	for (index = 0; index < print->printer_count; index++) {
		if (print->printers[index].is_default)
			fprintf(file, "default %lu\n", (unsigned long)print->printers[index].id);
	}
	error = fflush(file) != 0 || ferror(file);
	error |= fclose(file) != 0;
	if (error) {
		(void)unlink(temporary);
		return EIO;
	}

	/* In place, and its time kept (no reading again for this session's own change). */
	if (rename(temporary, print->config) != 0) {
		(void)unlink(temporary);
		return errno;
	}
	if (stat(print->config, &status) == 0)
		print->config_time = status.st_mtim;
	return 0;
}

/* Takes the settings file's lock (the file beside it); the descriptor, or -1. */
static int
print_lock(
	const struct kl_backend_print *print)
{
	char path[600];
	int fd;

	/* The lock file, held. */
	(void)snprintf(path, sizeof(path), "%s.lock", print->config);
	fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
	if (fd < 0)
		return -1;
	(void)flock(fd, LOCK_EX);
	return fd;
}

/* Lets the settings file's lock go. */
static void
print_unlock(
	int fd)
{
	/* Closed: the lock goes with it. */
	if (fd >= 0)
		(void)close(fd);
}

/* Finds a printer by its number. */
static struct kl_backend_printer *
print_printer(
	struct kl_backend_print *print,
	uint32_t id)
{
	size_t index;

	/* Each printer. */
	for (index = 0; index < print->printer_count; index++) {
		if (print->printers[index].id == id)
			return &print->printers[index];
	}
	return NULL;
}

/* Finds a job by its number. */
static struct print_job *
print_find_job(
	struct kl_backend_print *print,
	uint32_t job)
{
	size_t index;

	/* Each job. */
	for (index = 0; index < print->job_count; index++) {
		if (print->jobs[index].job.job == job)
			return &print->jobs[index];
	}
	return NULL;
}

/* Makes a job: a free slot, or the oldest ended job's (the table is never full of jobs not ended). */
static struct print_job *
print_new_job(
	struct kl_backend_print *print)
{
	struct print_job *job;
	size_t oldest;
	size_t index;

	/* A free slot. */
	if (print->job_count < KL_BACKEND_PRINT_JOBS_MAX) {
		job = &print->jobs[print->job_count];
		print->job_count++;
	} else {
		/* The oldest ended job's, moved out so that the list stays oldest first. */
		oldest = KL_BACKEND_PRINT_JOBS_MAX;
		for (index = 0; index < print->job_count; index++) {
			if (print->jobs[index].job.state >= KL_BACKEND_PRINT_DONE) {
				oldest = index;
				break;
			}
		}
		if (oldest == KL_BACKEND_PRINT_JOBS_MAX)
			oldest = 0;
		memmove(&print->jobs[oldest], &print->jobs[oldest + 1U], (print->job_count - oldest - 1U) * sizeof(print->jobs[0]));
		job = &print->jobs[print->job_count - 1U];
	}

	/* Its number. */
	memset(job, 0, sizeof(*job));
	job->fd = -1;
	job->job.job = print->next_job;
	print->next_job++;
	job->order = print->next_order;
	print->next_order++;
	return job;
}

/* Ends a job in a state: its document closed. */
static void
print_end_job(
	struct kl_backend_print *print,
	struct print_job *job,
	unsigned state,
	const char *detail)
{
	/* The document. */
	if (job->fd >= 0)
		(void)close(job->fd);
	job->fd = -1;

	/* The state. */
	job->job.state = state;
	print_copy(job->job.detail, sizeof(job->job.detail), detail);
	print->changed |= KL_BACKEND_PRINT_CHANGED_LIST;
}

/* Keeps an answer (the oldest dropped when they are too many). */
static void
print_result(
	struct kl_backend_print *print,
	uint32_t request,
	int error,
	unsigned saved)
{
	/* Room. */
	if (print->result_count == PRINT_RESULTS_MAX) {
		memmove(&print->results[0], &print->results[1], (PRINT_RESULTS_MAX - 1U) * sizeof(print->results[0]));
		print->result_count--;
	}

	/* The answer. */
	print->results[print->result_count].request = request;
	print->results[print->result_count].error = error;
	print->results[print->result_count].saved = saved;
	print->result_count++;
	print->changed |= KL_BACKEND_PRINT_CHANGED_RESULT;
}

/*
 * Starts the daemon when it is not running: a socket pair, its end on the
 * daemon's descriptor 3, the runtime directory in its environment.
 * Returns 0 or an errno value.
 */
static int
print_start(
	struct kl_backend_print *print)
{
	posix_spawn_file_actions_t actions;
	posix_spawnattr_t attributes;
	sigset_t defaults;
	sigset_t mask;
	char *arguments[2];
	char *environment[PRINT_ENVIRONMENT_MAX];
	char runtime[600];
	size_t count;
	size_t index;
	pid_t pid;
	int pair[2];
	int child;
	int status;

	/* Running already. */
	if (print->socket >= 0)
		return 0;
	if (print->runtime[0] == '\0')
		return ENOENT;

	/* The socket pair, the backend's end not blocking. */
	status = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair);
	if (status != 0)
		return errno;
	(void)fcntl(pair[0], F_SETFL, O_NONBLOCK);
	child = pair[1];
	if (child == 3) {
		child = fcntl(pair[1], F_DUPFD_CLOEXEC, 4);
		(void)close(pair[1]);
		if (child < 0) {
			(void)close(pair[0]);
			return EMFILE;
		}
	}

	/* The environment, the runtime directory the backend's. */
	count = 0;
	(void)snprintf(runtime, sizeof(runtime), "XDG_RUNTIME_DIR=%s", print->runtime);
	environment[count] = runtime;
	count++;
	for (index = 0; environ != NULL && environ[index] != NULL && count + 1U < PRINT_ENVIRONMENT_MAX; index++) {
		if (strncmp(environ[index], "XDG_RUNTIME_DIR=", 16U) == 0)
			continue;
		environment[count] = environ[index];
		count++;
	}
	environment[count] = NULL;

	/* The daemon: its descriptor 3, its signals as they start, no mask. */
	(void)posix_spawn_file_actions_init(&actions);
	(void)posix_spawn_file_actions_adddup2(&actions, child, 3);
	(void)posix_spawnattr_init(&attributes);
	(void)sigemptyset(&defaults);
	(void)sigaddset(&defaults, SIGPIPE);
	(void)sigaddset(&defaults, SIGCHLD);
	(void)sigaddset(&defaults, SIGINT);
	(void)sigaddset(&defaults, SIGTERM);
	(void)sigaddset(&defaults, SIGHUP);
	(void)sigemptyset(&mask);
	(void)posix_spawnattr_setsigdefault(&attributes, &defaults);
	(void)posix_spawnattr_setsigmask(&attributes, &mask);
	(void)posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);
	arguments[0] = print->program;
	arguments[1] = NULL;
	status = posix_spawn(&pid, print->program, &actions, &attributes, arguments, environment);
	(void)posix_spawn_file_actions_destroy(&actions);
	(void)posix_spawnattr_destroy(&attributes);
	(void)close(child);
	if (status != 0) {
		(void)close(pair[0]);
		return status;
	}

	/* Running. */
	print->daemon = pid;
	print->socket = pair[0];
	print->commands = 0;
	print->input_length = 0;
	print->spool[0] = '\0';
	return 0;
}

/*
 * The daemon ended (its socket's end): its jobs not ended fail, its lines
 * waiting go, its spool is removed.
 */
static void
print_stopped(
	struct kl_backend_print *print)
{
	size_t index;

	/* The socket. */
	(void)close(print->socket);
	print->socket = -1;
	print->daemon = -1;

	/* The lines waiting and their documents. */
	for (index = 0; index < print->queue_count; index++) {
		if (print->queue[index].fd >= 0)
			(void)close(print->queue[index].fd);
	}
	print->queue_count = 0;
	print->name_count = 0;

	/* The jobs not ended. */
	for (index = 0; index < print->job_count; index++) {
		if (print->jobs[index].job.state < KL_BACKEND_PRINT_DONE) {
			print->jobs[index].fd = -1;
			print_end_job(print, &print->jobs[index], KL_BACKEND_PRINT_FAILED, "daemon");
		}
	}

	/* What it left in its spool. */
	if (print->spool[0] != '\0')
		print_remove_spool(print->spool);
	print->spool[0] = '\0';
}

/* Queues a line for the daemon (with a descriptor to pass, -1 for none, and the job it is for) and sends what it can. */
static void
print_send(
	struct kl_backend_print *print,
	int fd,
	uint32_t job,
	const char *format,
	...)
{
	struct print_line *line;
	va_list arguments;
	int length;

	/* A full queue: the line is lost, its document closed. */
	if (print->queue_count == PRINT_QUEUE_MAX) {
		if (fd >= 0)
			(void)close(fd);
		return;
	}

	/* The line. */
	line = &print->queue[print->queue_count];
	va_start(arguments, format);
	length = vsnprintf(line->text, sizeof(line->text) - 1U, format, arguments);
	va_end(arguments);
	if (length < 0)
		length = 0;
	if ((size_t)length > sizeof(line->text) - 2U)
		length = (int)(sizeof(line->text) - 2U);
	line->text[length] = '\n';
	line->length = (size_t)length + 1U;
	line->fd = fd;
	line->job = job;
	print->queue_count++;
	print->commands++;

	/* Sent now when the socket takes it. */
	print_flush(print);
}

/*
 * Sends the lines waiting, each whole (its descriptor with it); a socket
 * that does not take one stops the sending until the next update.
 */
static void
print_flush(
	struct kl_backend_print *print)
{
	union {
		struct cmsghdr header;
		char space[CMSG_SPACE(sizeof(int))];
	} control;
	struct print_line *line;
	struct print_job *job;
	struct msghdr message;
	struct cmsghdr *rights;
	struct iovec vector;
	ssize_t sent;

	/* Each line, in order. */
	while (print->queue_count > 0U && print->socket >= 0) {
		line = &print->queue[0];
		memset(&message, 0, sizeof(message));
		vector.iov_base = line->text;
		vector.iov_len = line->length;
		message.msg_iov = &vector;
		message.msg_iovlen = 1;

		/* Its descriptor, with its first byte. */
		if (line->fd >= 0) {
			memset(&control, 0, sizeof(control));
			message.msg_control = control.space;
			message.msg_controllen = sizeof(control.space);
			rights = CMSG_FIRSTHDR(&message);
			rights->cmsg_level = SOL_SOCKET;
			rights->cmsg_type = SCM_RIGHTS;
			rights->cmsg_len = CMSG_LEN(sizeof(int));
			memcpy(CMSG_DATA(rights), &line->fd, sizeof(int));
		}
		sent = sendmsg(print->socket, &message, MSG_DONTWAIT | MSG_NOSIGNAL);
		if (sent < 0)
			return;

		/* The descriptor went with the first byte: the job's line went. */
		if (line->fd >= 0 && line->job != 0U) {
			job = print_find_job(print, line->job);
			if (job != NULL)
				job->sent = 1;
		}
		line->fd = -1;

		/* Part of it: the rest waits. */
		if ((size_t)sent < line->length) {
			memmove(line->text, line->text + sent, line->length - (size_t)sent);
			line->length -= (size_t)sent;
			return;
		}

		/* Whole: the next. */
		memmove(&print->queue[0], &print->queue[1], (print->queue_count - 1U) * sizeof(print->queue[0]));
		print->queue_count--;
	}
}

/* Reads the daemon's lines; its end stops it. */
static void
print_read(
	struct kl_backend_print *print)
{
	char bytes[PRINT_LINE_MAX];
	ssize_t got;
	ssize_t index;

	/* What there is. */
	for (;;) {
		got = recv(print->socket, bytes, sizeof(bytes), MSG_DONTWAIT);
		if (got < 0 && errno == EINTR)
			continue;
		if (got < 0)
			return;
		if (got == 0) {
			print_stopped(print);
			return;
		}

		/* Line by line. */
		for (index = 0; index < got; index++) {
			if (print->input_length == sizeof(print->input) - 1U)
				print->input_length = 0;
			if (bytes[index] != '\n') {
				print->input[print->input_length] = bytes[index];
				print->input_length++;
				continue;
			}
			print->input[print->input_length] = '\0';
			print->input_length = 0;
			print_line(print, print->input);
			if (print->socket < 0)
				return;
		}
	}
}

/* Carries out one of the daemon's lines. */
static void
print_line(
	struct kl_backend_print *print,
	char *line)
{
	struct kl_backend_printer *printer;
	struct print_job *job;
	unsigned long number;
	unsigned long count;
	char word[32];
	char detail[32];
	char path[KL_BACKEND_PRINTER_PATH_MAX];
	int consumed;
	int lock;

	/* Where its spool is. */
	if (strncmp(line, "SPOOL ", 6U) == 0) {
		print_copy(print->spool, sizeof(print->spool), line + 6);
		return;
	}

	/* A job taken into the spool, or refused. */
	if (sscanf(line, "ACCEPTED %lu", &number) == 1) {
		job = print_find_job(print, (uint32_t)number);
		if (job != NULL && job->fd >= 0) {
			(void)close(job->fd);
			job->fd = -1;
		}
		return;
	}
	detail[0] = '\0';
	if (sscanf(line, "REJECTED %lu %31s", &number, detail) >= 1) {
		job = print_find_job(print, (uint32_t)number);
		if (job != NULL && job->job.state < KL_BACKEND_PRINT_DONE)
			print_end_job(print, job, KL_BACKEND_PRINT_FAILED, detail);
		return;
	}

	/* A job's state. */
	detail[0] = '\0';
	if (sscanf(line, "STATE %lu %31s %31s", &number, word, detail) >= 2) {
		print_job_state(print, (uint32_t)number, word, detail);
		return;
	}

	/* The path an IPP printer answered at, kept. */
	if (sscanf(line, "PATH %lu %63s", &number, path) == 2) {
		job = print_find_job(print, (uint32_t)number);
		if (job == NULL)
			return;
		lock = print_lock(print);
		(void)print_load(print);
		printer = print_printer(print, job->job.printer);
		if (printer != NULL) {
			print_copy(printer->path, sizeof(printer->path), path);
			(void)print_save(print);
			print->changed |= KL_BACKEND_PRINT_CHANGED_LIST;
		}
		print_unlock(lock);
		return;
	}

	/* A printer's name. */
	consumed = 0;
	if (sscanf(line, "NAMED %lu%n", &number, &consumed) == 1) {
		print_named(print, (uint32_t)number, line + consumed);
		return;
	}

	/* Nothing to do: ended when the daemon read every command and nothing waits. */
	if (sscanf(line, "IDLE %lu", &count) == 1) {
		if (count == print->commands && print->queue_count == 0U) {
			print_send(print, -1, 0, "BYE %lu", count);
			print->commands--;
		}
		return;
	}
}

/* Moves a job to the state the daemon told. */
static void
print_job_state(
	struct kl_backend_print *print,
	uint32_t number,
	const char *state,
	const char *detail)
{
	struct print_job *job;
	int same;

	/* A job not ended. */
	job = print_find_job(print, number);
	if (job == NULL || job->job.state >= KL_BACKEND_PRINT_DONE)
		return;

	/* By the word. */
	print_copy(job->job.detail, sizeof(job->job.detail), detail);
	same = strcmp(state, "sending");
	if (same == 0) {
		job->job.state = KL_BACKEND_PRINT_SENDING;
		print->changed |= KL_BACKEND_PRINT_CHANGED_LIST;
		return;
	}
	same = strcmp(state, "waiting");
	if (same == 0) {
		job->job.state = KL_BACKEND_PRINT_WAITING;
		print->changed |= KL_BACKEND_PRINT_CHANGED_LIST;
		return;
	}
	same = strcmp(state, "done");
	if (same == 0) {
		print_end_job(print, job, KL_BACKEND_PRINT_DONE, detail);
		return;
	}
	same = strcmp(state, "cancelled");
	if (same == 0) {
		print_end_job(print, job, KL_BACKEND_PRINT_CANCELLED, detail);
		return;
	}
	print_end_job(print, job, KL_BACKEND_PRINT_FAILED, detail);
}

/* Keeps a printer's name and path the daemon found (NAMED <seq> <path> <name>, or NAMED <seq> for none). */
static void
print_named(
	struct kl_backend_print *print,
	uint32_t seq,
	char *rest)
{
	struct kl_backend_printer *printer;
	uint32_t id;
	size_t index;
	char *name;
	int lock;

	/* The printer asked about. */
	id = 0;
	for (index = 0; index < print->name_count; index++) {
		if (print->names[index].seq != seq)
			continue;
		id = print->names[index].printer;
		memmove(&print->names[index], &print->names[index + 1U], (print->name_count - index - 1U) * sizeof(print->names[0]));
		print->name_count--;
		break;
	}
	if (id == 0U || rest[0] != ' ')
		return;

	/* The path, then the name. */
	rest++;
	name = strchr(rest, ' ');
	if (name == NULL)
		return;
	*name = '\0';
	name++;

	/* Kept in the file. */
	lock = print_lock(print);
	(void)print_load(print);
	printer = print_printer(print, id);
	if (printer != NULL) {
		print_copy(printer->path, sizeof(printer->path), rest);
		print_copy(printer->name, sizeof(printer->name), name);
		(void)print_save(print);
		print->changed |= KL_BACKEND_PRINT_CHANGED_LIST;
	}
	print_unlock(lock);
}

/* Removes a dead daemon's spool: its files, the directory and its lock file. */
static void
print_remove_spool(
	const char *dir)
{
	struct dirent *entry;
	char path[1024];
	DIR *opened;

	/* Each file. */
	opened = opendir(dir);
	if (opened != NULL) {
		for (;;) {
			entry = readdir(opened);
			if (entry == NULL)
				break;
			if (entry->d_name[0] == '.')
				continue;
			(void)snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
			(void)unlink(path);
		}
		(void)closedir(opened);
	}

	/* The directory and its lock. */
	(void)rmdir(dir);
	(void)snprintf(path, sizeof(path), "%s.lock", dir);
	(void)unlink(path);
}

/* Tells whether a host is 1 to 63 letters, digits, dots and hyphens. */
static int
print_host_ok(
	const char *host)
{
	size_t length;
	size_t index;
	char c;

	/* Its length. */
	if (host == NULL)
		return 0;
	length = strlen(host);
	if (length == 0U || length >= KL_BACKEND_PRINTER_HOST_MAX)
		return 0;

	/* Each character. */
	for (index = 0; index < length; index++) {
		c = host[index];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-'))
			return 0;
	}
	return 1;
}

/* Copies a text into a field, cut to fit. */
static void
print_copy(
	char *to,
	size_t size,
	const char *from)
{
	/* As much as fits. */
	(void)snprintf(to, size, "%s", from != NULL ? from : "");
}
