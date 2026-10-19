/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The riscv64 UEFI loader.
 *
 * It reads zedbsd.cfg from the volume it was loaded from, then the kernel
 * the file names and the initial ramdisk root.img beside it.  It copies the
 * firmware's device tree, asks the firmware for the boot hart, fills the
 * handoff, exits boot services and enters the kernel at its physical entry
 * with translation off.  The kernel builds its own page tables, so the
 * loader builds none.
 */

#include "include/uefi.h"
#include "elf64.h"
#include "volume-discovery.h"
#include "zedbsd-config.h"
#include "bootloader/include/riscv64-handoff.h"
#include "bootloader/include/riscv64-kernel-image.h"

/* The page size the firmware allocates in. */
#define PAGE_SIZE			4096ULL

/* The largest kernel file and ramdisk the loader reads. */
#define MAX_KERNEL_FILE			0x04000000ULL
#define MAX_INITRD_FILE			0x40000000ULL

/* The largest device tree the loader copies; the HAL reads no more. */
#define MAX_FDT_BYTES			(2U * 1024U * 1024U)

/*
 * The first word of a flattened device tree, read byte by byte: the
 * big-endian magic 0xd00dfeed, then the tree's total size.
 *
 * Values referenced: Devicetree Specification (devicetree.org), the
 * flattened format's header (magic, totalsize).
 */
#define FDT_MAGIC			0xd00dfeedU
#define FDT_TOTALSIZE_OFFSET		4U

/* The sizes of the loader's file buffers. */
#define BOOT_SECTOR_BUFFER		4096U
#define CONFIG_READ_BUFFER		(ZBL_KERN_CONFIG_FILE_MAX + 1U)
#define FILE_INFO_BUFFER		1024U

/* The UTF-16 storage of a file path with its leading backslash and terminator. */
#define PATH_CHAR16_STORAGE		(ZBL_KERN_CONFIG_KERNEL_PATH_STORAGE_SIZE + 1U)

/* How many times ExitBootServices is tried when the map changed in between. */
#define EXIT_ATTEMPTS			3U

/*
 * What every step of the loader needs: the firmware's tables and the
 * volume the loader came from.
 *
 * efi_main() owns the one instance for the whole run.
 */
struct loader_context {
	EFI_HANDLE image;
	EFI_SYSTEM_TABLE *system;
	EFI_BOOT_SERVICES *boot;
	EFI_FILE_PROTOCOL *root;
	char volume_uuid[ZBL_UEFI_FAT_UUID_SIZE];
};

/*
 * A range of pages the loader obtained from the firmware: the kernel's,
 * the ramdisk's, the device tree copy's or the handoff's.
 */
struct page_run {
	EFI_PHYSICAL_ADDRESS base;
	UINTN pages;
};

/* The transition into the kernel, in riscv64-transition.S. */
extern void zbl_riscv64_enter(uint64_t boot_hart, uint64_t handoff, uint64_t entry) __attribute__((noreturn));

/*
 * The buffer the volume's boot sector is read into.
 *
 * It is aligned to its size so it meets any block device's I/O alignment up
 * to that size.
 */
static uint8_t boot_sector[BOOT_SECTOR_BUFFER] __attribute__((aligned(BOOT_SECTOR_BUFFER)));

/* The buffer zedbsd.cfg is read into, one byte larger than the parser takes so a longer file is seen. */
static uint8_t config_buffer[CONFIG_READ_BUFFER];

/* The buffer GetInfo fills with a file's information. */
static uint8_t file_info_buffer[FILE_INFO_BUFFER] __attribute__((aligned(8)));

/* The buffer the kernel's ELF and program headers are read into. */
static uint8_t kernel_header[ZBL_ELF_HEADER_BYTES] __attribute__((aligned(8)));

/* The configuration parsed from zedbsd.cfg; too large for the firmware's stack. */
static struct zbl_uefi_kern_config configuration;

/* The paths of the configuration file and the ramdisk on the volume, in UTF-16. */
static const CHAR16 config_path[] = {
	'\\', 'Z', 'E', 'D', 'B', 'S', 'D', '.', 'C', 'F', 'G', 0
};
static const CHAR16 initrd_path[] = {
	'\\', 'R', 'O', 'O', 'T', '.', 'I', 'M', 'G', 0
};

static void byte_zero(void *pointer, UINTN size);
static void byte_copy(void *destination, const void *source, UINTN size);
static int guid_equal(const EFI_GUID *left, const EFI_GUID *right);
static void console_ascii(struct loader_context *context, const char *string);
static void console_hex64(struct loader_context *context, const char *label, uint64_t value);
static EFI_STATUS report_failure(struct loader_context *context, const char *operation, EFI_STATUS status);
static uint32_t read_be32(const uint8_t *bytes);
static EFI_STATUS read_exact(EFI_FILE_PROTOCOL *file, UINT64 position, void *buffer, UINTN size);
static EFI_STATUS file_size(EFI_FILE_PROTOCOL *file, UINT64 *size);
static int path_utf16(const char *source, CHAR16 output[PATH_CHAR16_STORAGE]);
static EFI_STATUS open_volume(struct loader_context *context);
static EFI_STATUS read_volume_serial(struct loader_context *context, EFI_HANDLE device);
static EFI_STATUS load_configuration(struct loader_context *context);
static EFI_STATUS find_boot_hart(struct loader_context *context, uint64_t *boot_hart);
static EFI_STATUS copy_device_tree(struct loader_context *context, struct page_run *run, uint64_t *size);
static EFI_STATUS allocate_kernel(struct loader_context *context, UINTN pages, EFI_PHYSICAL_ADDRESS *base);
static EFI_STATUS load_kernel(struct loader_context *context, struct zbl_elf64_plan *plan, struct page_run *run);
static EFI_STATUS load_initrd(struct loader_context *context, struct page_run *run, uint64_t *size);
static EFI_STATUS exit_boot_services(struct loader_context *context);

/*
 * Loads the configured kernel and the ramdisk and enters the kernel.
 *
 * Every failure before ExitBootServices is reported on the console and
 * handed back to the firmware, which goes on to its next boot option.
 */
EFI_STATUS EFIAPI
efi_main(
	EFI_HANDLE image,
	EFI_SYSTEM_TABLE *system)
{
	struct loader_context context;
	struct zbl_elf64_plan plan;
	struct page_run kernel_run;
	struct page_run initrd_run;
	struct page_run fdt_run;
	struct page_run handoff_run;
	struct zbl6_riscv64_handoff *handoff;
	uint64_t boot_hart;
	uint64_t fdt_size;
	uint64_t initrd_size;
	uint64_t entry;
	EFI_STATUS status;
	EFI_STATUS reported;

	/* Refuses a system table without boot services. */
	if (system == 0 || system->BootServices == 0)
		return EFI_INVALID_PARAMETER;

	/* Starts the loader's context with no volume open yet. */
	context.image = image;
	context.system = system;
	context.boot = system->BootServices;
	context.root = 0;
	context.volume_uuid[0] = 0;
	console_ascii(&context, "RV64 UEFI ENTRY\n");

	/* Asks the firmware which hart it booted on, which the kernel must start on. */
	status = find_boot_hart(&context, &boot_hart);
	if (EFI_ERROR(status)) {
		reported = report_failure(&context, "Boot hart", status);
		return reported;
	}

	/* Opens the volume the loader came from and reads its serial. */
	status = open_volume(&context);
	if (EFI_ERROR(status)) {
		reported = report_failure(&context, "Open boot volume", status);
		return reported;
	}

	/* Reads and parses zedbsd.cfg. */
	status = load_configuration(&context);
	if (EFI_ERROR(status)) {
		reported = report_failure(&context, "Load zedbsd.cfg", status);
		return reported;
	}

	/* Reads the kernel into its physical home. */
	status = load_kernel(&context, &plan, &kernel_run);
	if (EFI_ERROR(status)) {
		reported = report_failure(&context, "Load kernel", status);
		return reported;
	}

	/* Reads the ramdisk, when the volume has one. */
	status = load_initrd(&context, &initrd_run, &initrd_size);
	if (EFI_ERROR(status)) {
		reported = report_failure(&context, "Load root.img", status);
		return reported;
	}

	/* Copies the firmware's device tree where it outlives boot services. */
	status = copy_device_tree(&context, &fdt_run, &fdt_size);
	if (EFI_ERROR(status)) {
		reported = report_failure(&context, "Copy device tree", status);
		return reported;
	}

	/* Allocates the handoff's page. */
	handoff_run.pages = 1;
	status = context.boot->AllocatePages(AllocateAnyPages, EfiLoaderData, handoff_run.pages, &handoff_run.base);
	if (EFI_ERROR(status)) {
		reported = report_failure(&context, "Allocate handoff", status);
		return reported;
	}

	/* Fills the handoff: the hart, the tree, the ramdisk, the kernel and the parameters. */
	handoff = (struct zbl6_riscv64_handoff *)(uintptr_t)handoff_run.base;
	byte_zero(handoff, sizeof(*handoff));
	handoff->magic = ZBL6_HANDOFF_MAGIC;
	handoff->version = ZBL6_RISCV64_HANDOFF_VERSION;
	handoff->size = sizeof(*handoff);
	handoff->flags = 0;
	handoff->boot_hart = boot_hart;
	handoff->fdt_phys = fdt_run.base;
	handoff->fdt_size = fdt_size;
	handoff->initrd_phys = initrd_run.base;
	handoff->initrd_size = initrd_size;
	handoff->kernel_phys_start = plan.physical_start;
	handoff->kernel_phys_end = plan.physical_end;
	byte_copy(&handoff->parameters, &configuration.parameter_record, sizeof(handoff->parameters));

	/*
	 * Works out the physical entry: the image keeps the same distance from
	 * its physical start as the entry from the linked virtual base.
	 */
	entry = plan.entry - RISCV64_KERNEL_LINK_VIRT_BASE + plan.physical_start;

	/* Reports what the kernel will find. */
	console_hex64(&context, "RV64 HART ", boot_hart);
	console_hex64(&context, "RV64 KERNEL ", plan.physical_start);
	console_hex64(&context, "RV64 ENTRY ", entry);
	console_hex64(&context, "RV64 FDT ", fdt_run.base);
	console_hex64(&context, "RV64 INITRD ", initrd_run.base);
	console_hex64(&context, "RV64 INITRD SIZE ", initrd_size);
	console_hex64(&context, "RV64 HANDOFF ", handoff_run.base);

	/* Leaves the firmware; nothing can be reported after this. */
	status = exit_boot_services(&context);
	if (EFI_ERROR(status)) {
		reported = report_failure(&context, "ExitBootServices", status);
		return reported;
	}

	/* Enters the kernel at its physical entry; it does not return. */
	zbl_riscv64_enter(boot_hart, handoff_run.base, entry);
}

/*
 * Clears bytes.
 *
 * The compiler may turn a structure's clearing into a call of memset,
 * which nothing else provides in a freestanding loader.
 */
void *
memset(
	void *pointer,
	int value,
	size_t size)
{
	uint8_t *bytes;
	size_t index;

	/* Writes the value to every byte. */
	bytes = pointer;
	for (index = 0; index < size; index++)
		bytes[index] = (uint8_t)value;

	/* Succeeded: the start of the bytes, as the standard function reports. */
	return pointer;
}

/*
 * Copies bytes.
 *
 * The compiler may turn a structure's copy into a call of memcpy, which
 * nothing else provides in a freestanding loader.
 */
void *
memcpy(
	void *destination,
	const void *source,
	size_t size)
{
	uint8_t *to;
	const uint8_t *from;
	size_t index;

	/* Copies every byte in order. */
	to = destination;
	from = source;
	for (index = 0; index < size; index++)
		to[index] = from[index];

	/* Succeeded: the destination, as the standard function reports. */
	return destination;
}

/* Clears bytes without the C library. */
static void
byte_zero(
	void *pointer,
	UINTN size)
{
	/* Clears through the freestanding memset. */
	(void)memset(pointer, 0, size);
}

/* Copies bytes without the C library. */
static void
byte_copy(
	void *destination,
	const void *source,
	UINTN size)
{
	/* Copies through the freestanding memcpy. */
	(void)memcpy(destination, source, size);
}

/* Tells whether two GUIDs are the same. */
static int
guid_equal(
	const EFI_GUID *left,
	const EFI_GUID *right)
{
	unsigned index;

	/* Compares the three leading fields. */
	if (left->Data1 != right->Data1)
		return 0;
	if (left->Data2 != right->Data2)
		return 0;
	if (left->Data3 != right->Data3)
		return 0;

	/* Compares the eight trailing bytes. */
	for (index = 0; index < 8U; index++) {
		if (left->Data4[index] != right->Data4[index])
			return 0;
	}

	/* Succeeded: every field is the same. */
	return 1;
}

/* Writes an ASCII string to the firmware's text console. */
static void
console_ascii(
	struct loader_context *context,
	const char *string)
{
	CHAR16 buffer[128];
	UINTN used;

	/* A firmware without a text console shows nothing. */
	if (context->system->ConOut == 0)
		return;
	if (context->system->ConOut->OutputString == 0)
		return;

	/* Converts the string to UTF-16 with CRLF line ends, writing out each full buffer. */
	used = 0;
	while (*string != 0) {
		/* A line feed is preceded by a carriage return. */
		if (*string == '\n') {
			buffer[used] = '\r';
			used++;
		}

		/* Takes the character. */
		buffer[used] = (uint8_t)*string;
		used++;
		string++;

		/* Writes out a buffer that has no room for another pair and the terminator. */
		if (used >= 125U) {
			buffer[used] = 0;
			(void)context->system->ConOut->OutputString(context->system->ConOut, buffer);
			used = 0;
		}
	}

	/* Writes out what is left. */
	if (used != 0) {
		buffer[used] = 0;
		(void)context->system->ConOut->OutputString(context->system->ConOut, buffer);
	}
}

/* Writes a label and a 64-bit value in hexadecimal, then a line end. */
static void
console_hex64(
	struct loader_context *context,
	const char *label,
	uint64_t value)
{
	static const char digits[] = "0123456789abcdef";
	char text[20];
	unsigned index;

	/* Spells the value with sixteen digits after 0x. */
	text[0] = '0';
	text[1] = 'x';
	for (index = 0; index < 16U; index++)
		text[2U + index] = digits[(value >> (60U - index * 4U)) & 0xfU];
	text[18] = '\n';
	text[19] = 0;

	/* Writes the label and the value. */
	console_ascii(context, label);
	console_ascii(context, text);
}

/* Reports a failed step on the console and passes its status on. */
static EFI_STATUS
report_failure(
	struct loader_context *context,
	const char *operation,
	EFI_STATUS status)
{
	/* Names the step and the firmware's status. */
	console_ascii(context, "RV64 UEFI FAILED: ");
	console_ascii(context, operation);
	console_ascii(context, "\n");
	console_hex64(context, "RV64 STATUS ", status);

	/* Hands the failure back to the caller, which returns it to the firmware. */
	return status;
}

/* Reads a big-endian 32-bit number. */
static uint32_t
read_be32(
	const uint8_t *bytes)
{
	uint32_t number;

	/* Assembles the number most significant byte first. */
	number = (uint32_t)bytes[0] << 24;
	number |= (uint32_t)bytes[1] << 16;
	number |= (uint32_t)bytes[2] << 8;
	number |= (uint32_t)bytes[3];

	/* Succeeded: the number. */
	return number;
}

/* Reads exactly size bytes of a file from a position. */
static EFI_STATUS
read_exact(
	EFI_FILE_PROTOCOL *file,
	UINT64 position,
	void *buffer,
	UINTN size)
{
	EFI_STATUS status;
	uint8_t *cursor;
	UINTN amount;

	/* Moves to the position. */
	cursor = buffer;
	status = file->SetPosition(file, position);
	if (EFI_ERROR(status))
		return status;

	/* Reads until every byte is in, refusing a short or long read. */
	while (size != 0) {
		/* Reads as much as remains. */
		amount = size;
		status = file->Read(file, &amount, cursor);
		if (EFI_ERROR(status))
			return status;

		/* Refuses the end of the file before the bytes, and a firmware that read more than asked. */
		if (amount == 0 || amount > size)
			return EFI_LOAD_ERROR;

		/* Moves past what was read. */
		cursor += amount;
		size -= amount;
	}

	/* Succeeded: the buffer holds the bytes. */
	return EFI_SUCCESS;
}

/* Reports a regular file's size, refusing a directory. */
static EFI_STATUS
file_size(
	EFI_FILE_PROTOCOL *file,
	UINT64 *size)
{
	EFI_FILE_INFO *info;
	EFI_STATUS status;
	UINTN info_size;

	/* Asks the firmware for the file's information. */
	info_size = sizeof(file_info_buffer);
	status = file->GetInfo(file, &EFI_FILE_INFO_ID, &info_size, file_info_buffer);
	if (EFI_ERROR(status))
		return status;

	/* Refuses a directory. */
	info = (EFI_FILE_INFO *)file_info_buffer;
	if ((info->Attribute & EFI_FILE_DIRECTORY) != 0)
		return EFI_LOAD_ERROR;

	/* Succeeded: the file's size. */
	*size = info->FileSize;
	return EFI_SUCCESS;
}

/*
 * Turns a relative path of zedbsd.cfg into a UTF-16 volume path: a leading
 * backslash and backslashes between the components.
 *
 * Returns 1, or 0 for a path that does not fit.
 */
static int
path_utf16(
	const char *source,
	CHAR16 output[PATH_CHAR16_STORAGE])
{
	UINTN index;

	/* Starts at the volume's root. */
	output[0] = '\\';

	/* Copies the path with its separators turned around. */
	for (index = 0; source[index] != 0; index++) {
		/* Refuses a path with no room for its terminator. */
		if (index + 2U >= PATH_CHAR16_STORAGE)
			return 0;

		/* A slash becomes the firmware's separator. */
		if (source[index] == '/') {
			output[index + 1U] = '\\';
		} else {
			output[index + 1U] = (uint8_t)source[index];
		}
	}

	/* Succeeded: the path is terminated. */
	output[index + 1U] = 0;
	return 1;
}

/*
 * Opens the root of the volume the loader was loaded from and reads the
 * volume's serial, which zedbsd.cfg's boot volume is named by.
 */
static EFI_STATUS
open_volume(
	struct loader_context *context)
{
	EFI_LOADED_IMAGE_PROTOCOL *loaded;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *file_system;
	EFI_STATUS status;

	/* Finds the loader's own image and the device it was loaded from. */
	status = context->boot->HandleProtocol(context->image, &EFI_LOADED_IMAGE_PROTOCOL_GUID, (void **)&loaded);
	if (EFI_ERROR(status))
		return status;

	/* Refuses an image without a device. */
	if (loaded == 0 || loaded->DeviceHandle == 0)
		return EFI_INVALID_PARAMETER;

	/* Finds the device's file system. */
	status = context->boot->HandleProtocol(loaded->DeviceHandle, &EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, (void **)&file_system);
	if (EFI_ERROR(status))
		return status;

	/* Opens its root directory. */
	status = file_system->OpenVolume(file_system, &context->root);
	if (EFI_ERROR(status))
		return status;

	/* Reads the volume's serial from its boot sector. */
	status = read_volume_serial(context, loaded->DeviceHandle);
	if (EFI_ERROR(status))
		return status;

	/* Succeeded: the volume is open. */
	console_ascii(context, "RV64 VOLUME ");
	console_ascii(context, context->volume_uuid);
	console_ascii(context, "\n");
	return EFI_SUCCESS;
}

/* Reads the FAT volume's serial from its boot sector on the loader's device. */
static EFI_STATUS
read_volume_serial(
	struct loader_context *context,
	EFI_HANDLE device)
{
	EFI_BLOCK_IO_PROTOCOL *block;
	EFI_BLOCK_IO_MEDIA *media;
	struct zbl_uefi_fat_info fat;
	enum zbl_uefi_fat_result fat_result;
	EFI_STATUS status;

	/* Finds the device's block access. */
	status = context->boot->HandleProtocol(device, &EFI_BLOCK_IO_PROTOCOL_GUID, (void **)&block);
	if (EFI_ERROR(status))
		return status;

	/* Refuses a medium the boot sector buffer cannot take. */
	media = block->Media;
	if (media == 0 || !media->MediaPresent)
		return EFI_NO_MEDIA;
	if (media->BlockSize == 0 || media->BlockSize > BOOT_SECTOR_BUFFER)
		return EFI_UNSUPPORTED;

	/* Reads the first block, the FAT boot sector. */
	status = block->ReadBlocks(block, media->MediaId, 0, media->BlockSize, boot_sector);
	if (EFI_ERROR(status))
		return status;

	/* Checks the boot sector and takes the serial out of it. */
	fat_result = zbl_uefi_fat_bpb_parse(boot_sector, media->BlockSize, media->BlockSize, media->LastBlock + 1U, &fat);
	if (fat_result != ZBL_UEFI_FAT_OK) {
		console_ascii(context, "RV64 FAT ");
		console_ascii(context, zbl_uefi_fat_result_name(fat_result));
		console_ascii(context, "\n");
		return EFI_LOAD_ERROR;
	}

	/* Succeeded: the serial in the form zedbsd.cfg names a volume by. */
	zbl_uefi_fat_uuid(fat.volume_serial, context->volume_uuid);
	return EFI_SUCCESS;
}

/* Reads and parses zedbsd.cfg from the volume's root. */
static EFI_STATUS
load_configuration(
	struct loader_context *context)
{
	EFI_FILE_PROTOCOL *file;
	EFI_STATUS status;
	UINT64 size;
	enum zbl_uefi_kern_config_result result;

	/* Opens the file. */
	status = context->root->Open(context->root, &file, config_path, EFI_FILE_MODE_READ, 0);
	if (EFI_ERROR(status))
		return status;

	/* Refuses a file larger than the parser takes. */
	status = file_size(file, &size);
	if (EFI_ERROR(status)) {
		(void)file->Close(file);
		return status;
	}
	if (size == 0 || size >= sizeof(config_buffer)) {
		(void)file->Close(file);
		return EFI_BAD_BUFFER_SIZE;
	}

	/* Reads the file and closes it. */
	status = read_exact(file, 0, config_buffer, (UINTN)size);
	(void)file->Close(file);
	if (EFI_ERROR(status))
		return status;

	/* Parses the file against the volume's serial. */
	result = zbl_uefi_kern_config_parse(&configuration,
					    config_buffer,
					    (size_t)size,
					    context->volume_uuid,
					    sizeof(context->volume_uuid));
	if (result != ZBL_UEFI_KERN_CONFIG_OK) {
		console_ascii(context, "zedbsd.cfg rejected: ");
		console_ascii(context, zbl_uefi_kern_config_result_name(result));
		console_ascii(context, "\n");
		return EFI_LOAD_ERROR;
	}

	/* Succeeded: reports the kernel's path and the parameters. */
	console_ascii(context, "RV64 KERNEL ");
	console_ascii(context, configuration.kernel_path);
	console_ascii(context, "\n");
	console_ascii(context, "RV64 PARAMS ");
	console_ascii(context, configuration.parameter_record.text);
	console_ascii(context, "\n");
	return EFI_SUCCESS;
}

/*
 * Asks the firmware for the boot hart's id.
 *
 * Only the boot hart runs after the firmware: the others wait in the
 * machine-mode firmware until the kernel starts them, so a wrong id would
 * leave the kernel unable to start them.
 */
static EFI_STATUS
find_boot_hart(
	struct loader_context *context,
	uint64_t *boot_hart)
{
	RISCV_EFI_BOOT_PROTOCOL *protocol;
	EFI_STATUS status;
	UINTN hart;

	/* Finds the firmware's boot protocol. */
	status = context->boot->LocateProtocol(&RISCV_EFI_BOOT_PROTOCOL_GUID, 0, (void **)&protocol);
	if (EFI_ERROR(status))
		return status;

	/* Refuses a protocol without the call. */
	if (protocol == 0 || protocol->GetBootHartId == 0)
		return EFI_UNSUPPORTED;

	/* Asks for the id. */
	status = protocol->GetBootHartId(protocol, &hart);
	if (EFI_ERROR(status))
		return status;

	/* Succeeded: the boot hart. */
	*boot_hart = hart;
	return EFI_SUCCESS;
}

/*
 * Copies the firmware's device tree into pages that stay the kernel's.
 *
 * The firmware's own copy may lie in memory that boot services release.
 */
static EFI_STATUS
copy_device_tree(
	struct loader_context *context,
	struct page_run *run,
	uint64_t *size)
{
	const uint8_t *tree;
	uint32_t magic;
	uint32_t total;
	UINTN index;
	int matches;
	EFI_STATUS status;

	/* Finds the tree among the firmware's configuration tables. */
	tree = 0;
	for (index = 0; index < context->system->NumberOfTableEntries; index++) {
		/* Takes the table whose identifier is the device tree's. */
		matches = guid_equal(&context->system->ConfigurationTable[index].VendorGuid, &EFI_DEVICE_TREE_GUID);
		if (matches) {
			tree = context->system->ConfigurationTable[index].VendorTable;
			break;
		}
	}

	/* Refuses a firmware that gave no tree. */
	if (tree == 0)
		return EFI_NOT_FOUND;

	/* Refuses a table that is not a flattened tree, or one too large. */
	magic = read_be32(tree);
	if (magic != FDT_MAGIC)
		return EFI_LOAD_ERROR;
	total = read_be32(tree + FDT_TOTALSIZE_OFFSET);
	if (total < 40U || total > MAX_FDT_BYTES)
		return EFI_BAD_BUFFER_SIZE;

	/* Allocates the copy's pages. */
	run->pages = (UINTN)((total + PAGE_SIZE - 1U) / PAGE_SIZE);
	status = context->boot->AllocatePages(AllocateAnyPages, EfiLoaderData, run->pages, &run->base);
	if (EFI_ERROR(status))
		return status;

	/* Succeeded: copies the tree. */
	byte_copy((void *)(uintptr_t)run->base, tree, total);
	*size = total;
	return EFI_SUCCESS;
}

/*
 * Allocates the kernel's pages: at the linked physical address when the
 * firmware has it free, otherwise anywhere, with a megapage more so that
 * the image can start on one.
 */
static EFI_STATUS
allocate_kernel(
	struct loader_context *context,
	UINTN pages,
	EFI_PHYSICAL_ADDRESS *base)
{
	EFI_PHYSICAL_ADDRESS address;
	EFI_STATUS status;

	/* Tries the linked address first. */
	address = RISCV64_KERNEL_LINK_PHYS_START;
	status = context->boot->AllocatePages(AllocateAddress, EfiLoaderData, pages, &address);
	if (!EFI_ERROR(status)) {
		*base = address;
		return EFI_SUCCESS;
	}

	/* Takes any pages with room to round the start up to a megapage. */
	status = context->boot->AllocatePages(AllocateAnyPages, EfiLoaderData,
					      pages + (UINTN)(RISCV64_KERNEL_PHYS_ALIGN / PAGE_SIZE),
					      &address);
	if (EFI_ERROR(status))
		return status;

	/* Succeeded: the first megapage boundary of the run. */
	*base = (address + RISCV64_KERNEL_PHYS_ALIGN - 1U) & ~(RISCV64_KERNEL_PHYS_ALIGN - 1U);
	return EFI_SUCCESS;
}

/* Reads the configured kernel, places it and copies its segments. */
static EFI_STATUS
load_kernel(
	struct loader_context *context,
	struct zbl_elf64_plan *plan,
	struct page_run *run)
{
	static CHAR16 path[PATH_CHAR16_STORAGE];
	EFI_FILE_PROTOCOL *file;
	EFI_PHYSICAL_ADDRESS base;
	EFI_STATUS status;
	UINT64 size;
	uint64_t bytes;
	uint32_t index;
	uint8_t *destination;
	int converted;
	int accepted;

	/* Turns the configured path into the firmware's form. */
	converted = path_utf16(configuration.kernel_path, path);
	if (!converted)
		return EFI_BAD_BUFFER_SIZE;

	/* Opens the kernel. */
	status = context->root->Open(context->root, &file, path, EFI_FILE_MODE_READ, 0);
	if (EFI_ERROR(status))
		return status;

	/* Refuses a kernel file too small for its headers or too large to be one. */
	status = file_size(file, &size);
	if (EFI_ERROR(status)) {
		(void)file->Close(file);
		return status;
	}
	if (size < ZBL_ELF_HEADER_BYTES || size > MAX_KERNEL_FILE) {
		(void)file->Close(file);
		return EFI_BAD_BUFFER_SIZE;
	}

	/* Reads the ELF header and the program headers. */
	status = read_exact(file, 0, kernel_header, sizeof(kernel_header));
	if (EFI_ERROR(status)) {
		(void)file->Close(file);
		return status;
	}

	/* Refuses a kernel the loader cannot place. */
	accepted = zbl_elf64_plan(kernel_header, size, plan);
	if (!accepted) {
		(void)file->Close(file);
		return EFI_LOAD_ERROR;
	}

	/* Refuses an image larger than the HAL's window. */
	bytes = plan->link_physical_end - plan->link_physical_start;
	if (bytes > RISCV64_KERNEL_MAX_BYTES) {
		(void)file->Close(file);
		return EFI_BAD_BUFFER_SIZE;
	}

	/* Allocates the image's pages. */
	run->pages = (UINTN)((bytes + PAGE_SIZE - 1U) / PAGE_SIZE);
	status = allocate_kernel(context, run->pages, &base);
	if (EFI_ERROR(status)) {
		(void)file->Close(file);
		return status;
	}
	run->base = base;

	/* Rebases the plan onto the pages. */
	accepted = zbl_elf64_place(plan, base);
	if (!accepted) {
		(void)file->Close(file);
		return EFI_LOAD_ERROR;
	}

	/* Copies each segment and clears what the file does not hold. */
	for (index = 0; index < plan->segment_count; index++) {
		/* Clears the segment's memory, which covers its zero-filled tail. */
		destination = (uint8_t *)(uintptr_t)plan->segment[index].physical;
		byte_zero(destination, (UINTN)plan->segment[index].memory_size);

		/* Reads the segment's file part. */
		status = read_exact(file,
				    plan->segment[index].offset,
				    destination,
				    (UINTN)plan->segment[index].file_size);
		if (EFI_ERROR(status)) {
			(void)file->Close(file);
			return status;
		}
	}

	/* Succeeded: the image is in place. */
	(void)file->Close(file);
	return EFI_SUCCESS;
}

/*
 * Reads root.img into pages that stay the kernel's.  A volume without it
 * boots without a ramdisk.
 */
static EFI_STATUS
load_initrd(
	struct loader_context *context,
	struct page_run *run,
	uint64_t *size)
{
	EFI_FILE_PROTOCOL *file;
	EFI_STATUS status;
	UINT64 bytes;

	/* Starts with no ramdisk. */
	run->base = 0;
	run->pages = 0;
	*size = 0;

	/* Opens the ramdisk; a volume without one has none. */
	status = context->root->Open(context->root, &file, initrd_path, EFI_FILE_MODE_READ, 0);
	if (status == EFI_NOT_FOUND)
		return EFI_SUCCESS;
	if (EFI_ERROR(status))
		return status;

	/* Refuses an empty or oversized ramdisk. */
	status = file_size(file, &bytes);
	if (EFI_ERROR(status)) {
		(void)file->Close(file);
		return status;
	}
	if (bytes == 0 || bytes > MAX_INITRD_FILE) {
		(void)file->Close(file);
		return EFI_BAD_BUFFER_SIZE;
	}

	/* Allocates its pages. */
	run->pages = (UINTN)((bytes + PAGE_SIZE - 1U) / PAGE_SIZE);
	status = context->boot->AllocatePages(AllocateAnyPages, EfiLoaderData, run->pages, &run->base);
	if (EFI_ERROR(status)) {
		(void)file->Close(file);
		return status;
	}

	/* Reads the file into them. */
	status = read_exact(file, 0, (void *)(uintptr_t)run->base, (UINTN)bytes);
	(void)file->Close(file);
	if (EFI_ERROR(status))
		return status;

	/* Succeeded: the ramdisk is in memory. */
	*size = bytes;
	return EFI_SUCCESS;
}

/*
 * Takes the memory map and exits boot services, retrying when the map
 * changed in between.
 */
static EFI_STATUS
exit_boot_services(
	struct loader_context *context)
{
	EFI_MEMORY_DESCRIPTOR *map;
	EFI_STATUS status;
	UINTN map_size;
	UINTN capacity;
	UINTN map_key;
	UINTN descriptor_size;
	UINT32 descriptor_version;
	unsigned attempt;

	/* Closes the volume, which nothing reads any more. */
	if (context->root != 0) {
		(void)context->root->Close(context->root);
		context->root = 0;
	}

	/* Asks for the map's size. */
	map_size = 0;
	status = context->boot->GetMemoryMap(&map_size, 0, &map_key, &descriptor_size, &descriptor_version);
	if (status != EFI_BUFFER_TOO_SMALL)
		return EFI_LOAD_ERROR;
	if (descriptor_size == 0)
		return EFI_LOAD_ERROR;

	/* Allocates the map with room for the descriptors the allocation itself adds. */
	capacity = map_size + descriptor_size * 16U;
	status = context->boot->AllocatePool(EfiLoaderData, capacity, (void **)&map);
	if (EFI_ERROR(status))
		return status;

	/* Exits with the key of a fresh map, retrying a stale one. */
	console_ascii(context, "RV64 UEFI EXIT\n");
	for (attempt = 0; attempt < EXIT_ATTEMPTS; attempt++) {
		/* Takes the map whose key ExitBootServices consumes. */
		map_size = capacity;
		status = context->boot->GetMemoryMap(&map_size, map, &map_key, &descriptor_size, &descriptor_version);
		if (EFI_ERROR(status))
			return status;

		/* Exits; only a stale key is retried. */
		status = context->boot->ExitBootServices(context->image, map_key);
		if (status == EFI_SUCCESS)
			return EFI_SUCCESS;
		if (status != EFI_INVALID_PARAMETER)
			return status;
	}

	/* Every retry found the map changed. */
	return status;
}
