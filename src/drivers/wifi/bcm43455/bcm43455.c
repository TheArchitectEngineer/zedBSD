/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * BCM43455: the device's lifecycle, its start and stop, and its network
 * device.
 *
 * The board's driver makes the device with the files it read, probes it
 * once (start without the radio, learn the identity, stop) and publishes
 * it as wlanN with a WLAN station.  Opening the interface starts the chip
 * again, with the radio up and the bus thread serving it; closing it
 * stops the thread, takes the radio down and cuts the chip's power.
 *
 * Every start makes the same settings: receive glomming off, the CLM
 * loaded, the country set to the world-wide "XX" and read back (E6),
 * roaming off, power saving off, the events the driver uses enabled.
 *
 * The radio operations (scan and connect) and the FullMAC data path are
 * not here yet: the station is attached with only quiesce and stop_retry,
 * and a received data frame is dropped (ws206-p005, with the WLAN core's
 * FullMAC engine of ws206-p008).
 */

#include "drivers/wifi/bcm43455/bcm43455-device.h"
#include "drivers/wifi/bcm43455/bcm43455-bytes.h"
#include "drivers/wifi/bcm43455/bcm43455-hw.h"
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/net/net-device.h>
#include <kern/net/packet-buf.h>
#include <uapi/errno.h>

/* The country the regulatory table is set to: world-wide (E6), the firmware's choice of revision. */
#define COUNTRY_CODE_0			'X'
#define COUNTRY_CODE_1			'X'
#define COUNTRY_REVISION_ANY		0xffffffffU
#define COUNTRY_LENGTH			12U
#define COUNTRY_ABBREVIATION		0U
#define COUNTRY_REVISION		4U
#define COUNTRY_CODE			8U

/* The firmware's power saving mode that keeps the radio awake. */
#define POWER_SAVE_OFF			0U

/* How long the station's close may stay busy before close gives up for a retry. */
#define STATION_CLOSE_TRIES		1000U

/* The bytes the valid-channel list answer takes: its count and up to the maximum. */
#define CHANNEL_LIST_LENGTH		(4U + 4U * BCM43455_CHANNELS_MAX)

/* The radio's 2.4 GHz and 5 GHz channel numbering. */
#define BAND_2G_BASE_MHZ		2407U
#define CHANNEL_14_MHZ			2484U
#define BAND_5G_BASE_MHZ		5000U

/* The parts of the station's radio interface this stage implements. */
static int radio_quiesce(void *context);
static int radio_stop_retry(void *context);

/*
 * The radio operations: only the lifecycle's two, which the WLAN layer
 * takes as a radio without connection methods.  Scan, disconnect and the
 * two FullMAC methods (fullmac_connect, ethernet_transmit) come together
 * in ws206-p005, which turns this into a FullMAC method set.
 */
static const struct wlan_radio_ops radio_operations = {
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	radio_quiesce,
	radio_stop_retry,
	NULL,
	NULL,
	NULL
};

/* The network device's operations. */
static int net_open(struct net_device *net);
static void net_close(struct net_device *net);
static int net_transmit(struct net_device *net, struct packet_buf *packet);
static unsigned net_poll(struct net_device *net, unsigned budget);
static int net_ioctl(struct net_device *net, unsigned long request, void *argument);
static void net_release(void *driver_data);

/* The network device's operations. */
static const struct net_device_ops net_operations = {
	net_open,
	net_close,
	net_transmit,
	net_poll,
	net_ioctl,
	net_release
};

/* What the bus does with what it receives. */
static void hook_event(void *context, const struct bcm43455_event *event, const uint8_t *frame, size_t length);
static void hook_data(void *context, const uint8_t *frame, size_t length);
static int hook_transmit(void *context, uint8_t *frame, size_t capacity, size_t *length);
static void hook_failed(void *context, int error);

/* What the bus does with what it receives. */
static const struct bcm43455_bus_hooks bus_hooks = {
	hook_event,
	hook_data,
	hook_transmit,
	hook_failed
};

static int ring_init(struct bcm43455_frame_ring *ring, unsigned capacity);
static void ring_release(struct bcm43455_frame_ring *ring);
static int ring_put_locked(struct bcm43455_frame_ring *ring, const uint8_t *frame, size_t length);
static int ring_take_locked(struct bcm43455_frame_ring *ring, uint8_t *frame, size_t capacity, size_t *length);
static void ring_clear_locked(struct bcm43455_frame_ring *ring);
static int start_settings(struct bcm43455_device *device);
static int load_clm(struct bcm43455_device *device);
static int set_country(struct bcm43455_device *device);
static int read_identity(struct bcm43455_device *device);
static int enable_events(struct bcm43455_device *device);
static int read_channels(struct bcm43455_device *device);
static int command_u32(struct bcm43455_device *device, uint32_t command, uint32_t value);
static int check_address(const struct bcm43455_device *device);
static int discard_unpublished(struct bcm43455_device *device);
static int close_locked(struct bcm43455_device *device);
static int station_close_wait(struct bcm43455_device *device);
static void clear_rings(struct bcm43455_device *device);
static uint32_t channel_frequency(uint32_t channel);

/*
 * Makes a device on a bus.
 */
int
bcm43455_device_create(
	const struct bcm43455_bus_ops *ops,
	void *context,
	struct bcm43455_files *files,
	struct bcm43455_device **result)
{
	struct bcm43455_device *device;
	int error;

	/* Refuses a missing bus, missing files or nowhere to put the device. */
	if (ops == NULL ||
	    files == NULL ||
	    result == NULL)
		return EINVAL;
	if (files->firmware == NULL ||
	    files->clm == NULL ||
	    files->nvram == NULL)
		return EINVAL;

	/* Allocates the device, cleared. */
	device = kern_calloc(1, sizeof(*device));
	if (device == NULL)
		return ENOMEM;
	device->ops = ops;
	device->context = context;

	/* Takes the files: the caller's description no longer owns them. */
	device->files = *files;
	files->firmware = NULL;
	files->clm = NULL;
	files->nvram = NULL;

	/* Reads the firmware's version text from the file, which names its features. */
	error = bcm43455_firmware_check(
		device->files.firmware,
		device->files.firmware_length,
		device->file_version,
		sizeof(device->file_version));
	if (error != 0)
		goto fail;

	/* Turns the NVRAM text into the chip's form once, and keeps its placeholder address. */
	error = bcm43455_nvram_convert(
		device->files.nvram,
		device->files.nvram_length,
		device->nvram,
		sizeof(device->nvram),
		&device->nvram_length);
	if (error != 0) {
		kern_logf("bcm43455: NVRAM settings not usable (%d)\n", error);
		goto fail;
	}
	error = bcm43455_nvram_mac(device->files.nvram, device->files.nvram_length, device->placeholder_mac);
	if (error == 0)
		device->has_placeholder = 1;

	/* Prepares the bus and the locks. */
	error = bcm43455_bus_init(&device->bus, &bus_hooks, device);
	if (error != 0)
		goto fail;
	error = mutex_init(&device->lifecycle_lock, LOCK_RANK_DEVICE, "bcm43455 lifecycle");
	if (error != 0)
		goto fail;
	spin_init(&device->ring_lock, LOCK_RANK_DEVICE, "bcm43455 rings");

	/* Allocates the rings between the bus thread and the network. */
	error = ring_init(&device->rx_ring, BCM43455_RX_RING_SIZE);
	if (error != 0)
		goto fail;
	error = ring_init(&device->tx_ring, BCM43455_TX_RING_SIZE);
	if (error != 0)
		goto fail;

	/* Succeeded: the device waits for its probe. */
	kern_logf("bcm43455: firmware file %s\n", device->file_version);
	*result = device;
	return 0;

fail:
	/* Gives back what was made, files included. */
	bcm43455_device_destroy(device);
	return error;
}

/*
 * Brings the chip up once to learn its identity, then powers it down.
 */
int
bcm43455_device_probe(
	struct bcm43455_device *device)
{
	int error;

	/* Refuses a missing device. */
	if (device == NULL)
		return EINVAL;

	/* Starts the chip without its radio, which reads the identity, and stops it again. */
	mutex_lock(&device->lifecycle_lock);

	error = bcm43455_device_start(device, 0);
	if (error == 0)
		bcm43455_device_stop(device);

	mutex_unlock(&device->lifecycle_lock);

	if (error != 0)
		return error;

	/* Refuses a chip without an address of its own. */
	error = check_address(device);
	if (error != 0)
		return error;

	/* Succeeded: the chip can be published. */
	kern_logf(
		"bcm43455: chip address %02x:%02x:%02x:%02x:%02x:%02x, %u channels\n",
		device->identity.mac[0],
		device->identity.mac[1],
		device->identity.mac[2],
		device->identity.mac[3],
		device->identity.mac[4],
		device->identity.mac[5],
		device->identity.profile.channel_count);
	return 0;
}

/*
 * Publishes the probed chip as wlanN and attaches its station.
 */
int
bcm43455_device_publish(
	struct bcm43455_device *device)
{
	struct net_device *net;
	struct wlan_station *station;
	unsigned index;
	int error;
	int discard_error;

	/* Refuses a missing or already published device. */
	if (device == NULL)
		return EINVAL;

	/* Holds the lifecycle across the publication, so an early open waits for the station. */
	mutex_lock(&device->lifecycle_lock);

	if (device->published) {
		mutex_unlock(&device->lifecycle_lock);
		return EALREADY;
	}

	/* Describes the network device: an Ethernet-like WLAN with the chip's address. */
	net = net_device_alloc();
	if (net == NULL) {
		mutex_unlock(&device->lifecycle_lock);
		return ENOSPC;
	}
	net->flags = NET_DEVICE_BROADCAST | NET_DEVICE_MULTICAST;
	net->mtu = BCM43455_MTU;
	kern_memcpy(net->hwaddr, device->identity.mac, sizeof(device->identity.mac));
	net->hwaddr_len = (uint8_t)sizeof(device->identity.mac);
	net->capabilities = NET_DEVICE_CAP_WLAN;
	net->ops = &net_operations;
	net->driver_data = device;

	/* Publishes it under the first free wlanN. */
	error = ENOSPC;
	for (index = 0; index < NET_DEVICE_MAX; index++) {
		kern_memcpy(net->name, "wlan", 4U);
		net->name[4] = (char)('0' + index);
		net->name[5] = '\0';
		error = net_device_create(net);
		if (error != EEXIST)
			break;
	}
	if (error != 0) {
		net->driver_data = NULL;
		net_device_destroy(net);
		mutex_unlock(&device->lifecycle_lock);

		/* A registry that is shutting down is a normal end for a device found late. */
		if (error == EBUSY)
			return ENODEV;
		return error;
	}
	device->net = net;

	/* Attaches the station with the carrier down; a WLAN layer shutting down refuses it. */
	station = NULL;
	error = net_device_set_carrier(net, 0);
	if (error == 0)
		error = wlan_station_attach(net, &radio_operations, device, &device->identity.profile, &station);
	if (error == 0 && station == NULL)
		error = EIO;
	if (error != 0) {
		discard_error = discard_unpublished(device);
		mutex_unlock(&device->lifecycle_lock);
		if (discard_error != 0)
			kern_logf("bcm43455: half-published device not withdrawn (%d)\n", discard_error);
		return error;
	}

	/* Succeeded: wlanN and its station exist for as long as the kernel runs. */
	device->station = station;
	device->published = 1;
	kern_logf("bcm43455: published as %s\n", net->name);

	mutex_unlock(&device->lifecycle_lock);
	return 0;
}

/*
 * Releases a device that was never published.
 */
void
bcm43455_device_destroy(
	struct bcm43455_device *device)
{
	/* Nothing to release. */
	if (device == NULL)
		return;

	/* Clears the converted NVRAM, then releases the rings and the files. */
	kern_memset(device->nvram, 0, sizeof(device->nvram));
	ring_release(&device->rx_ring);
	ring_release(&device->tx_ring);
	bcm43455_files_release(&device->files);
	kern_free(device);
}

/*
 * Starts the chip.
 */
int
bcm43455_device_start(
	struct bcm43455_device *device,
	int up)
{
	int error;

	/* Forgets the previous run's bus state and powers the chip from reset. */
	bcm43455_bus_reset(&device->bus);
	device->powered = 1;
	error = device->ops->power_up(device->context);
	if (error != 0) {
		kern_logf("bcm43455: chip did not power up (%d)\n", error);
		goto fail;
	}

	/* Finds the chip's cores and RAM. */
	error = bcm43455_chip_attach(&device->bus.chip, device->ops, device->context);
	if (error != 0)
		goto fail;

	/* Loads and starts the firmware. */
	error = bcm43455_chip_download(
		&device->bus.chip,
		device->files.firmware,
		device->files.firmware_length,
		device->nvram,
		device->nvram_length);
	if (error != 0)
		goto fail;
	device->firmware_running = 1;

	/* Brings the frame function up and lets the card interrupt the host. */
	error = bcm43455_chip_start(&device->bus.chip);
	if (error != 0)
		goto fail;
	device->ops->interrupt_enable(device->context, 1);

	/* Makes the settings every start makes, in direct mode. */
	bcm43455_bus_direct(&device->bus, 1);
	error = start_settings(device);
	if (error != 0)
		goto fail;

	/* A probe stops here: the radio stays down. */
	if (!up) {
		bcm43455_bus_direct(&device->bus, 0);
		return 0;
	}

	/* Brings the radio up. */
	error = command_u32(device, BCM43455_WLC_UP, 1);
	if (error != 0) {
		kern_logf("bcm43455: radio did not come up (%d)\n", error);
		goto fail;
	}

	/* Hands the bus to its thread. */
	bcm43455_bus_direct(&device->bus, 0);
	error = bcm43455_bus_start(&device->bus);
	if (error != 0)
		goto fail;

	/* Succeeded: the firmware runs and the bus thread serves it. */
	device->runtime_active = 1;
	return 0;

fail:
	/* Undoes whatever came up. */
	bcm43455_device_stop(device);
	return error;
}

/*
 * Stops the bus thread, takes the radio down and cuts the chip's power.
 */
void
bcm43455_device_stop(
	struct bcm43455_device *device)
{
	int error;

	/* Takes the bus back from its thread. */
	device->runtime_active = 0;
	error = bcm43455_bus_stop(&device->bus);
	if (error != 0)
		kern_logf("bcm43455: bus thread had stopped on error %d\n", error);

	/* Takes the radio down while the firmware still answers. */
	if (device->firmware_running) {
		bcm43455_bus_direct(&device->bus, 1);
		error = command_u32(device, BCM43455_WLC_DOWN, 1);
		if (error != 0 && error != ENETDOWN)
			kern_logf("bcm43455: radio not taken down (%d)\n", error);
		bcm43455_bus_direct(&device->bus, 0);
		device->firmware_running = 0;
	}

	/* Keeps the card's interrupt away and cuts the chip's power. */
	if (device->powered) {
		device->ops->interrupt_enable(device->context, 0);
		device->ops->power_down(device->context);
		device->powered = 0;
	}

	/* Drops what the rings still hold, and the bus's frame copies. */
	clear_rings(device);
	bcm43455_bus_reset(&device->bus);
}

/*
 * Queues an Ethernet frame for the firmware to send.
 */
int
bcm43455_device_ethernet_transmit(
	struct bcm43455_device *device,
	const uint8_t *frame,
	size_t length)
{
	unsigned long enabled;
	int error;

	/* Refuses a frame the data channel cannot carry. */
	if (frame == NULL || length == 0)
		return EINVAL;
	if (length > BCM43455_BUS_ETHERNET_MAX)
		return EMSGSIZE;

	/* Queues it while the bus thread serves the device. */
	enabled = spin_lock_irqsave(&device->ring_lock);

	if (!device->runtime_active)
		error = ENETDOWN;
	else
		error = ring_put_locked(&device->tx_ring, frame, length);

	spin_unlock_irqrestore(&device->ring_lock, enabled);

	if (error != 0)
		return error;

	/* Wakes the bus thread to send it. */
	bcm43455_bus_wake(&device->bus);

	/* Succeeded: the bus thread sends the frame within the firmware's credit. */
	return 0;
}

/*
 * Takes the oldest received Ethernet frame.
 */
int
bcm43455_device_ethernet_receive(
	struct bcm43455_device *device,
	uint8_t *frame,
	size_t capacity,
	size_t *length)
{
	unsigned long enabled;
	int taken;

	/* Takes the frame out of the ring. */
	enabled = spin_lock_irqsave(&device->ring_lock);

	taken = ring_take_locked(&device->rx_ring, frame, capacity, length);

	spin_unlock_irqrestore(&device->ring_lock, enabled);

	/* Reports whether a frame was taken. */
	return taken;
}

/*
 * Builds the scan profile from the firmware's list of valid channels.
 */
int
bcm43455_device_scan_profile(
	const uint32_t *channels,
	unsigned count,
	struct wlan_scan_profile *profile)
{
	struct wlan_scan_channel *entry;
	uint32_t frequency;
	unsigned i;
	unsigned j;
	int repeated;

	/* Refuses missing buffers. */
	if (channels == NULL || profile == NULL)
		return EINVAL;

	/* Keeps each known channel once, in the firmware's order. */
	kern_memset(profile, 0, sizeof(*profile));
	for (i = 0; i < count; i++) {
		/* A channel the WLAN station does not know is left out. */
		frequency = channel_frequency(channels[i]);
		if (frequency == 0)
			continue;

		/* A channel listed twice is kept once. */
		repeated = 0;
		for (j = 0; j < profile->channel_count; j++) {
			if (profile->channels[j].channel == channels[i])
				repeated = 1;
		}
		if (repeated)
			continue;

		/* The profile is full. */
		if (profile->channel_count == WLAN_SCAN_CHANNEL_MAX)
			break;

		/* The firmware dwells on the channel itself; the host never sends a probe. */
		entry = &profile->channels[profile->channel_count];
		entry->channel = channels[i];
		entry->center_frequency_mhz = frequency;
		entry->flags = WLAN_SCAN_CHANNEL_OFFLOADED_DWELL;
		profile->channel_count++;
	}

	/* A list without one known channel gives no profile. */
	if (profile->channel_count == 0)
		return EINVAL;

	/* Succeeded: the profile holds the firmware's channels. */
	return 0;
}

/*
 * Reports whether a space-separated capability list names a capability.
 */
int
bcm43455_device_has_capability(
	const char *list,
	const char *name)
{
	size_t start;
	size_t end;
	size_t length;
	int same;

	/* Measures the name. */
	length = 0;
	while (name[length] != '\0')
		length++;
	if (length == 0)
		return 0;

	/* Compares the name with every word of the list. */
	start = 0;
	for (;;) {
		/* Skips the spaces before the word. */
		while (list[start] == ' ' ||
		       list[start] == '\n' ||
		       list[start] == '\t')
			start++;
		if (list[start] == '\0')
			return 0;

		/* Finds the word's end. */
		end = start;
		while (list[end] != '\0' &&
		       list[end] != ' ' &&
		       list[end] != '\n' &&
		       list[end] != '\t')
			end++;

		/* A word of the same length and letters is the capability. */
		if (end - start == length) {
			same = kern_memcmp(list + start, name, length);
			if (same == 0)
				return 1;
		}
		start = end;
	}
}

/*
 * Reports whether a firmware build's version text names a feature.
 *
 * The text is "43455c0-roml/feature-feature-... Version: ..."; a feature
 * is a word between the slash, the hyphens and the first space.
 */
int
bcm43455_device_has_feature(
	const char *version,
	const char *name)
{
	size_t start;
	size_t end;
	size_t length;
	int same;

	/* Measures the name. */
	length = 0;
	while (name[length] != '\0')
		length++;
	if (length == 0)
		return 0;

	/* The features start after the slash. */
	start = 0;
	while (version[start] != '\0' && version[start] != '/')
		start++;
	if (version[start] == '\0')
		return 0;
	start++;

	/* Compares the name with every feature up to the first space. */
	for (;;) {
		/* Finds the feature's end. */
		end = start;
		while (version[end] != '\0' &&
		       version[end] != '-' &&
		       version[end] != ' ')
			end++;

		/* A feature of the same length and letters is the one asked for. */
		if (end - start == length) {
			same = kern_memcmp(version + start, name, length);
			if (same == 0)
				return 1;
		}

		/* The list ends at the first space or the end of the text. */
		if (version[end] != '-')
			return 0;
		start = end + 1U;
	}
}

/*
 * Makes the settings every start makes and reads the identity.
 */
static int
start_settings(
	struct bcm43455_device *device)
{
	int error;

	/* Turns receive glomming off, so every frame arrives alone; an older firmware may not know it. */
	error = bcm43455_bus_iovar_set_u32(&device->bus, "bus:rxglom", 0);
	if (error != 0)
		kern_logf("bcm43455: receive glomming not turned off (%d)\n", error);

	/* Loads the regulatory table, then picks the world-wide country from it. */
	error = load_clm(device);
	if (error != 0)
		return error;
	error = set_country(device);
	if (error != 0)
		return error;

	/* Reads the address, the version and the capabilities. */
	error = read_identity(device);
	if (error != 0)
		return error;

	/* Asks for the events the driver uses. */
	error = enable_events(device);
	if (error != 0)
		return error;

	/* Lets the host decide on roaming, which it does not do. */
	error = bcm43455_bus_iovar_set_u32(&device->bus, "roam_off", 1);
	if (error != 0) {
		kern_logf("bcm43455: roaming not turned off (%d)\n", error);
		return error;
	}

	/* Keeps the radio out of power saving. */
	error = command_u32(device, BCM43455_WLC_SET_PM, POWER_SAVE_OFF);
	if (error != 0) {
		kern_logf("bcm43455: power saving not turned off (%d)\n", error);
		return error;
	}

	/* Reads the channels the firmware allows. */
	error = read_channels(device);
	if (error != 0)
		return error;

	/* Succeeded: the firmware is set up. */
	return 0;
}

/*
 * Sends the CLM to the firmware in clmload messages and checks that it was
 * taken.
 */
static int
load_clm(
	struct bcm43455_device *device)
{
	uint8_t message[BCM43455_CLM_HEADER_LENGTH + BCM43455_CLM_CHUNK_MAX];
	uint8_t status_bytes[4];
	size_t offset;
	size_t message_length;
	size_t taken;
	size_t answered;
	uint32_t status;
	unsigned messages;
	int error;

	/* Sends the table a message at a time. */
	offset = 0;
	messages = 0;
	while (offset < device->files.clm_length) {
		/* Builds the message that carries the next piece. */
		error = bcm43455_clm_message(
			message,
			sizeof(message),
			device->files.clm,
			device->files.clm_length,
			offset,
			&message_length,
			&taken);
		if (error != 0)
			return error;

		/* Sends it. */
		error = bcm43455_bus_iovar_set(&device->bus, "clmload", message, message_length);
		if (error != 0) {
			kern_logf("bcm43455: CLM piece at %u refused (%d)\n", (unsigned)offset, error);
			return error;
		}
		offset += taken;
		messages++;
	}

	/* Asks the firmware whether it took the table. */
	answered = 0;
	error = bcm43455_bus_iovar_get(&device->bus, "clmload_status", status_bytes, sizeof(status_bytes), &answered);
	if (error != 0) {
		kern_logf("bcm43455: CLM status not read (%d)\n", error);
		return error;
	}
	if (answered < sizeof(status_bytes))
		return EIO;
	status = bcm43455_get_le32(status_bytes);
	if (status != 0) {
		kern_logf("bcm43455: CLM refused (status %u)\n", status);
		return EIO;
	}

	/* Succeeded: the regulatory table is loaded. */
	kern_logf("bcm43455: CLM loaded (%u bytes in %u messages)\n", (unsigned)device->files.clm_length, messages);
	return 0;
}

/*
 * Sets the country to the world-wide "XX" and logs what the firmware
 * reads back, which is the regulatory domain it then runs in.
 */
static int
set_country(
	struct bcm43455_device *device)
{
	uint8_t country[COUNTRY_LENGTH];
	uint8_t answer[COUNTRY_LENGTH];
	size_t answered;
	int error;

	/* The abbreviation and the code are both XX; the revision is the firmware's choice. */
	kern_memset(country, 0, sizeof(country));
	country[COUNTRY_ABBREVIATION] = (uint8_t)COUNTRY_CODE_0;
	country[COUNTRY_ABBREVIATION + 1U] = (uint8_t)COUNTRY_CODE_1;
	bcm43455_put_le32(country + COUNTRY_REVISION, COUNTRY_REVISION_ANY);
	country[COUNTRY_CODE] = (uint8_t)COUNTRY_CODE_0;
	country[COUNTRY_CODE + 1U] = (uint8_t)COUNTRY_CODE_1;

	/* Sets it. */
	error = bcm43455_bus_iovar_set(&device->bus, "country", country, sizeof(country));
	if (error != 0) {
		kern_logf("bcm43455: country XX not set (%d)\n", error);
		return error;
	}

	/* Reads it back and logs it. */
	kern_memset(answer, 0, sizeof(answer));
	answered = 0;
	error = bcm43455_bus_iovar_get(&device->bus, "country", answer, sizeof(answer), &answered);
	if (error != 0) {
		kern_logf("bcm43455: country not read back (%d)\n", error);
		return error;
	}
	if (answered < sizeof(answer))
		return EIO;
	kern_logf(
		"bcm43455: country %c%c revision %d (code %c%c)\n",
		answer[COUNTRY_ABBREVIATION],
		answer[COUNTRY_ABBREVIATION + 1U],
		(int)bcm43455_get_le32(answer + COUNTRY_REVISION),
		answer[COUNTRY_CODE],
		answer[COUNTRY_CODE + 1U]);

	/* Refuses a firmware that settled on another country. */
	if (answer[COUNTRY_ABBREVIATION] != COUNTRY_CODE_0 || answer[COUNTRY_ABBREVIATION + 1U] != COUNTRY_CODE_1)
		return EIO;

	/* Succeeded: the radio runs world-wide. */
	return 0;
}

/*
 * Reads the chip's address, the firmware's version text and its
 * capabilities, and whether it has the in-firmware supplicant.
 */
static int
read_identity(
	struct bcm43455_device *device)
{
	struct bcm43455_identity *identity;
	uint8_t text[BCM43455_BCDC_CONTROL_MAX];
	size_t answered;
	size_t copied;
	int in_file;
	int in_list;
	int error;

	identity = &device->identity;

	/* The address from the chip's OTP. */
	answered = 0;
	error = bcm43455_bus_iovar_get(&device->bus, "cur_etheraddr", identity->mac, sizeof(identity->mac), &answered);
	if (error != 0) {
		kern_logf("bcm43455: chip address not read (%d)\n", error);
		return error;
	}
	if (answered < sizeof(identity->mac))
		return EIO;

	/* The version text of the running firmware. */
	kern_memset(text, 0, sizeof(text));
	answered = 0;
	error = bcm43455_bus_iovar_get(&device->bus, "ver", text, sizeof(text) - 1U, &answered);
	if (error != 0) {
		kern_logf("bcm43455: firmware version not read (%d)\n", error);
		return error;
	}
	copied = answered;
	if (copied > sizeof(identity->version) - 1U)
		copied = sizeof(identity->version) - 1U;
	kern_memcpy(identity->version, text, copied);
	identity->version[copied] = '\0';
	kern_logf("bcm43455: firmware %s\n", identity->version);

	/* The capability list. */
	kern_memset(text, 0, sizeof(text));
	answered = 0;
	error = bcm43455_bus_iovar_get(&device->bus, "cap", text, sizeof(text) - 1U, &answered);
	if (error != 0) {
		kern_logf("bcm43455: capabilities not read (%d)\n", error);
		return error;
	}
	kern_memcpy(identity->capabilities, text, answered);
	identity->capabilities[answered] = '\0';
	kern_logf("bcm43455: capabilities %s\n", identity->capabilities);

	/* The supplicant must be named by both the file's build features and the running list. */
	in_file = bcm43455_device_has_feature(device->file_version, "idsup");
	in_list = bcm43455_device_has_capability(identity->capabilities, "idsup");
	identity->supplicant = 0;
	if (in_file && in_list)
		identity->supplicant = 1;
	if (!identity->supplicant) {
		kern_logf(
			"bcm43455: firmware has no supplicant (idsup: build %d, capabilities %d); connecting will be refused\n",
			in_file,
			in_list);
	}

	/* Succeeded: the identity is known. */
	return 0;
}

/*
 * Asks the firmware for the events the driver uses.
 */
static int
enable_events(
	struct bcm43455_device *device)
{
	static const unsigned events[] = {
		BCM43455_EVENT_SET_SSID,
		BCM43455_EVENT_AUTH,
		BCM43455_EVENT_DEAUTH,
		BCM43455_EVENT_DEAUTH_IND,
		BCM43455_EVENT_ASSOC,
		BCM43455_EVENT_DISASSOC_IND,
		BCM43455_EVENT_LINK,
		BCM43455_EVENT_PSK_SUP,
		BCM43455_EVENT_ESCAN_RESULT
	};
	uint8_t mask[BCM43455_EVENT_MASK_LENGTH];
	unsigned i;
	int error;

	/* Sets each event's bit. */
	kern_memset(mask, 0, sizeof(mask));
	for (i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
		error = bcm43455_event_mask_add(mask, events[i]);
		if (error != 0)
			return error;
	}

	/* Hands the mask to the firmware. */
	error = bcm43455_bus_iovar_set(&device->bus, "event_msgs", mask, sizeof(mask));
	if (error != 0) {
		kern_logf("bcm43455: event mask not set (%d)\n", error);
		return error;
	}

	/* Succeeded: the firmware reports the events. */
	return 0;
}

/*
 * Reads the channels the firmware allows into the scan profile.
 */
static int
read_channels(
	struct bcm43455_device *device)
{
	uint8_t list[CHANNEL_LIST_LENGTH];
	uint8_t request[4];
	uint32_t channels[BCM43455_CHANNELS_MAX];
	size_t answered;
	uint32_t count;
	unsigned i;
	int error;

	/* Asks for the list, telling the firmware how many entries fit. */
	bcm43455_put_le32(request, BCM43455_CHANNELS_MAX);
	answered = 0;
	error = bcm43455_bus_control(
		&device->bus,
		BCM43455_WLC_GET_VALID_CHANNELS,
		0,
		request,
		sizeof(request),
		list,
		sizeof(list),
		&answered);
	if (error != 0) {
		kern_logf("bcm43455: channel list not read (%d)\n", error);
		return error;
	}

	/* The answer is a count and that many channel numbers. */
	if (answered < 4U)
		return EIO;
	count = bcm43455_get_le32(list);
	if (count > BCM43455_CHANNELS_MAX || answered < 4U + 4U * (size_t)count)
		return EIO;
	for (i = 0; i < count; i++)
		channels[i] = bcm43455_get_le32(list + 4U + 4U * i);

	/* Keeps the channels the WLAN station knows. */
	error = bcm43455_device_scan_profile(channels, (unsigned)count, &device->identity.profile);
	if (error != 0) {
		kern_logf("bcm43455: none of the %u channels is usable\n", count);
		return error;
	}

	/* Succeeded: the scan profile is the firmware's. */
	kern_logf("bcm43455: %u valid channels, %u scanned\n", count, device->identity.profile.channel_count);
	return 0;
}

/* Sets a firmware command that takes one 32-bit number. */
static int
command_u32(
	struct bcm43455_device *device,
	uint32_t command,
	uint32_t value)
{
	uint8_t bytes[4];
	int error;

	/* The number is little-endian. */
	bcm43455_put_le32(bytes, value);
	error = bcm43455_bus_control(&device->bus, command, 1, bytes, sizeof(bytes), NULL, 0, NULL);
	if (error != 0)
		return error;

	/* Succeeded: the firmware took the command. */
	return 0;
}

/*
 * Refuses an address that is not the chip's own: all zeros, multicast, or
 * the placeholder every board's NVRAM carries.
 */
static int
check_address(
	const struct bcm43455_device *device)
{
	const uint8_t *mac;
	unsigned i;
	int zero;
	int same;

	mac = device->identity.mac;

	/* An address of all zeros was never programmed. */
	zero = 1;
	for (i = 0; i < 6U; i++) {
		if (mac[i] != 0)
			zero = 0;
	}
	if (zero) {
		kern_logf("bcm43455: chip has no address\n");
		return EADDRNOTAVAIL;
	}

	/* A multicast address cannot name one interface. */
	if ((mac[0] & 0x01U) != 0) {
		kern_logf("bcm43455: chip address is multicast\n");
		return EADDRNOTAVAIL;
	}

	/* The NVRAM's placeholder is shared by every board. */
	if (device->has_placeholder) {
		same = kern_memcmp(mac, device->placeholder_mac, 6U);
		if (same == 0) {
			kern_logf("bcm43455: chip reports the NVRAM's placeholder address\n");
			return EADDRNOTAVAIL;
		}
	}

	/* Succeeded: the address is the chip's own. */
	return 0;
}

/*
 * Withdraws a network device whose station did not attach.  The caller
 * holds lifecycle_lock, which is let go around the network's calls: they
 * may wait for an open, which takes it.
 */
static int
discard_unpublished(
	struct bcm43455_device *device)
{
	struct net_device *net;
	int error;

	net = device->net;
	device->net = NULL;
	if (net == NULL)
		return 0;

	mutex_unlock(&device->lifecycle_lock);

	/* Takes it out of the registry, then destroys it. */
	(void)net_device_set_carrier(net, 0);
	error = net_device_gone(net);
	if (error == 0) {
		net->driver_data = NULL;
		net_device_destroy(net);
	}

	mutex_lock(&device->lifecycle_lock);

	/* Reports a device the registry kept. */
	if (error != 0)
		return error;

	/* Succeeded: the device is gone. */
	return 0;
}

/*
 * Closes the station, stops the chip under the station's quiesce barrier
 * and lets the station reconcile.  The caller holds lifecycle_lock, which
 * is let go around the station's close: the WLAN layer takes the station's
 * control gate there, and the gate comes before lifecycle_lock (the
 * station's quiesce, called under the gate, takes lifecycle_lock).
 */
static int
close_locked(
	struct bcm43455_device *device)
{
	int error;

	/* Retires the station's scan and connection first. */
	mutex_unlock(&device->lifecycle_lock);

	error = station_close_wait(device);

	mutex_lock(&device->lifecycle_lock);

	if (error != 0)
		return error;

	/* Stops the chip while the station admits nothing. */
	error = wlan_station_quiesce_begin(device->station);
	if (error != 0)
		return error;
	bcm43455_device_stop(device);
	wlan_station_quiesce_end(device->station);

	/* Lets the station see the stopped radio and leave its closing state. */
	mutex_unlock(&device->lifecycle_lock);

	error = station_close_wait(device);

	mutex_lock(&device->lifecycle_lock);

	if (error != 0)
		return error;

	/* Succeeded: the chip is off and the station down. */
	return 0;
}

/*
 * Closes the station, retrying while an admitted caller is still inside.
 */
static int
station_close_wait(
	struct bcm43455_device *device)
{
	unsigned tries;
	int error;

	/* Retries a busy close a millisecond apart, up to about a second. */
	for (tries = 0;
	     ;
	     tries++) {
		error = wlan_station_close(device->station);
		if (error != EBUSY)
			break;
		if (tries == STATION_CLOSE_TRIES)
			break;
		(void)device->ops->sleep(device->context, 1);
	}

	/* Reports a close that did not finish. */
	if (error != 0)
		return error;

	/* Succeeded: the station is down. */
	return 0;
}

/* Drops what both rings hold. */
static void
clear_rings(
	struct bcm43455_device *device)
{
	unsigned long enabled;

	/* Empties the rings, clearing the frames' bytes. */
	enabled = spin_lock_irqsave(&device->ring_lock);

	ring_clear_locked(&device->rx_ring);
	ring_clear_locked(&device->tx_ring);

	spin_unlock_irqrestore(&device->ring_lock, enabled);
}

/*
 * The station's quiesce: stops the chip if it still runs.  Called by the
 * WLAN layer's detach and shutdown with the station's control gate held.
 */
static int
radio_quiesce(
	void *context)
{
	struct bcm43455_device *device;

	device = context;

	/* Stops a chip that still runs; a closed one has nothing left. */
	mutex_lock(&device->lifecycle_lock);

	if (device->runtime_active || device->powered)
		bcm43455_device_stop(device);

	mutex_unlock(&device->lifecycle_lock);

	/* Succeeded: no producer is left. */
	return 0;
}

/*
 * The station's retry of a close that failed: runs the close again.
 */
static int
radio_stop_retry(
	void *context)
{
	struct bcm43455_device *device;
	int error;

	device = context;

	/* Retries the whole close. */
	mutex_lock(&device->lifecycle_lock);

	error = close_locked(device);

	mutex_unlock(&device->lifecycle_lock);

	/* Reports a close that is still not done. */
	if (error != 0)
		return error;

	/* Succeeded: the radio is stopped and the station reconciled. */
	return 0;
}

/*
 * Opens the interface: starts the chip with its radio up and opens the
 * station.
 */
static int
net_open(
	struct net_device *net)
{
	struct bcm43455_device *device;
	int busy;
	int error;

	/* Refuses a device without its driver. */
	if (net == NULL || net->driver_data == NULL)
		return ENODEV;
	device = net->driver_data;

	mutex_lock(&device->lifecycle_lock);

	/* Refuses an open before the station is attached, or while a retry still owns it. */
	if (device->station == NULL) {
		mutex_unlock(&device->lifecycle_lock);
		return ENODEV;
	}
	busy = wlan_station_stop_busy(device->station);
	if (busy) {
		mutex_unlock(&device->lifecycle_lock);
		return EBUSY;
	}

	/* Starts the chip with its radio and its bus thread, then the station. */
	error = 0;
	if (!device->runtime_active)
		error = bcm43455_device_start(device, 1);
	if (error == 0) {
		error = wlan_station_open(device->station);
		if (error != 0)
			bcm43455_device_stop(device);
	}

	mutex_unlock(&device->lifecycle_lock);

	/* Reports why the interface did not open. */
	if (error != 0) {
		kern_logf("bcm43455: %s not opened (%d)\n", net->name, error);
		return error;
	}

	/* Succeeded: the radio is up. */
	return 0;
}

/*
 * Closes the interface: closes the station and stops the chip.
 */
static void
net_close(
	struct net_device *net)
{
	struct bcm43455_device *device;
	int error;

	/* Drops the carrier first. */
	if (net == NULL || net->driver_data == NULL)
		return;
	(void)net_device_set_carrier(net, 0);
	device = net->driver_data;

	mutex_lock(&device->lifecycle_lock);

	/* A failed close is left to the station's retry. */
	if (device->station != NULL) {
		wlan_station_stop_request(device->station);
		error = close_locked(device);
		wlan_station_stop_complete(device->station, error);
		if (error != 0)
			kern_logf("bcm43455: close left for a retry (%d)\n", error);
	}

	mutex_unlock(&device->lifecycle_lock);
}

/*
 * Sends a packet through the station, which decides whether the link
 * carries it.
 */
static int
net_transmit(
	struct net_device *net,
	struct packet_buf *packet)
{
	struct bcm43455_device *device;
	int error;

	/* Refuses a packet without a device, freeing it. */
	if (packet == NULL)
		return EINVAL;
	if (net == NULL || net->driver_data == NULL) {
		packet_buf_free(packet);
		return ENODEV;
	}
	device = net->driver_data;
	if (device->station == NULL) {
		packet_buf_free(packet);
		return ENODEV;
	}

	/* The station consumes the packet whatever it decides. */
	error = wlan_station_transmit(device->station, packet);
	if (error != 0)
		return error;

	/* Succeeded: the packet is queued. */
	return 0;
}

/*
 * Takes received frames off the ring.  Until the FullMAC data path exists
 * (ws206-p005) they are counted and dropped.
 */
static unsigned
net_poll(
	struct net_device *net,
	unsigned budget)
{
	struct bcm43455_device *device;
	uint8_t frame[BCM43455_BUS_ETHERNET_MAX];
	size_t length;
	unsigned done;
	int taken;

	/* Refuses a device without its driver. */
	if (net == NULL || net->driver_data == NULL)
		return 0;
	device = net->driver_data;

	/* Drains up to the budget. */
	for (done = 0; done < budget; done++) {
		taken = bcm43455_device_ethernet_receive(device, frame, sizeof(frame), &length);
		if (!taken)
			break;
		net->rx_dropped++;
	}
	kern_memset(frame, 0, sizeof(frame));

	/* Reports how many frames were taken. */
	return done;
}

/* Hands the WLAN requests to the station. */
static int
net_ioctl(
	struct net_device *net,
	unsigned long request,
	void *argument)
{
	int error;

	/* The station answers every WLAN request. */
	error = wlan_station_ioctl(net, request, argument);
	if (error != 0)
		return error;

	/* Succeeded: the station answered. */
	return 0;
}

/* The device lives for as long as the kernel runs; its network device's end frees nothing. */
static void
net_release(
	void *driver_data)
{
	(void)driver_data;
}

/*
 * The bus's event hook.  Until the radio operations exist (ws206-p005)
 * events are counted, and the first few after each start are logged.
 */
static void
hook_event(
	void *context,
	const struct bcm43455_event *event,
	const uint8_t *frame,
	size_t length)
{
	struct bcm43455_device *device;
	unsigned long enabled;
	int log;

	(void)frame;
	(void)length;

	device = context;

	/* Counts the event and decides whether it is one of the first few. */
	log = 0;
	enabled = spin_lock_irqsave(&device->ring_lock);

	device->events++;
	if (device->events_logged < BCM43455_EVENT_LOG_LIMIT) {
		device->events_logged++;
		log = 1;
	}

	spin_unlock_irqrestore(&device->ring_lock, enabled);

	/* Logs it for the board's first bring-up. */
	if (log)
		kern_logf("bcm43455: event %u status %u reason %u\n", event->type, event->status, event->reason);
}

/* The bus's data hook: keeps the frame for the poll and schedules it. */
static void
hook_data(
	void *context,
	const uint8_t *frame,
	size_t length)
{
	struct bcm43455_device *device;
	unsigned long enabled;
	struct net_device *net;
	int error;

	device = context;

	/* Queues a copy; a full ring drops it. */
	enabled = spin_lock_irqsave(&device->ring_lock);

	error = ring_put_locked(&device->rx_ring, frame, length);
	net = device->net;

	spin_unlock_irqrestore(&device->ring_lock, enabled);

	/* Lets the network worker take it. */
	if (error == 0 && net != NULL)
		net_device_schedule_poll(net);
}

/* The bus's transmit hook: hands the oldest queued frame to the bus thread. */
static int
hook_transmit(
	void *context,
	uint8_t *frame,
	size_t capacity,
	size_t *length)
{
	struct bcm43455_device *device;
	unsigned long enabled;
	int taken;

	device = context;

	/* Takes it out of the ring. */
	enabled = spin_lock_irqsave(&device->ring_lock);

	taken = ring_take_locked(&device->tx_ring, frame, capacity, length);

	spin_unlock_irqrestore(&device->ring_lock, enabled);

	/* Reports whether a frame was given. */
	return taken;
}

/*
 * The bus's failure hook.  Records the error; the link loss and the
 * restart are reported by the radio operations (ws206-p005).
 */
static void
hook_failed(
	void *context,
	int error)
{
	struct bcm43455_device *device;
	unsigned long enabled;

	device = context;

	/* Keeps the first error. */
	enabled = spin_lock_irqsave(&device->ring_lock);

	if (device->bus_error == 0)
		device->bus_error = error;

	spin_unlock_irqrestore(&device->ring_lock, enabled);
}

/* Allocates a ring's frames. */
static int
ring_init(
	struct bcm43455_frame_ring *ring,
	unsigned capacity)
{
	/* Allocates the frames, cleared. */
	ring->entries = kern_calloc(capacity, sizeof(*ring->entries));
	if (ring->entries == NULL)
		return ENOMEM;
	ring->capacity = capacity;
	ring->head = 0;
	ring->count = 0;
	ring->dropped = 0;

	/* Succeeded: the ring is empty. */
	return 0;
}

/* Releases a ring's frames. */
static void
ring_release(
	struct bcm43455_frame_ring *ring)
{
	/* A ring never allocated has nothing to release. */
	if (ring->entries == NULL)
		return;

	/* Clears and frees the frames. */
	kern_memset(ring->entries, 0, (size_t)ring->capacity * sizeof(*ring->entries));
	kern_free(ring->entries);
	ring->entries = NULL;
	ring->capacity = 0;
	ring->head = 0;
	ring->count = 0;
}

/* Copies a frame to the ring's tail; ENOBUFS when it is full. */
static int
ring_put_locked(
	struct bcm43455_frame_ring *ring,
	const uint8_t *frame,
	size_t length)
{
	struct bcm43455_frame_entry *entry;
	unsigned tail;

	/* Refuses a frame too long for an entry, and counts it. */
	if (length > BCM43455_BUS_ETHERNET_MAX) {
		ring->dropped++;
		return EMSGSIZE;
	}

	/* A full ring drops the frame. */
	if (ring->count == ring->capacity) {
		ring->dropped++;
		return ENOBUFS;
	}

	/* Copies it into the entry after the last one. */
	tail = (ring->head + ring->count) % ring->capacity;
	entry = &ring->entries[tail];
	kern_memcpy(entry->bytes, frame, length);
	entry->length = length;
	ring->count++;

	/* Succeeded: the frame waits in the ring. */
	return 0;
}

/* Copies the ring's oldest frame out and clears its entry; returns 1 when one was taken. */
static int
ring_take_locked(
	struct bcm43455_frame_ring *ring,
	uint8_t *frame,
	size_t capacity,
	size_t *length)
{
	struct bcm43455_frame_entry *entry;
	size_t copied;

	/* An empty ring gives nothing. */
	if (ring->count == 0)
		return 0;

	/* Copies the oldest frame, cut to the caller's room. */
	entry = &ring->entries[ring->head];
	copied = entry->length;
	if (copied > capacity)
		copied = capacity;
	kern_memcpy(frame, entry->bytes, copied);
	*length = copied;

	/* Clears the entry and moves past it. */
	kern_memset(entry->bytes, 0, entry->length);
	entry->length = 0;
	ring->head = (ring->head + 1U) % ring->capacity;
	ring->count--;

	/* Reports the frame taken. */
	return 1;
}

/* Drops every frame of a ring and clears their bytes. */
static void
ring_clear_locked(
	struct bcm43455_frame_ring *ring)
{
	struct bcm43455_frame_entry *entry;

	/* Clears the entries from the oldest on. */
	while (ring->count != 0) {
		entry = &ring->entries[ring->head];
		kern_memset(entry->bytes, 0, entry->length);
		entry->length = 0;
		ring->head = (ring->head + 1U) % ring->capacity;
		ring->count--;
	}
	ring->head = 0;
}

/*
 * The centre frequency of a channel the WLAN station knows, or 0: the
 * same numbering the station's scan profile accepts.
 */
static uint32_t
channel_frequency(
	uint32_t channel)
{
	/* The 2.4 GHz channels 1 to 13, five megahertz apart, and 14 apart from them. */
	if (channel >= 1U && channel <= 13U)
		return BAND_2G_BASE_MHZ + 5U * channel;
	if (channel == 14U)
		return CHANNEL_14_MHZ;

	/* The 5 GHz channels 36 to 144 and 149 to 181, twenty megahertz apart. */
	if (channel >= 36U &&
	    channel <= 144U &&
	    ((channel - 36U) % 4U) == 0)
		return BAND_5G_BASE_MHZ + 5U * channel;
	if (channel >= 149U &&
	    channel <= 181U &&
	    ((channel - 149U) % 4U) == 0)
		return BAND_5G_BASE_MHZ + 5U * channel;

	/* Any other number is not a channel the station scans. */
	return 0;
}
