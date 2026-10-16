/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The control device of the amdrdna driver, /dev/amdrdnaN.
 *
 * Anyone may read the driver's state and the video BIOS image through it;
 * only root may ask for further stages.  The device exists in kernels
 * built with CONFIG_AMDRDNA_CONTROL, which is meant for the time the driver
 * is being brought up; a kernel without it runs the stages the boot line
 * names and nothing more.  The driver's record outlives the device: the
 * function is never detached while the device exists.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/cdev.h>
#include <kern/cred.h>
#include <kern/file.h>
#include <kern/kcrt.h>
#include <kern/lock.h>
#include <kern/uaccess.h>
#include <uapi/amdrdna.h>
#include <uapi/errno.h>

#include "drivers/gpu/amdrdna/amdrdna-node.h"
#include "drivers/gpu/amdrdna/amdrdna-private.h"
#include "drivers/gpu/amdrdna/amdrdna-regs.h"

#ifndef CONFIG_AMDRDNA_CONTROL
#define CONFIG_AMDRDNA_CONTROL 0
#endif

#if CONFIG_AMDRDNA_CONTROL

/* The device numbers of the class: major 23 (free in the kernel's table), the minor is the GPU's number. */
#define CONTROL_DEVICE_MAJOR		23U

/* The longest device name: "amdrdna" and a number. */
#define CONTROL_NAME_CAPACITY		16U

static int control_ioctl(struct file *file, unsigned long request, uintptr_t argument);
static int control_status(struct amdrdna_device *device, uintptr_t argument);
static int control_advance(struct amdrdna_device *device, uintptr_t argument);
static int control_vbios(struct amdrdna_device *device, uintptr_t argument);
static int control_display(struct amdrdna_device *device, uintptr_t argument);
static int control_screen(struct amdrdna_device *device, uintptr_t argument);
static int control_reset(struct amdrdna_device *device, uintptr_t argument);
static int control_hotplug(struct amdrdna_device *device, uintptr_t argument);
static void control_fill(struct amdrdna_device *device, struct amdrdna_status *status);
static void control_fill_memory(struct amdrdna_device *device, struct amdrdna_status *status);
static uint32_t control_fill_power(struct amdrdna_device *device, struct amdrdna_status *status);
static void control_fill_engines(struct amdrdna_device *device, struct amdrdna_status *status);
static void control_fill_spaces(struct amdrdna_device *device, struct amdrdna_status *status);
static uint32_t ip_version(const struct amdrdna_ip *ip);

/* The device's operations: requests only. */
static const struct cdev_ops control_ops = {
	.ioctl = control_ioctl
};

/*
 * The number the next control device takes.  Only the attach, which PCI
 * runs one function at a time, moves it; it only ever increases.
 */
static unsigned control_next_number;

#endif

/*
 * Publishes the control device of one GPU.
 *
 * Returns 0, also in a kernel built without the device, or the error of
 * the registration.
 */
int
amdrdna_control_create(
	struct amdrdna_device *device)
{
#if CONFIG_AMDRDNA_CONTROL
	char name[CONTROL_NAME_CAPACITY];
	unsigned number;
	int error;

	/* Names the device after the GPU's number. */
	number = control_next_number;
	control_next_number++;
	(void)kern_snprintf(name, sizeof(name), "amdrdna%u", number);

	/* Publishes it; the registry keeps its own reference until the kernel stops. */
	error = cdev_register_managed(name,
	    (dev_t)((CONTROL_DEVICE_MAJOR << 16) + number),
	    &control_ops,
	    device,
	    NULL,
	    &device->control);
	if (error != 0) {
		amdrdna_mark("control device %s not published (%d)", name, error);
		return error;
	}

	/* Succeeded: /dev/amdrdnaN answers requests. */
	amdrdna_mark("control device /dev/%s", name);
	return 0;
#else
	UNUSED_PARAMETER(device);

	/* A kernel built without the device publishes nothing. */
	return 0;
#endif
}

#if CONFIG_AMDRDNA_CONTROL

/* Answers a request on the control device. */
static int
control_ioctl(
	struct file *file,
	unsigned long request,
	uintptr_t argument)
{
	struct amdrdna_device *device;
	int error;

	/* The GPU the device was opened on. */
	device = file->f_data;

	/* Does what the request asks. */
	switch (request) {
	case AMDRDNA_IOC_STATUS:
		error = control_status(device, argument);
		break;
	case AMDRDNA_IOC_ADVANCE:
		error = control_advance(device, argument);
		break;
	case AMDRDNA_IOC_VBIOS:
		error = control_vbios(device, argument);
		break;
	case AMDRDNA_IOC_DISPLAY:
		error = control_display(device, argument);
		break;
	case AMDRDNA_IOC_SCREEN:
		error = control_screen(device, argument);
		break;
	case AMDRDNA_IOC_RESET:
		error = control_reset(device, argument);
		break;
	case AMDRDNA_IOC_HOTPLUG:
		error = control_hotplug(device, argument);
		break;
	default:
		error = ENOTTY;
		break;
	}

	/* Reports why the request failed. */
	if (error != 0)
		return error;

	/* Succeeded: the request was answered. */
	return 0;
}

/* Reports the driver's state and what its stages read: AMDRDNA_IOC_STATUS. */
static int
control_status(
	struct amdrdna_device *device,
	uintptr_t argument)
{
	struct amdrdna_status status;
	int error;

	/* The caller's version and size must be this interface's. */
	error = copyin(argument, &status, sizeof(status));
	if (error != 0)
		return error;
	if (status.version != AMDRDNA_INTERFACE_VERSION || status.size != sizeof(status))
		return EINVAL;

	/* Fills the answer. */
	control_fill(device, &status);

	/* Hands it to the caller. */
	error = copyout(&status, argument, sizeof(status));
	if (error != 0)
		return error;

	/* Succeeded: the caller has the state. */
	return 0;
}

/* Asks for the stages up to a target: AMDRDNA_IOC_ADVANCE, for root only. */
static int
control_advance(
	struct amdrdna_device *device,
	uintptr_t argument)
{
	struct amdrdna_advance advance;
	struct ucred *credential;
	int superuser;
	int error;

	/* Refuses a caller that is not root. */
	credential = cred_current_ref();
	superuser = cred_is_superuser(credential);
	cred_release(credential);
	if (!superuser)
		return EPERM;

	/* The caller's version and size must be this interface's. */
	error = copyin(argument, &advance, sizeof(advance));
	if (error != 0)
		return error;
	if (advance.version != AMDRDNA_INTERFACE_VERSION || advance.size != sizeof(advance))
		return EINVAL;

	/* Raises the target; the bring-up thread runs the stages. */
	error = amdrdna_request_target(device, advance.target);
	if (error != 0)
		return error;

	/* Succeeded: the stages up to the target will run. */
	return 0;
}

/*
 * Resets the GPU and sets it up again: AMDRDNA_IOC_RESET, for root only,
 * while no session uses the GPU.  The answer carries the reset's result;
 * the request itself fails only when it is refused.
 */
static int
control_reset(
	struct amdrdna_device *device,
	uintptr_t argument)
{
	struct amdrdna_reset_ask ask;
	struct ucred *credential;
	uint32_t milliseconds;
	int superuser;
	int result;
	int error;

	/* Refuses a caller that is not root. */
	credential = cred_current_ref();
	superuser = cred_is_superuser(credential);
	cred_release(credential);
	if (!superuser)
		return EPERM;

	/* The caller's version and size must be this interface's. */
	error = copyin(argument, &ask, sizeof(ask));
	if (error != 0)
		return error;
	if (ask.version != AMDRDNA_INTERFACE_VERSION || ask.size != sizeof(ask))
		return EINVAL;

	/* The reset, run by the bring-up thread; a refusal is the request's error. */
	milliseconds = 0U;
	result = amdrdna_reset_request(device, AMDRDNA_RESET_REQUEST, &milliseconds);
	if (result == EBUSY || result == ENODEV)
		return result;

	/* The answer. */
	ask.result = result;
	ask.milliseconds = milliseconds;
	ask.resets = device->reset.resets;
	error = copyout(&ask, argument, sizeof(ask));
	if (error != 0)
		return error;

	/* Succeeded: the reset ran; its result is in the answer. */
	return 0;
}

/*
 * Acts as if the monitor went away or came: AMDRDNA_IOC_HOTPLUG, for root
 * only.  The answer carries the action's result, whether the output is
 * dark and the mode; the request itself fails only when it is refused.
 */
static int
control_hotplug(
	struct amdrdna_device *device,
	uintptr_t argument)
{
	struct amdrdna_hotplug_ask ask;
	struct ucred *credential;
	int superuser;
	int result;
	int error;

	/* Refuses a caller that is not root. */
	credential = cred_current_ref();
	superuser = cred_is_superuser(credential);
	cred_release(credential);
	if (!superuser)
		return EPERM;

	/* The caller's version and size must be this interface's. */
	error = copyin(argument, &ask, sizeof(ask));
	if (error != 0)
		return error;
	if (ask.version != AMDRDNA_INTERFACE_VERSION || ask.size != sizeof(ask))
		return EINVAL;

	/* The action, run by the bring-up thread; a refusal is the request's error. */
	result = amdrdna_hotplug_ask(device, ask.level);
	if (result == EINVAL ||
	    result == ENOENT ||
	    result == ENODEV)
		return result;

	/* The answer. */
	ask.result = result;
	ask.dark = 0U;
	if (device->display.output_dark)
		ask.dark = 1U;
	ask.width = device->display.width;
	ask.height = device->display.height;
	ask.refresh_millihz = device->display.refresh_millihz;
	error = copyout(&ask, argument, sizeof(ask));
	if (error != 0)
		return error;

	/* Succeeded: the action ran; its result is in the answer. */
	return 0;
}

/* Copies part of the video BIOS image to the caller: AMDRDNA_IOC_VBIOS. */
static int
control_vbios(
	struct amdrdna_device *device,
	uintptr_t argument)
{
	struct amdrdna_vbios_read request;
	const uint8_t *image;
	uint32_t total;
	uint32_t length;
	unsigned long irq;
	int error;

	/* The caller's version and size must be this interface's. */
	error = copyin(argument, &request, sizeof(request));
	if (error != 0)
		return error;
	if (request.version != AMDRDNA_INTERFACE_VERSION || request.size != sizeof(request))
		return EINVAL;

	/* The image exists once stage A1v has run; it is never freed while the device exists. */
	irq = spin_lock_irqsave(&device->state_lock);

	image = NULL;
	total = 0U;
	if (device->stage >= AMDRDNA_STAGE_A1V) {
		image = device->vbios;
		total = device->vbios_bytes;
	}

	spin_unlock_irqrestore(&device->state_lock, irq);

	/* Without an image there is nothing to copy. */
	if (image == NULL)
		return ENOENT;

	/* An offset past the image's end copies nothing. */
	if (request.offset > total)
		return EINVAL;

	/* Copies what the caller asked for, as far as the image goes. */
	length = total - request.offset;
	if (length > request.length)
		length = request.length;
	error = copyout(image + request.offset, (uintptr_t)request.buffer, length);
	if (error != 0)
		return error;

	/* Reports the bytes copied and the image's length. */
	request.copied = length;
	request.total = total;
	error = copyout(&request, argument, sizeof(request));
	if (error != 0)
		return error;

	/* Succeeded: the caller has the part of the image it asked for. */
	return 0;
}

/*
 * Reports the display the driver took over, its registers and its counts:
 * AMDRDNA_IOC_DISPLAY.  The registers are read once stage A1 has read the
 * firmware's chain; before, the answer is ENOENT.
 */
static int
control_display(
	struct amdrdna_device *device,
	uintptr_t argument)
{
	struct amdrdna_display_state state;
	struct amdrdna_node *node;
	unsigned long irq;
	unsigned stage;
	int error;

	/* The caller's version and size must be this interface's. */
	error = copyin(argument, &state, sizeof(state));
	if (error != 0)
		return error;
	if (state.version != AMDRDNA_INTERFACE_VERSION || state.size != sizeof(state))
		return EINVAL;

	/* The firmware's chain is known once stage A1 reported ok. */
	irq = spin_lock_irqsave(&device->state_lock);

	stage = device->stage;

	spin_unlock_irqrestore(&device->state_lock, irq);

	/* Without the firmware's chain there is no display to report. */
	if (stage < AMDRDNA_STAGE_A1 || !device->gop.lit)
		return ENOENT;

	/* The display's part, then the GPU device's. */
	kern_memset(&state, 0, sizeof(state));
	state.version = AMDRDNA_INTERFACE_VERSION;
	state.size = sizeof(state);
	amdrdna_display_report(device, &state);

	/* Registered with the display; leased while an open holds it. */
	node = device->node;
	if (node != NULL && node->display != 0) {
		mutex_lock(&node->display_lock);

		state.flags |= AMDRDNA_DISPLAY_REGISTERED;
		if (node->owner != NULL)
			state.flags |= AMDRDNA_DISPLAY_LEASED;
		if (node->broken != 0)
			state.flags |= AMDRDNA_DISPLAY_BROKEN;
		state.presents = node->presents;
		state.leases = node->leases;

		mutex_unlock(&node->display_lock);
	}

	/* Hands it to the caller. */
	error = copyout(&state, argument, sizeof(state));
	if (error != 0)
		return error;

	/* Succeeded: the caller has the display's state. */
	return 0;
}

/* Copies the picture the display shows to the caller: AMDRDNA_IOC_SCREEN, for root only. */
static int
control_screen(
	struct amdrdna_device *device,
	uintptr_t argument)
{
	struct amdrdna_screen screen;
	struct ucred *credential;
	int superuser;
	int error;

	/* Refuses a caller that is not root: the screen may show another user's windows. */
	credential = cred_current_ref();
	superuser = cred_is_superuser(credential);
	cred_release(credential);
	if (!superuser)
		return EPERM;

	/* The caller's version and size must be this interface's. */
	error = copyin(argument, &screen, sizeof(screen));
	if (error != 0)
		return error;
	if (screen.version != AMDRDNA_INTERFACE_VERSION || screen.size != sizeof(screen))
		return EINVAL;

	/* Copies the rows. */
	error = amdrdna_display_screen(device, &screen);
	if (error != 0)
		return error;

	/* Reports the picture and the bytes copied. */
	error = copyout(&screen, argument, sizeof(screen));
	if (error != 0)
		return error;

	/* Succeeded: the caller has the picture. */
	return 0;
}

/* Fills the status answer from the driver's record. */
static void
control_fill(
	struct amdrdna_device *device,
	struct amdrdna_status *status)
{
	const struct amdrdna_gop *gop;
	unsigned long irq;
	uint32_t flags;
	bool training;

	/* Starts from zero, keeping the caller's version and size. */
	kern_memset(status, 0, sizeof(*status));
	status->version = AMDRDNA_INTERFACE_VERSION;
	status->size = sizeof(*status);

	/* The stage machine, as one consistent view. */
	irq = spin_lock_irqsave(&device->state_lock);

	status->stage = device->stage;
	status->target = device->target;
	status->running = device->running;
	status->failed_stage = device->failed_stage;
	status->failed_error = device->failed_error;
	flags = 0U;
	if (device->thread_running)
		flags |= AMDRDNA_STATUS_THREAD_RUNNING;

	spin_unlock_irqrestore(&device->state_lock, irq);

	/* The identity and the windows, read by stage A0. */
	status->pci_id = ((uint32_t)AMDRDNA_PCI_VENDOR << 16) | device->product;
	status->pci_revision = device->revision;
	status->pci_subsystem = ((uint32_t)device->subvendor << 16) | device->subproduct;
	status->graphics_version = ip_version(&device->blocks.blocks[AMDRDNA_BLOCK_GC]);
	status->display_version = ip_version(&device->blocks.blocks[AMDRDNA_BLOCK_DCN]);
	status->security_version = ip_version(&device->blocks.blocks[AMDRDNA_BLOCK_PSP]);
	status->power_version = ip_version(&device->blocks.blocks[AMDRDNA_BLOCK_SMU]);
	status->vram_mib = device->vram_mib;
	status->bar0_mib = (uint32_t)(device->vram_window_bytes >> 20);
	status->rebar_capability = device->resize_sizes;
	status->rebar_control = device->resize_control;
	status->power_firmware_state = device->smu_state;
	status->secure_os_word = device->secure_os_word;
	status->boot_mailbox = device->boot_word;
	status->engine_status = device->engine_status;
	status->bios_scratch_7 = device->scratch_post;
	status->frame_buffer_base = device->frame_buffer_base;
	status->cpu_window_mib = (uint32_t)(device->layout.cpu_pool_end >> 20);

	/* The state flags of stage A0. */
	if (device->posted)
		flags |= AMDRDNA_STATUS_POSTED;
	if (device->secure_os_word != 0U)
		flags |= AMDRDNA_STATUS_SECURE_OS_ALIVE;
	if ((device->platform_ready_word & AMDRDNA_PSP_DONE) != 0U)
		flags |= AMDRDNA_STATUS_PLATFORM_READY;
	if (device->resize_capability != 0U)
		flags |= AMDRDNA_STATUS_RESIZABLE_BAR;
	if (device->boot_database.present)
		flags |= AMDRDNA_STATUS_BOOT_DATABASE;

	/* The firmware's picture, read by stage A1. */
	gop = &device->gop;
	if (gop->lit) {
		status->gop_vram_offset = gop->surface_offset;
		status->gop_timing = gop->timing;
		status->gop_plane = gop->plane;
		status->gop_output = gop->output;
		status->gop_output_kind = gop->output_kind;
		status->gop_width = gop->width;
		status->gop_height = gop->height;
		status->gop_pitch_word = gop->pitch_word;
		status->gop_format = gop->format;
		status->gop_source_width = gop->viewport_width;
		status->gop_source_height = gop->viewport_height;
		status->gop_shape = (uint32_t)gop->picture.shape;
	}

	/* Whether the loader's picture is this GPU's. */
	if (gop->loader_on_gpu)
		flags |= AMDRDNA_STATUS_GOP_ON_GPU;

	/* The video BIOS, read by stage A1v. */
	if (status->stage >= AMDRDNA_STAGE_A1V && device->vbios != NULL) {
		flags |= AMDRDNA_STATUS_VBIOS;
		status->vbios_source = device->vbios_source;
		status->vbios_length = device->vbios_bytes;
		status->firmware_capability_bits = device->vbios_info.capability_bits;
		status->firmware_table_version = ((uint32_t)device->vbios_info.firmware_format << 8) | device->vbios_info.firmware_content;
		status->power_table_id = device->vbios_info.power_table_id;
		status->shutdown_temperature = device->vbios_info.shutdown_temperature;

		/* Whether the board asks for the long memory training. */
		training = amdrdna_training_allowed(device);
		if (training)
			flags |= AMDRDNA_STATUS_TRAINING_ALLOWED;
	}

	/* The shared memory and the interrupts, set up by stage A2. */
	if (status->stage >= AMDRDNA_STAGE_A2)
		control_fill_memory(device, status);

	/* The training, the security processor and the power side, from stage A3t on. */
	if (status->stage >= AMDRDNA_STAGE_A2)
		flags |= control_fill_power(device, status);

	/* The engines, from stage A5 on (or as far as a failed A5 got). */
	if (status->stage >= AMDRDNA_STAGE_A4)
		control_fill_engines(device, status);

	/* The address spaces and the GPU device, from stage A5s on (or as far as a failed A5s got). */
	if (status->stage >= AMDRDNA_STAGE_A5)
		control_fill_spaces(device, status);

	/* Publishes the flags. */
	status->flags = flags;
}

/* Fills the status answer's part that stage A2 set up. */
static void
control_fill_memory(
	struct amdrdna_device *device,
	struct amdrdna_status *status)
{
	/* The system-memory table's reach. */
	status->gart_base = device->gart_start;
	status->gart_mib = (uint32_t)(AMDRDNA_GART_BYTES >> 20);

	/* The free VRAM of both pools, as one view. */
	mutex_lock(&device->memory_lock);

	status->cpu_pool_free_mib = (uint32_t)(device->cpu_pool.free_bytes >> 20);
	status->gpu_pool_free_mib = (uint32_t)(device->gpu_pool.free_bytes >> 20);

	mutex_unlock(&device->memory_lock);

	/* The interrupt's kind. */
	status->interrupt_kind = AMDRDNA_INTERRUPT_MSI;
	if (device->interrupts.irq.type == DRV_PCI_IRQ_MSIX)
		status->interrupt_kind = AMDRDNA_INTERRUPT_MSIX;

	/* The counts so far and the display's underflow at the end of the stage. */
	status->interrupt_signals = (uint32_t)device->interrupts.signals;
	status->vm_faults = (uint32_t)device->interrupts.faults;
	status->display_underflow = device->underflow_after;
}

/*
 * Fills the status answer's part that stages A3t, A3 and A4 set up, and
 * reports their state flags.
 */
static uint32_t
control_fill_power(
	struct amdrdna_device *device,
	struct amdrdna_status *status)
{
	struct amdrdna_thermal *thermal;
	unsigned long irq;
	uint32_t flags;

	/* The training's result. */
	status->training_result = device->training.result;
	status->training_reason = device->training.reason;
	status->training_ms = device->training.milliseconds;

	/* The security processor's side. */
	status->region_kib = device->psp.region_bytes >> 10;
	status->programs_loaded = device->psp.loaded;
	status->secure_os_version = device->psp.secure_os_version;
	status->security_warnings = device->psp.warnings;

	/* The interrupt entries read and those no block took. */
	status->interrupt_entries = (uint32_t)device->interrupts.entries;
	status->interrupt_unknown = (uint32_t)device->interrupts.unknown;

	/* The state flags of the three stages. */
	flags = 0U;
	if (device->interrupts.enabled)
		flags |= AMDRDNA_STATUS_INTERRUPTS_ON;
	if (device->psp.booted)
		flags |= AMDRDNA_STATUS_SECURE_OS_OURS;

	/* The power side, as one consistent view of what the watch publishes. */
	thermal = &device->thermal;
	irq = spin_lock_irqsave(&thermal->lock);

	status->smu_version = thermal->smu_version;
	status->smu_interface = thermal->interface_version;
	status->fan_mode = thermal->mode;
	status->fan_duty = thermal->duty_percent;
	status->fan_rpm = thermal->fan_speed;
	status->fan_driven = thermal->fan_driven;
	status->temperature = thermal->temperature;
	status->temperature_highest = thermal->highest;
	status->temperature_limit = thermal->limit;
	status->thermal_alerts = (uint32_t)(thermal->high_alerts + thermal->low_alerts + thermal->critical_alerts);
	if (thermal->faulted)
		flags |= AMDRDNA_STATUS_THERMAL_FAULT;
	if (thermal->hung)
		flags |= AMDRDNA_STATUS_SMU_HUNG;

	spin_unlock_irqrestore(&thermal->lock, irq);

	/* Succeeded: the flags of the three stages. */
	return flags;
}

/* Fills the status answer's part that stage A5 set up. */
static void
control_fill_engines(
	struct amdrdna_device *device,
	struct amdrdna_status *status)
{
	struct amdrdna_engines *engines;
	unsigned long irq;
	uint64_t interrupts;
	unsigned index;

	/* What runs. */
	engines = &device->engines;
	status->engine_flags = 0U;
	if (engines->scheduler_up)
		status->engine_flags |= AMDRDNA_RUNS_SCHEDULER;
	if (engines->graphics.up)
		status->engine_flags |= AMDRDNA_RUNS_GRAPHICS;
	if (engines->copy.up)
		status->engine_flags |= AMDRDNA_RUNS_COPY;
	if (engines->direct)
		status->engine_flags |= AMDRDNA_RUNS_DIRECT;
	status->compute_queues = engines->compute_count;

	/* The shape the stage read. */
	status->address_config = engines->address_config;
	status->backends = engines->backends;
	status->compute_units = engines->units;
	status->channels_off = engines->channels_off;

	/* The end-of-pipe interrupts of the graphics and compute rings, each read under its ring's lock. */
	irq = spin_lock_irqsave(&engines->graphics.lock);

	interrupts = engines->graphics.interrupts;

	spin_unlock_irqrestore(&engines->graphics.lock, irq);
	for (index = 0U; index < engines->compute_count; index++) {
		/* One compute ring's count. */
		irq = spin_lock_irqsave(&engines->compute[index].lock);

		interrupts += engines->compute[index].interrupts;

		spin_unlock_irqrestore(&engines->compute[index].lock, irq);
	}

	/* Publishes their sum. */
	status->end_of_pipe_interrupts = (uint32_t)interrupts;

	/* The copy engine's traps, under its ring's lock. */
	irq = spin_lock_irqsave(&engines->copy.lock);

	status->copy_traps = (uint32_t)engines->copy_traps;

	spin_unlock_irqrestore(&engines->copy.lock, irq);

	/* What the checks saw. */
	status->fences = engines->fences;
	status->copied_bytes = engines->copied_bytes;
	status->dispatch_words = engines->dispatch_words;
}

/* Fills the status answer's part that stage A5s set up. */
static void
control_fill_spaces(
	struct amdrdna_device *device,
	struct amdrdna_status *status)
{
	struct amdrdna_spaces *spaces;
	struct amdrdna_node *node;
	unsigned long irq;

	/* The spaces open and kept, and the drops, under the spaces' lock. */
	spaces = &device->spaces;
	irq = spin_lock_irqsave(&spaces->lock);

	status->spaces = (spaces->count & 0xffffU) | ((spaces->held & 0xffffU) << 16);
	status->context_flushes = (uint32_t)spaces->pool.flushes;

	spin_unlock_irqrestore(&spaces->lock, irq);

	/* The GPU device's state. */
	status->device_flags = 0U;
	node = device->node;
	if (node == NULL)
		return;
	if (node->gpu != NULL)
		status->device_flags |= AMDRDNA_DEVICE_REGISTERED;
	if (node->renderer != 0)
		status->device_flags |= AMDRDNA_DEVICE_RENDERER;
	if (node->faulted != 0 || spaces->faulted)
		status->device_flags |= AMDRDNA_DEVICE_FAULTED;
}

/* Packs an IP block's version as major << 16 | minor << 8 | step; 0 for an unlisted block. */
static uint32_t
ip_version(
	const struct amdrdna_ip *ip)
{
	uint32_t version;

	/* An unlisted block has no version. */
	if (ip->found == 0U)
		return 0U;

	/* Packs the three parts. */
	version = ((uint32_t)ip->major << 16) | ((uint32_t)ip->minor << 8) | ip->step;

	/* Succeeded: the packed version. */
	return version;
}

#endif
