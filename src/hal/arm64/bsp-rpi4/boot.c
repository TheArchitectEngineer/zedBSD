#include <hal/hal.h>
#include "../bsp.h"
#include "../defs.h"
#include <kern/boot.h>

static struct rpi4_fdt_info boot_info;
static uintptr_t boot_fdt;
static struct rpi4_boot_handoff kernel_handoff;

/*
 * The firmware's command line (/chosen/bootargs, from cmdline.txt), copied
 * out of the device tree while the tree is known to be intact, and handed
 * to the kernel as "boot.command-line".  Filled once at boot; the first
 * byte is zero when the firmware passed no line.
 */
static char boot_command_line[KERN_BOOT_PARAMETERS_STORAGE_SIZE];

static void copy_command_line(const struct rpi4_fdt_info *info, uintptr_t fdt_phys);
static int handoff_name_is(const char *name, const char *wanted);

void
rpi4_boot_set_info(const struct rpi4_fdt_info *info, uintptr_t fdt_phys)
{
	boot_info = *info;
	boot_fdt = fdt_phys;
	hal_memset(&kernel_handoff,0,sizeof(kernel_handoff));
	kernel_handoff.common.magic=KERN_HANDOFF_MAGIC;
	kernel_handoff.common.version=KERN_HANDOFF_VERSION_MULTIBOOT;
	kernel_handoff.common.size=sizeof(kernel_handoff);
	kernel_handoff.common.device_count=1;
	kernel_handoff.common.boot_bios_id=0x80;
	kernel_handoff.common.boot_partition_scheme=KERN_PARTITION_SCHEME_MBR;
	kernel_handoff.common.boot_partition_index=1;
	kernel_handoff.common.boot_partition_lba=KERN_BOOT_PARTITION_LBA_UNKNOWN;
	kernel_handoff.extension_magic=KERN_RPI4_HANDOFF_MAGIC;
	kernel_handoff.extension_version=1;
	kernel_handoff.extension_size=sizeof(kernel_handoff)-sizeof(kernel_handoff.common);
	kernel_handoff.fdt_phys=fdt_phys;
	kernel_handoff.sdhci_phys=info->sdhci_base;
	kernel_handoff.sdhci_irq=info->sdhci_irq;
	copy_command_line(info, fdt_phys);
}

void
rpi4_boot_set_framebuffer(uint64_t phys,uint64_t size,uint32_t width,uint32_t height,
	uint32_t pitch,uint32_t format)
{
	kernel_handoff.framebuffer_phys=phys;
	kernel_handoff.framebuffer_size=size;
	kernel_handoff.framebuffer_width=width;
	kernel_handoff.framebuffer_height=height;
	kernel_handoff.framebuffer_pitch=pitch;
	kernel_handoff.framebuffer_format=format;
	if(size&&boot_info.reserved_count<RPI4_FDT_MAX_RESERVED){
		boot_info.reserved[boot_info.reserved_count].base=phys;
		boot_info.reserved[boot_info.reserved_count].size=size;
		boot_info.reserved_count++;
	}
}

const struct rpi4_fdt_info *rpi4_boot_info(void) { return &boot_info; }
uintptr_t rpi4_boot_fdt_phys(void) { return boot_fdt; }
const void *rpi4_kernel_handoff(void){return &kernel_handoff;}

/*
 * Returns one named architecture handoff object.
 */
void *
hal_get_arch_handoff(
	const char *name)
{
	int match;

	/* Exposes the firmware's command line when it passed one. */
	if (boot_command_line[0] != '\0') {
		match = handoff_name_is(name, "boot.command-line");
		if (match)
			return boot_command_line;
	}

	/* Reports an unavailable or unknown architecture handoff. */
	return NULL;
}

/*
 * Copies the firmware's command line out of the device tree.
 *
 * A line longer than the kernel keeps is cut at the last separator that
 * fits, so no parameter is handed over in part.
 */
static void
copy_command_line(
	const struct rpi4_fdt_info *info,
	uintptr_t fdt_phys)
{
	const char *line;
	size_t length;
	size_t limit;

	/* Starts with no line, which is what an absent one leaves. */
	boot_command_line[0] = '\0';

	/* Leaves the line empty when the firmware passed none. */
	if (info->bootargs_offset == 0U || info->bootargs_length == 0U)
		return;

	/* Measures the line up to its terminator, within the property. */
	line = (const char *)(ARM64_DIRECT_BASE + fdt_phys +
	    info->bootargs_offset);
	length = 0;
	while (length < info->bootargs_length && line[length] != '\0')
		length++;

	/* Cuts a line the kernel cannot keep at its last separator. */
	limit = sizeof(boot_command_line) - 1U;
	if (length > limit) {
		length = limit;
		while (length > 0U && line[length] != ' ')
			length--;
	}

	/* Copies the line and terminates it. */
	hal_memcpy(boot_command_line, line, length);
	boot_command_line[length] = '\0';
}

/*
 * Tells whether a handoff name is the one wanted.
 */
static int
handoff_name_is(
	const char *name,
	const char *wanted)
{
	size_t index;

	/* Refuses a missing name. */
	if (name == NULL)
		return 0;

	/* Compares the names character by character, terminator included. */
	for (index = 0; wanted[index] != '\0'; index++) {
		if (name[index] != wanted[index])
			return 0;
	}

	/* Reports whether the name ends where the wanted one does. */
	if (name[index] != '\0')
		return 0;
	return 1;
}
