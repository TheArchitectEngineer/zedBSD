#include <hal/hal.h>
#include "../bsp.h"
#include "../defs.h"
#include <kern/boot.h>

/* How many values of the boot framebuffer the kernel reads by name. */
#define RPI4_FRAMEBUFFER_VALUE_COUNT 6U

static struct rpi4_fdt_info boot_info;
static uintptr_t boot_fdt;

/*
 * The handoff the kernel's entry reads: the common record alone, a single
 * SD boot device on the first partition of an MBR disk.  Filled once with
 * the boot record; what is particular to the board goes by name.
 */
static struct kern_boot_handoff kernel_handoff;

/*
 * The firmware's command line (/chosen/bootargs, from cmdline.txt), copied
 * out of the device tree while the tree is known to be intact, and handed
 * to the kernel as "boot.command-line".  Filled once at boot; the first
 * byte is zero when the firmware passed no line.
 */
static char boot_command_line[KERN_BOOT_PARAMETERS_STORAGE_SIZE];

/*
 * The named objects of hal_get_arch_handoff(): the device tree's physical
 * address ("rpi4.fdt") and the SD controller's ("rpi4.sdhci"), each a
 * 64-bit word.  Filled once at boot and not changed afterwards; zero means
 * the firmware named none, and the name then reports no object.
 */
static uint64_t handoff_fdt_phys;
static uint64_t handoff_sdhci_phys;

/*
 * The names under which the kernel reads the boot framebuffer, one per
 * value, in the order of framebuffer_values[].
 */
static const char *const framebuffer_value_names[RPI4_FRAMEBUFFER_VALUE_COUNT] = {
	"rpi4.framebuffer.phys",
	"rpi4.framebuffer.size",
	"rpi4.framebuffer.width",
	"rpi4.framebuffer.height",
	"rpi4.framebuffer.pitch",
	"rpi4.framebuffer.format",
};

/*
 * The firmware's framebuffer as the kernel reads it, each value a 64-bit
 * word: the physical base, the size in bytes, the width and the height in
 * pixels, the pitch in bytes and the firmware's pixel order (0 blue first,
 * 1 red first).  Filled once when the firmware allocates the framebuffer
 * and not changed afterwards; the names report no object while the size
 * is zero.
 */
static uint64_t framebuffer_values[RPI4_FRAMEBUFFER_VALUE_COUNT];

static void copy_command_line(const struct rpi4_fdt_info *info, uintptr_t fdt_phys);
static int handoff_name_is(const char *name, const char *wanted);

void
rpi4_boot_set_info(const struct rpi4_fdt_info *info, uintptr_t fdt_phys)
{
	boot_info = *info;
	boot_fdt = fdt_phys;
	hal_memset(&kernel_handoff,0,sizeof(kernel_handoff));
	kernel_handoff.magic=KERN_HANDOFF_MAGIC;
	kernel_handoff.version=KERN_HANDOFF_VERSION_MULTIBOOT;
	kernel_handoff.size=sizeof(kernel_handoff);
	kernel_handoff.device_count=1;
	kernel_handoff.boot_bios_id=0x80;
	kernel_handoff.boot_partition_scheme=KERN_PARTITION_SCHEME_MBR;
	kernel_handoff.boot_partition_index=1;
	kernel_handoff.boot_partition_lba=KERN_BOOT_PARTITION_LBA_UNKNOWN;
	hal_memset(framebuffer_values,0,sizeof(framebuffer_values));
	copy_command_line(info, fdt_phys);

	/* Keeps the tree's and the SD controller's addresses for their names. */
	handoff_fdt_phys = fdt_phys;
	handoff_sdhci_phys = info->sdhci_base;
}

void
rpi4_boot_set_framebuffer(uint64_t phys,uint64_t size,uint32_t width,uint32_t height,
	uint32_t pitch,uint32_t format)
{
	framebuffer_values[0]=phys;
	framebuffer_values[1]=size;
	framebuffer_values[2]=width;
	framebuffer_values[3]=height;
	framebuffer_values[4]=pitch;
	framebuffer_values[5]=format;
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
 *
 * "boot.command-line" is the firmware's command line; "rpi4.fdt" and
 * "rpi4.sdhci" are the physical addresses of the device tree and of the
 * SD controller it names, each a 64-bit word; "rpi4.framebuffer.phys",
 * ".size", ".width", ".height", ".pitch" and ".format" are the firmware's
 * framebuffer, each a 64-bit word, all present or all absent.
 */
void *
hal_get_arch_handoff(
	const char *name)
{
	unsigned index;
	int match;

	/* Exposes the firmware's command line when it passed one. */
	if (boot_command_line[0] != '\0') {
		match = handoff_name_is(name, "boot.command-line");
		if (match)
			return boot_command_line;
	}

	/* Exposes the device tree when the firmware passed one. */
	if (handoff_fdt_phys != 0) {
		match = handoff_name_is(name, "rpi4.fdt");
		if (match)
			return &handoff_fdt_phys;
	}

	/* Exposes the SD controller when the device tree names one. */
	if (handoff_sdhci_phys != 0) {
		match = handoff_name_is(name, "rpi4.sdhci");
		if (match)
			return &handoff_sdhci_phys;
	}

	/* Exposes the framebuffer's values when the firmware allocated one. */
	if (framebuffer_values[1] != 0) {
		/* Returns one of the framebuffer's values under its own name. */
		for (index = 0; index < RPI4_FRAMEBUFFER_VALUE_COUNT; index++) {
			match = handoff_name_is(name, framebuffer_value_names[index]);
			if (match)
				return &framebuffer_values[index];
		}
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
