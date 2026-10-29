/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The device I/O thread (io-device): the work of the compositor that may
 * wait for a device or the kernel runs here, away from the event loop: the
 * input devices' finding and closing, and the jobs other parts give it
 * (the power's state, the network's details, the computer's readings, the
 * monitor's samples).
 *
 * The input devices: the event loop asks for a finding (the evdev nodes
 * not read yet are opened and their capabilities read, the open of a PS/2
 * mouse port without a mouse waits for its timeout) and later takes the
 * nodes opened, which the seat classifies on the event loop's thread as
 * before; the nodes it does not keep, and the devices it is done with, are
 * closed here too, because a device's close runs its driver's close.
 * Where the backend opens the devices through a seat service (Linux and
 * FreeBSD), the event loop scans as before.
 *
 * The file I/O thread (io-file): the reads and writes of the session's
 * files run here, in the order given (a file is written in the order its
 * writes were given), each answered on the event loop's thread when it is
 * done.  A write's flush takes seconds on zedBSD's UFS.
 */

#include "kwl.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Marks a parameter a function takes but does not use. */
#define UNUSED_PARAMETER(name)	((void)(name))

/*
 * One finding of the input devices: the lane's job and the backend's
 * record of it (the paths read already, the nodes opened).  The backend
 * pointer stays valid while the lane runs (the lane stops before the
 * backend closes).
 */
struct io_find_job {
	struct kwl_lane_job lane;
	struct kl_backend *backend;
	int error;
	struct kl_backend_input_find find;
};

/* One descriptor of an input device to close. */
struct io_close_job {
	struct kwl_lane_job lane;
	struct kl_backend *backend;
	int descriptor;
};

static void io_find_run(struct kwl_lane_job *job);
static void io_find_done(void *context, struct kwl_lane_job *job);
static void io_find_drop(struct kwl_lane_job *job);
static void io_close_run(struct kwl_lane_job *job);
static void io_close_done(void *context, struct kwl_lane_job *job);
static void io_close_drop(struct kwl_lane_job *job);
static void io_find_known(const struct kwl_server *server, struct kl_backend_input_find *find);

/* The operations of a finding. */
static const struct kwl_lane_ops io_find_ops = {
	io_find_run,
	io_find_done,
	io_find_drop
};

/* The operations of a close. */
static const struct kwl_lane_ops io_close_ops = {
	io_close_run,
	io_close_done,
	io_close_drop
};

/*
 * Starts the I/O threads once the threads' wake pipe is made: the file
 * I/O thread and the device I/O thread, which finds and closes the input
 * devices where the backend opens them itself.  Without one, its work is
 * done in the event loop as before (logged).
 */
void
kwl_io_start(
	struct kwl_server *server)
{
	int supported;
	int error;

	/* Made after the wake pipe. */
	if (!server->threads_prepared)
		return;

	/* The file I/O thread, once. */
	if (!server->io_file_started) {
		error = kwl_lane_start(&server->io_file, "io-file", 1U, server->wake[1]);
		if (error != 0)
			printf("KWL IO thread=io-file errno=%d the files are written in the event loop\n", error);
		else
			server->io_file_started = 1U;
	}

	/* The device I/O thread, once. */
	if (server->io_started)
		return;

	/* The thread, which waits for the first job. */
	error = kwl_lane_start(&server->io_device, "io-device", 1U, server->wake[1]);
	if (error != 0) {
		printf("KWL IO thread=io-device errno=%d the devices are waited for in the event loop\n", error);
		return;
	}

	/* Marks the device thread as started, so it is started once. */
	server->io_started = 1U;

	/* The scans and the closes go to the thread from now on, unless a seat opens the devices. */
	supported = kl_backend_input_find_supported();
	if (supported)
		server->io_input = 1U;
}

/*
 * Takes what the device I/O thread finished, on the event loop's thread,
 * after its wake pipe.
 */
void
kwl_io_take(
	struct kwl_server *server)
{
	/* The findings offer their nodes, the other jobs are applied; the closes need nothing. */
	if (server->io_started)
		(void)kwl_lane_take(&server->io_device, server);

	/* The files read and written are applied and answered. */
	if (server->io_file_started)
		(void)kwl_lane_take(&server->io_file, server);
}

/*
 * Gives the file I/O thread a job (run there, done here at a later pass);
 * a key other than 0 replaces the job of the same key not started yet
 * (KWL_IO_KEY_*).  Returns 0, or ENOTSUP when the thread does not run: the
 * caller does the work itself.
 */
int
kwl_io_file_submit(
	struct kwl_server *server,
	struct kwl_lane_job *job,
	const struct kwl_lane_ops *ops,
	uint64_t key)
{
	/* Without the thread the caller does it. */
	if (!server->io_file_started)
		return ENOTSUP;

	/* Succeeded: the thread has it. */
	kwl_lane_submit(&server->io_file, job, ops, key);
	return 0;
}

/*
 * Gives the device I/O thread a job (run there, done here at a later
 * pass); a key other than 0 replaces the job of the same key not started
 * yet (KWL_IO_KEY_*).  Returns 0, or ENOTSUP when the thread does not run:
 * the caller does the work itself.
 */
int
kwl_io_device_submit(
	struct kwl_server *server,
	struct kwl_lane_job *job,
	const struct kwl_lane_ops *ops,
	uint64_t key)
{
	/* Without the thread the caller does it. */
	if (!server->io_started)
		return ENOTSUP;

	/* Succeeded: the thread has it. */
	kwl_lane_submit(&server->io_device, job, ops, key);
	return 0;
}

/*
 * Ends the file I/O thread at the end of the session, after the event
 * loop: every job given runs and is taken first (the files the session
 * changed reach the disk), then the thread ends.
 */
void
kwl_io_file_finish(
	struct kwl_server *server)
{
	/* Nothing runs. */
	if (!server->io_file_started)
		return;

	/* Everything given is done and answered, then the thread ends. */
	kwl_lane_finish(&server->io_file, server);
	kwl_lane_stop(&server->io_file);
	server->io_file_started = 0U;
}

/*
 * Stops the device I/O thread at the end of the session, after the seat
 * let go of its devices: the job running ends, the nodes found and not
 * taken are closed, and so are the descriptors waiting to be closed.
 */
void
kwl_io_stop(
	struct kwl_server *server)
{
	/* Nothing runs. */
	if (!server->io_started)
		return;

	/* The thread ends; the jobs left are dropped, which closes their descriptors. */
	kwl_lane_stop(&server->io_device);
	server->io_started = 0U;
	server->io_input = 0U;
	server->input_finding = 0U;
}

/*
 * Asks the device I/O thread to open the input devices not read yet; the
 * seat is offered them when the finding is taken.  Returns 0, EBUSY when a
 * finding is under way already, ENOTSUP when no thread runs (the caller
 * scans itself), or ENOMEM.
 */
int
kwl_io_input_find(
	struct kwl_server *server)
{
	struct io_find_job *job;

	/* Without the thread, or where a seat opens the devices, the event loop scans. */
	if (!server->io_input)
		return ENOTSUP;

	/* One finding at a time: a slow open does not gather scans behind it. */
	if (server->input_finding)
		return EBUSY;

	/* The job's record. */
	job = calloc(1U, sizeof(*job));
	if (job == NULL)
		return ENOMEM;

	/* The paths read now, and the backend's state the finding needs, copied on this thread. */
	job->backend = server->backend;
	io_find_known(server, &job->find);
	kl_backend_input_find_begin(server->backend, &job->find);

	/* The thread opens the nodes; input_finding holds the next scan back until the taking. */
	server->input_finding = 1U;
	kwl_lane_submit(&server->io_device, &job->lane, &io_find_ops, 0U);

	/* Succeeded: the nodes come with a later pass. */
	return 0;
}

/*
 * Closes the descriptor of an input device the seat is done with: on the
 * device I/O thread when it runs (the seat no longer reads it), here
 * otherwise.
 */
void
kwl_io_input_close(
	struct kwl_server *server,
	int descriptor)
{
	struct io_close_job *job;

	/* Nothing to close. */
	if (descriptor < 0)
		return;

	/* Without the thread, or without memory for the job, the descriptor is closed here. */
	job = NULL;
	if (server->io_input)
		job = calloc(1U, sizeof(*job));
	if (job == NULL) {
		kl_backend_input_close(server->backend, descriptor);
		return;
	}

	/* The thread closes it; the descriptor is the job's from here. */
	job->backend = server->backend;
	job->descriptor = descriptor;
	kwl_lane_submit(&server->io_device, &job->lane, &io_close_ops, 0U);
}

/* Opens the nodes on the device I/O thread. */
static void
io_find_run(
	struct kwl_lane_job *lane_job)
{
	struct io_find_job *job;

	/* The finding's record. */
	job = (struct io_find_job *)lane_job;

	/* The backend opens what is not known (ENOTSUP where a seat opens them). */
	job->error = kl_backend_input_find(job->backend, &job->find);
}

/*
 * Offers the nodes a finding opened to the seat, on the event loop's
 * thread, as the backend's scan offers them: a node the seat reads
 * already (it was opened by an earlier pass meanwhile) or does not keep
 * is closed on the device I/O thread.
 */
static void
io_find_done(
	void *context,
	struct kwl_lane_job *lane_job)
{
	struct kl_backend_input_node *node;
	struct kwl_server *server;
	struct io_find_job *job;
	unsigned index;
	int known;
	int kept;

	/* The finding is over: the next scan may ask again. */
	server = context;
	job = (struct io_find_job *)lane_job;
	server->input_finding = 0U;

	/* A refusal of the backend is logged; it opened nothing. */
	if (job->error != 0)
		printf("KWL IO input find errno=%d\n", job->error);

	/* Each node opened, in the order found. */
	for (index = 0U; index < job->find.count; index++) {
		node = &job->find.nodes[index];

		/* A node the seat reads already is closed again. */
		known = kwl_backend_input_known(server, node->path);
		if (known) {
			kwl_io_input_close(server, node->descriptor);
			continue;
		}

		/* The seat's classification keeps it, or it is closed. */
		kept = kwl_input_probe(server, node->descriptor, node->path, &node->caps);
		if (!kept)
			kwl_io_input_close(server, node->descriptor);
	}

	/* The record is the event loop's to free. */
	free(job);
}

/* Closes the nodes of a finding that is not taken (the thread stopped), on the stopping thread. */
static void
io_find_drop(
	struct kwl_lane_job *lane_job)
{
	struct io_find_job *job;
	unsigned index;

	/* Each node opened is closed; nothing reads it. */
	job = (struct io_find_job *)lane_job;
	for (index = 0U; index < job->find.count; index++)
		kl_backend_input_close(job->backend, job->find.nodes[index].descriptor);

	/* The record. */
	free(job);
}

/* Closes a descriptor on the device I/O thread. */
static void
io_close_run(
	struct kwl_lane_job *lane_job)
{
	struct io_close_job *job;

	/* The device's driver closes it here. */
	job = (struct io_close_job *)lane_job;
	kl_backend_input_close(job->backend, job->descriptor);
	job->descriptor = -1;
}

/* Frees a close that ran. */
static void
io_close_done(
	void *context,
	struct kwl_lane_job *lane_job)
{
	UNUSED_PARAMETER(context);

	/* The descriptor was closed by the thread. */
	free(lane_job);
}

/* Closes a descriptor whose close did not run (the thread stopped), on the stopping thread. */
static void
io_close_drop(
	struct kwl_lane_job *lane_job)
{
	struct io_close_job *job;

	/* The descriptor still needs its close. */
	job = (struct io_close_job *)lane_job;
	if (job->descriptor >= 0)
		kl_backend_input_close(job->backend, job->descriptor);

	/* The record. */
	free(job);
}

/* Lists the paths of the devices the seat reads, for a finding to leave alone. */
static void
io_find_known(
	const struct kwl_server *server,
	struct kl_backend_input_find *find)
{
	unsigned index;

	/* Each live device that is not going, as kwl_backend_input_known counts them. */
	find->known_count = 0U;
	for (index = 0U; index < KWL_INPUT_MAX; index++) {
		if (!server->inputs[index].live || server->inputs[index].closing)
			continue;
		if (find->known_count >= KL_BACKEND_INPUT_KNOWN_MAX)
			break;
		(void)snprintf(find->known[find->known_count], sizeof(find->known[0]), "%s", server->inputs[index].path);
		find->known_count++;
	}
}
