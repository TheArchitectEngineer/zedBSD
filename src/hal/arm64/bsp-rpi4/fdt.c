#include <hal/hal.h>
#include "fdt.h"

#define FDT_MAGIC 0xd00dfeedU
#define FDT_BEGIN_NODE 1U
#define FDT_END_NODE 2U
#define FDT_PROP 3U
#define FDT_NOP 4U
#define FDT_END 9U
#define FDT_MAX_DEPTH 24
#define FDT_MAX_CELLS 32

enum { FDT_OK, FDT_E_HEADER, FDT_E_BOUNDS, FDT_E_STRUCTURE,
	FDT_E_DEPTH, FDT_E_CELLS, FDT_E_OVERFLOW, FDT_E_NOT_RPI4 };

struct fdt_node {
	uint8_t parent_addr_cells;
	uint8_t parent_size_cells;
	uint8_t addr_cells;
	uint8_t size_cells;
	uint8_t is_memory;
	uint8_t is_rpi4;
	uint8_t is_uart;
	uint8_t is_mailbox;
	uint8_t is_gic;
	uint8_t is_sdhci;
	uint8_t is_chosen;
	uint8_t is_cpu;
	uint8_t spin_table;
	uint64_t release_addr;
	uint32_t reg[FDT_MAX_CELLS];
	uint32_t reg_count;
	uint32_t ranges[FDT_MAX_CELLS];
	uint32_t ranges_count;
	uint32_t interrupts[12];
	uint32_t interrupt_count;
};

/*
 * Where the parse of the structure block stands: the blob, the strings,
 * the nodes open from the root down, and what has been found so far.
 */
struct fdt_parse {
	const uint8_t *data;
	const uint8_t *strings;
	uint32_t size_strings;
	const uint8_t *cursor;
	const uint8_t *end;
	int depth;
	struct fdt_node stack[FDT_MAX_DEPTH];
	struct rpi4_fdt_info *info;
};

static uint32_t
read_be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	    ((uint32_t)p[2] << 8) | p[3];
}

static uint64_t
read_be64(const uint8_t *p)
{
	return ((uint64_t)read_be32(p) << 32) | read_be32(p + 4);
}

static int
bounded_string(const uint8_t *p, const uint8_t *end, size_t *length)
{
	const uint8_t *q = p;
	while (q < end && *q != 0) q++;
	if (q == end) return FDT_E_BOUNDS;
	*length = (size_t)(q - p);
	return FDT_OK;
}

static int
string_equal(const uint8_t *s, size_t n, const char *literal)
{
	size_t i = 0;
	while (literal[i] != '\0') i++;
	if (i != n) return 0;
	for (i = 0; i < n; i++) if (s[i] != (uint8_t)literal[i]) return 0;
	return 1;
}

static int
compatible_has(const uint8_t *p, size_t length, const char *wanted)
{
	const uint8_t *end = p + length;
	while (p < end) {
		size_t n;
		if (bounded_string(p, end, &n) != FDT_OK) return 0;
		if (string_equal(p, n, wanted)) return 1;
		p += n + 1;
	}
	return 0;
}

static int
cells_to_u64(const uint32_t *cells, unsigned count, uint64_t *value)
{
	uint64_t v = 0;
	unsigned i;
	if (count > 2) return FDT_E_CELLS;
	for (i = 0; i < count; i++) v = (v << 32) | cells[i];
	*value = v;
	return FDT_OK;
}

static int
translate(const struct fdt_node *parent, uint64_t address, uint64_t *translated)
{
	unsigned stride, offset;
	if (parent->ranges_count == 0) {
		*translated = address;
		return FDT_OK;
	}
	stride = parent->addr_cells + parent->parent_addr_cells +
	    parent->size_cells;
	if (stride == 0) return FDT_E_CELLS;
	for (offset = 0; offset + stride <= parent->ranges_count; offset += stride) {
		uint64_t child, physical, size;
		int error = cells_to_u64(parent->ranges + offset,
		    parent->addr_cells, &child);
		if (error != FDT_OK) return error;
		error = cells_to_u64(parent->ranges + offset + parent->addr_cells,
		    parent->parent_addr_cells, &physical);
		if (error != FDT_OK) return error;
		error = cells_to_u64(parent->ranges + offset + parent->addr_cells +
		    parent->parent_addr_cells, parent->size_cells, &size);
		if (error != FDT_OK) return error;
		if (address < child || address - child >= size) continue;
		if (physical > ~(uint64_t)0 - (address - child)) return FDT_E_OVERFLOW;
		*translated = physical + address - child;
		return FDT_OK;
	}
	return FDT_E_BOUNDS;
}

static int
add_range(struct rpi4_fdt_range *ranges, unsigned *count, unsigned maximum,
	uint64_t base, uint64_t size)
{
	if (size == 0) return FDT_OK;
	if (base > ~(uint64_t)0 - size) return FDT_E_OVERFLOW;
	if (*count >= maximum) return FDT_E_BOUNDS;
	ranges[*count].base = base;
	ranges[*count].size = size;
	(*count)++;
	return FDT_OK;
}

static uint32_t
decode_irq(const uint32_t *cells, unsigned count)
{
	if (count < 2) return 0;
	if (cells[0] == 0) return cells[1] + 32;
	if (cells[0] == 1) return cells[1] + 16;
	return cells[1];
}


static int commit_node(struct fdt_node *node, const struct fdt_node *parent, struct rpi4_fdt_info *info);
static int commit_memory(const struct fdt_node *node, struct rpi4_fdt_info *info);
static int commit_device(const struct fdt_node *node, const struct fdt_node *parent, struct rpi4_fdt_info *info);
static int commit_cpu(const struct fdt_node *node, struct rpi4_fdt_info *info);
static int read_reserve_map(const uint8_t *data, uint32_t total, uint32_t off_reserve, struct rpi4_fdt_info *info);
static int begin_node(struct fdt_parse *parse);
static int read_property(struct fdt_parse *parse);
static void record_property(struct fdt_node *node, const uint8_t *name, size_t name_length, const uint8_t *value, uint32_t length);
static int record_cells(struct fdt_node *node, const uint8_t *name, size_t name_length, const uint8_t *value, uint32_t length);
static int end_node(struct fdt_parse *parse);

/*
 * Parses the firmware's flattened device tree into what the HAL needs.
 *
 * Every offset and length in the blob is checked against the bytes the
 * caller says are readable before it is used.
 */
int
rpi4_fdt_parse(
	const void *blob,
	size_t available,
	struct rpi4_fdt_info *info)
{
	struct fdt_parse parse;
	const uint8_t *data;
	uint32_t total;
	uint32_t off_struct;
	uint32_t off_strings;
	uint32_t off_reserve;
	uint32_t size_struct;
	uint32_t size_strings;
	uint32_t token;
	int error;

	/* Requires a header and the magic number. */
	if (blob == NULL || info == NULL || available < 40)
		return FDT_E_HEADER;
	data = blob;
	if (read_be32(data) != FDT_MAGIC)
		return FDT_E_HEADER;

	/* Reads the header's sizes and offsets. */
	total = read_be32(data + 4);
	off_struct = read_be32(data + 8);
	off_strings = read_be32(data + 12);
	off_reserve = read_be32(data + 16);
	size_strings = read_be32(data + 32);
	size_struct = read_be32(data + 36);

	/* Refuses a blob larger than what is readable or than 2 MiB. */
	if (total < 40 || total > available || total > 2U * 1024U * 1024U)
		return FDT_E_BOUNDS;

	/* Refuses blocks that do not lie inside the blob. */
	if (off_struct > total || size_struct > total - off_struct)
		return FDT_E_BOUNDS;
	if (off_strings > total || size_strings > total - off_strings)
		return FDT_E_BOUNDS;
	if (off_reserve > total)
		return FDT_E_BOUNDS;

	/* Starts the answer and reads the reserved ranges. */
	hal_memset(info, 0, sizeof(*info));
	info->totalsize = total;
	error = read_reserve_map(data, total, off_reserve, info);
	if (error != FDT_OK)
		return error;

	/* Walks the structure block token by token. */
	parse.data = data;
	parse.strings = data + off_strings;
	parse.size_strings = size_strings;
	parse.cursor = data + off_struct;
	parse.end = parse.cursor + size_struct;
	parse.depth = -1;
	parse.info = info;
	while (parse.cursor + 4 <= parse.end) {
		token = read_be32(parse.cursor);
		parse.cursor += 4;

		/* Dispatches on the token. */
		switch (token) {
		case FDT_BEGIN_NODE:
			error = begin_node(&parse);
			break;
		case FDT_PROP:
			error = read_property(&parse);
			break;
		case FDT_END_NODE:
			error = end_node(&parse);
			break;
		case FDT_NOP:
			error = FDT_OK;
			break;
		case FDT_END:
			/* The end must close every node, and the tree must be a Pi 4's. */
			if (parse.depth != -1)
				return FDT_E_STRUCTURE;
			if (!info->compatible_rpi4)
				return FDT_E_NOT_RPI4;
			return FDT_OK;
		default:
			error = FDT_E_STRUCTURE;
			break;
		}

		/* Stops at the first malformed token. */
		if (error != FDT_OK)
			return error;
	}

	/* Failed: the structure block ended without its end token. */
	return FDT_E_STRUCTURE;
}

/*
 * Names a parse error for the boot log.
 */
const char *
rpi4_fdt_error(
	int error)
{
	static const char *const names[] = {"ok", "header", "bounds", "structure",
					    "depth", "cells", "overflow", "not-rpi4"};

	/* A number outside the table has no name. */
	if (error < 0 || (unsigned)error >= sizeof(names) / sizeof(names[0]))
		return "unknown";

	/* Succeeded: the error's name. */
	return names[error];
}

/* Records what a closed node says, once all its properties are read. */
static int
commit_node(
	struct fdt_node *node,
	const struct fdt_node *parent,
	struct rpi4_fdt_info *info)
{
	int error;

	/* The root (or any node) naming the board marks the tree a Pi 4's. */
	if (node->is_rpi4)
		info->compatible_rpi4 = 1;

	/* A CPU of /cpus. */
	if (node->is_cpu) {
		error = commit_cpu(node, info);
		if (error != FDT_OK)
			return error;
	}

	/* A memory node's ranges. */
	if (node->is_memory) {
		error = commit_memory(node, info);
		if (error != FDT_OK)
			return error;
	}

	/* A peripheral the HAL uses. */
	error = commit_device(node, parent, info);
	if (error != FDT_OK)
		return error;

	/* Succeeded: the node is recorded. */
	return FDT_OK;
}

/* Records every range of a memory node's reg. */
static int
commit_memory(
	const struct fdt_node *node,
	struct rpi4_fdt_info *info)
{
	unsigned stride;
	unsigned offset;
	uint64_t base;
	uint64_t size;
	int error;

	/* A node whose parent gives no cells has no ranges to read. */
	stride = node->parent_addr_cells + node->parent_size_cells;
	if (stride == 0)
		return FDT_OK;

	/* Reads each base and size pair. */
	for (offset = 0; offset + stride <= node->reg_count; offset += stride) {
		error = cells_to_u64(node->reg + offset, node->parent_addr_cells, &base);
		if (error != FDT_OK)
			return error;
		error = cells_to_u64(node->reg + offset + node->parent_addr_cells,
				     node->parent_size_cells,
				     &size);
		if (error != FDT_OK)
			return error;
		error = add_range(info->memory, &info->memory_count, RPI4_FDT_MAX_MEMORY, base, size);
		if (error != FDT_OK)
			return error;
	}

	/* Succeeded: every range is recorded. */
	return FDT_OK;
}

/*
 * Records the base, and the interrupt where the HAL needs it, of the
 * first UART, mailbox, GIC and eMMC2 controller the tree lists.
 */
static int
commit_device(
	const struct fdt_node *node,
	const struct fdt_node *parent,
	struct rpi4_fdt_info *info)
{
	unsigned stride;
	uint64_t base;
	uint64_t size;
	int wanted;
	int error;

	/* Only the peripherals the HAL maps itself. */
	wanted = node->is_uart || node->is_mailbox || node->is_gic || node->is_sdhci;
	if (!wanted)
		return FDT_OK;

	/* Requires one whole base and size in reg. */
	stride = node->parent_addr_cells + node->parent_size_cells;
	if (stride == 0 || node->reg_count < stride)
		return FDT_OK;

	/* Reads the first range and translates it to a CPU address. */
	error = cells_to_u64(node->reg, node->parent_addr_cells, &base);
	if (error != FDT_OK)
		return error;
	error = cells_to_u64(node->reg + node->parent_addr_cells, node->parent_size_cells, &size);
	if (error != FDT_OK || size == 0)
		return FDT_E_CELLS;
	error = translate(parent, base, &base);
	if (error != FDT_OK)
		return error;

	/* The first UART, with its interrupt. */
	if (node->is_uart && info->uart_base == 0) {
		info->uart_base = base;
		info->uart_irq = decode_irq(node->interrupts, node->interrupt_count);
	}

	/* The first mailbox. */
	if (node->is_mailbox && info->mailbox_base == 0)
		info->mailbox_base = base;

	/* The first eMMC2 controller, with its interrupt. */
	if (node->is_sdhci && info->sdhci_base == 0) {
		info->sdhci_base = base;
		info->sdhci_irq = decode_irq(node->interrupts, node->interrupt_count);
	}

	/* The first GIC: the distributor, then the CPU interface from the second range. */
	if (node->is_gic && info->gic_dist_base == 0) {
		info->gic_dist_base = base;
		if (node->reg_count >= stride * 2U) {
			error = cells_to_u64(node->reg + stride, node->parent_addr_cells, &base);
			if (error != FDT_OK)
				return error;
			error = translate(parent, base, &base);
			if (error != FDT_OK)
				return error;
			info->gic_cpu_base = base;
		}
	}

	/* Succeeded: the peripheral is recorded. */
	return FDT_OK;
}

/*
 * Records one cpu node: its affinity from reg, and its spin table.
 *
 * A tree with more CPUs than the table holds keeps the first ones; the
 * HAL brings up no more than four anyway.
 */
static int
commit_cpu(
	const struct fdt_node *node,
	struct rpi4_fdt_info *info)
{
	struct rpi4_fdt_cpu *cpu;
	uint64_t mpidr;
	int error;

	/* A cpu node without an address cannot be named to the interrupt controller. */
	if (node->parent_addr_cells == 0 || node->reg_count < node->parent_addr_cells)
		return FDT_E_CELLS;

	/* The reg cells are the CPU's affinity. */
	error = cells_to_u64(node->reg, node->parent_addr_cells, &mpidr);
	if (error != FDT_OK)
		return error;

	/* CPUs past the table are left out. */
	if (info->cpu_count >= RPI4_FDT_MAX_CPUS)
		return FDT_OK;

	/* Appends the CPU. */
	cpu = &info->cpus[info->cpu_count];
	cpu->mpidr = mpidr;
	cpu->release_addr = node->release_addr;
	cpu->spin_table = node->spin_table;
	info->cpu_count++;

	/* Succeeded: the CPU is recorded. */
	return FDT_OK;
}

/* Reads the memory reservation block, up to its empty entry. */
static int
read_reserve_map(
	const uint8_t *data,
	uint32_t total,
	uint32_t off_reserve,
	struct rpi4_fdt_info *info)
{
	uint32_t offset;
	uint64_t base;
	uint64_t size;
	int error;

	/* Reads each 16-byte base and size until the terminating zero pair. */
	for (offset = off_reserve; offset + 16U <= total; offset += 16U) {
		base = read_be64(data + offset);
		size = read_be64(data + offset + 8);
		if (base == 0 && size == 0)
			break;
		error = add_range(info->reserved, &info->reserved_count, RPI4_FDT_MAX_RESERVED, base, size);
		if (error != FDT_OK)
			return error;
	}

	/* Succeeded: every reservation is recorded. */
	return FDT_OK;
}

/*
 * Opens a node: it inherits its parent's cell counts until its own
 * properties say otherwise.
 */
static int
begin_node(
	struct fdt_parse *parse)
{
	struct fdt_node *node;
	size_t length;
	int chosen;
	int error;

	/* Refuses a tree deeper than the stack. */
	parse->depth++;
	if (parse->depth >= FDT_MAX_DEPTH)
		return FDT_E_DEPTH;

	/* Reads the node's name, which must end inside the block. */
	error = bounded_string(parse->cursor, parse->end, &length);
	if (error != FDT_OK)
		return error;

	/* The root's parent has two address cells and one size cell. */
	node = &parse->stack[parse->depth];
	hal_memset(node, 0, sizeof(*node));
	if (parse->depth == 0) {
		node->parent_addr_cells = 2;
		node->parent_size_cells = 1;
		node->addr_cells = 2;
		node->size_cells = 1;
	} else {
		node->parent_addr_cells = parse->stack[parse->depth - 1].addr_cells;
		node->parent_size_cells = parse->stack[parse->depth - 1].size_cells;
		node->addr_cells = parse->stack[parse->depth - 1].addr_cells;
		node->size_cells = parse->stack[parse->depth - 1].size_cells;
	}

	/* The /chosen node carries the firmware's command line. */
	chosen = string_equal(parse->cursor, length, "chosen");
	node->is_chosen = (uint8_t)(parse->depth == 1 && chosen);

	/* Steps over the name and its padding. */
	parse->cursor += (length + 1 + 3) & ~(size_t)3;

	/* Succeeded: the node is open. */
	return FDT_OK;
}

/* Reads one property of the open node and records what it says. */
static int
read_property(
	struct fdt_parse *parse)
{
	struct fdt_node *node;
	const uint8_t *value;
	const uint8_t *name;
	size_t name_length;
	uint32_t length;
	uint32_t nameoff;
	int bootargs;
	int error;

	/* Requires an open node and a whole property header. */
	if (parse->depth < 0 || parse->cursor + 8 > parse->end)
		return FDT_E_STRUCTURE;
	length = read_be32(parse->cursor);
	nameoff = read_be32(parse->cursor + 4);
	parse->cursor += 8;

	/* Requires a value inside the block and a name inside the strings. */
	if (length > (uint32_t)(parse->end - parse->cursor) || nameoff >= parse->size_strings)
		return FDT_E_BOUNDS;
	value = parse->cursor;
	name = parse->strings + nameoff;
	error = bounded_string(name, parse->strings + parse->size_strings, &name_length);
	if (error != FDT_OK)
		return error;
	node = &parse->stack[parse->depth];

	/* Notes where the firmware's command line is. */
	bootargs = string_equal(name, name_length, "bootargs");
	if (node->is_chosen && bootargs) {
		parse->info->bootargs_offset = (uint32_t)(value - parse->data);
		parse->info->bootargs_length = length;
	}

	/* Records what the node's other properties say about it. */
	record_property(node, name, name_length, value, length);
	error = record_cells(node, name, name_length, value, length);
	if (error != FDT_OK)
		return error;

	/* Steps over the value and its padding. */
	parse->cursor += (length + 3U) & ~3U;

	/* Succeeded: the property is read. */
	return FDT_OK;
}

/* Records a property that names what kind of node this is. */
static void
record_property(
	struct fdt_node *node,
	const uint8_t *name,
	size_t name_length,
	const uint8_t *value,
	uint32_t length)
{
	int matches;

	/* The node's own cell counts, for its children. */
	matches = string_equal(name, name_length, "#address-cells");
	if (matches && length == 4) {
		node->addr_cells = (uint8_t)read_be32(value);
		return;
	}
	matches = string_equal(name, name_length, "#size-cells");
	if (matches && length == 4) {
		node->size_cells = (uint8_t)read_be32(value);
		return;
	}

	/* A memory or cpu node by its device_type. */
	matches = string_equal(name, name_length, "device_type");
	if (matches && length >= 7 && string_equal(value, 6, "memory")) {
		node->is_memory = 1;
		return;
	}
	if (matches && length >= 4 && string_equal(value, 3, "cpu")) {
		node->is_cpu = 1;
		return;
	}

	/* A CPU held in the firmware's spin table, and where it waits. */
	matches = string_equal(name, name_length, "enable-method");
	if (matches) {
		node->spin_table = (uint8_t)compatible_has(value, length, "spin-table");
		return;
	}
	matches = string_equal(name, name_length, "cpu-release-addr");
	if (matches && length == 8) {
		node->release_addr = read_be64(value);
		return;
	}
	if (matches && length == 4) {
		node->release_addr = read_be32(value);
		return;
	}

	/* The board and the peripherals the HAL uses, by compatible string. */
	matches = string_equal(name, name_length, "compatible");
	if (matches) {
		node->is_rpi4 |= compatible_has(value, length, "raspberrypi,4-model-b");
		node->is_rpi4 |= compatible_has(value, length, "brcm,bcm2711");
		node->is_uart |= compatible_has(value, length, "arm,pl011");
		node->is_mailbox |= compatible_has(value, length, "brcm,bcm2835-mbox");
		node->is_gic |= compatible_has(value, length, "arm,gic-400");
		node->is_sdhci |= compatible_has(value, length, "brcm,bcm2711-emmc2");
	}
}

/* Copies reg, ranges or interrupts into the node's cells, up to their room. */
static int
record_cells(
	struct fdt_node *node,
	const uint8_t *name,
	size_t name_length,
	const uint8_t *value,
	uint32_t length)
{
	uint32_t *out;
	uint32_t *out_count;
	uint32_t count;
	unsigned maximum;
	unsigned index;
	int is_reg;
	int is_ranges;
	int is_interrupts;

	/* Picks where the cells go, or leaves any other property alone. */
	is_reg = string_equal(name, name_length, "reg");
	is_ranges = string_equal(name, name_length, "ranges");
	is_interrupts = string_equal(name, name_length, "interrupts");
	if (is_reg) {
		out = node->reg;
		out_count = &node->reg_count;
		maximum = FDT_MAX_CELLS;
	} else if (is_ranges) {
		out = node->ranges;
		out_count = &node->ranges_count;
		maximum = FDT_MAX_CELLS;
	} else if (is_interrupts) {
		out = node->interrupts;
		out_count = &node->interrupt_count;
		maximum = 12;
	} else {
		return FDT_OK;
	}

	/* Cells are whole 32-bit words. */
	if ((length & 3U) != 0)
		return FDT_E_CELLS;

	/* Copies as many cells as there is room for. */
	count = length / 4;
	if (count > maximum)
		count = maximum;
	for (index = 0; index < count; index++)
		out[index] = read_be32(value + index * 4);
	*out_count = count;

	/* Succeeded: the cells are recorded. */
	return FDT_OK;
}

/* Closes the open node and records what it said. */
static int
end_node(
	struct fdt_parse *parse)
{
	int error;

	/* Refuses an end with no node open. */
	if (parse->depth < 0)
		return FDT_E_STRUCTURE;

	/* The root names the board; any other node is recorded against its parent. */
	if (parse->stack[parse->depth].is_rpi4)
		parse->info->compatible_rpi4 = 1;
	if (parse->depth > 0) {
		error = commit_node(&parse->stack[parse->depth], &parse->stack[parse->depth - 1], parse->info);
		if (error != FDT_OK)
			return error;
	}
	parse->depth--;

	/* Succeeded: the node is closed. */
	return FDT_OK;
}
