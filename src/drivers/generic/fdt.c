/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Read-only access to a flattened device tree.
 *
 * The blob is big-endian throughout.  Its header names a structure block, a
 * stream of 32-bit tokens that opens and closes nodes and lists their
 * properties, and a strings block that holds the property names.  Every read
 * below is bounded by the limits drv_fdt_open() checked, so a damaged blob
 * produces an error rather than a read outside it.
 */

#include <drivers/generic/fdt.h>
#include <kern/kcrt.h>
#include <uapi/errno.h>

/* The magic number every device tree blob starts with. */
#define FDT_MAGIC		0xd00dfeedU

/* The oldest header layout that carries the strings block size. */
#define FDT_OLDEST_VERSION	17U

/* The size of the version 17 header. */
#define FDT_HEADER_SIZE		40U

/* Header field offsets. */
#define FDT_HEADER_MAGIC	0U
#define FDT_HEADER_TOTALSIZE	4U
#define FDT_HEADER_STRUCTURE	8U
#define FDT_HEADER_STRINGS	12U
#define FDT_HEADER_VERSION	20U
#define FDT_HEADER_COMPATIBLE	24U
#define FDT_HEADER_STRINGS_SIZE	32U
#define FDT_HEADER_STRUCTURE_SIZE	36U

/* Structure block tokens. */
#define FDT_TOKEN_BEGIN_NODE	1U
#define FDT_TOKEN_END_NODE	2U
#define FDT_TOKEN_PROPERTY	3U
#define FDT_TOKEN_NOP		4U
#define FDT_TOKEN_END		9U

/* The deepest node nesting the parent search follows. */
#define FDT_MAX_DEPTH		32U

/* The widest address, in cells, that translation handles. */
#define FDT_MAX_ADDRESS_CELLS	2U

/*
 * One token of the structure block, decoded.
 *
 * For a begin-node token the payload is the node name; for a property token
 * it is the property value, and the name offset points into the strings
 * block.  Next is where the following token starts.
 */
struct fdt_token {
	uint32_t kind;
	uint32_t payload;
	uint32_t payload_length;
	uint32_t name_offset;
	uint32_t next;
};

static uint32_t read_be32(const uint8_t *bytes);
static int read_token(const struct drv_fdt *fdt, uint32_t offset, struct fdt_token *token);
static int node_content(const struct drv_fdt *fdt, uint32_t node, uint32_t *content);
static bool property_name_is(const struct drv_fdt *fdt, uint32_t name_offset, const char *name);
static bool string_list_contains(const uint8_t *list, uint32_t length, const char *wanted);
static int translate_through(const struct drv_fdt *fdt, uint32_t bus, uint64_t *address);

/*
 * Checks a device tree header and records where its blocks are.
 *
 * Available is how many bytes may be read at the blob; the header's own
 * total size must fit inside it.
 */
int
drv_fdt_open(
	struct drv_fdt *fdt,
	const void *blob,
	size_t available)
{
	const uint8_t *bytes;
	uint32_t magic;
	uint32_t total_size;
	uint32_t version;
	uint32_t compatible_version;

	/* Refuses missing storage and a blob too small for a header. */
	if (fdt == NULL || blob == NULL)
		return EINVAL;
	if (available < FDT_HEADER_SIZE)
		return EINVAL;

	/* Refuses anything that is not a device tree. */
	bytes = blob;
	magic = read_be32(bytes + FDT_HEADER_MAGIC);
	if (magic != FDT_MAGIC)
		return EINVAL;

	/* Refuses a total size outside what the caller let us read. */
	total_size = read_be32(bytes + FDT_HEADER_TOTALSIZE);
	if (total_size < FDT_HEADER_SIZE)
		return EINVAL;
	if (total_size > available)
		return EINVAL;

	/*
	 * Version 17 introduced the strings block size, which the bounds below
	 * need.  A newer blob stays readable as long as it says it is
	 * compatible with 17.
	 */
	version = read_be32(bytes + FDT_HEADER_VERSION);
	compatible_version = read_be32(bytes + FDT_HEADER_COMPATIBLE);
	if (version < FDT_OLDEST_VERSION)
		return ENOTSUP;
	if (compatible_version > FDT_OLDEST_VERSION)
		return ENOTSUP;

	/* Records the block limits from the header. */
	fdt->blob = bytes;
	fdt->size = total_size;
	fdt->structure_offset = read_be32(bytes + FDT_HEADER_STRUCTURE);
	fdt->structure_size = read_be32(bytes + FDT_HEADER_STRUCTURE_SIZE);
	fdt->strings_offset = read_be32(bytes + FDT_HEADER_STRINGS);
	fdt->strings_size = read_be32(bytes + FDT_HEADER_STRINGS_SIZE);

	/* Refuses a structure block that is misaligned or leaves the blob. */
	if ((fdt->structure_offset & 3U) != 0)
		return EINVAL;
	if (fdt->structure_offset < FDT_HEADER_SIZE)
		return EINVAL;
	if (fdt->structure_offset > total_size)
		return EINVAL;
	if (fdt->structure_size > total_size - fdt->structure_offset)
		return EINVAL;

	/* Refuses a strings block that leaves the blob. */
	if (fdt->strings_offset > total_size)
		return EINVAL;
	if (fdt->strings_size > total_size - fdt->strings_offset)
		return EINVAL;

	/* Succeeded: the blob's blocks lie inside it. */
	return 0;
}

/*
 * Finds the next node whose compatible list names the given string.
 *
 * The search starts after the node named by after, or at the root when after
 * is DRV_FDT_NO_NODE.  ENOENT means no later node matches.
 */
int
drv_fdt_find_compatible(
	const struct drv_fdt *fdt,
	const char *compatible,
	uint32_t after,
	uint32_t *node)
{
	struct fdt_token token;
	const uint8_t *value;
	uint32_t length;
	uint32_t offset;
	bool matched;
	int error;

	/* Refuses a missing blob, name or result. */
	if (fdt == NULL || compatible == NULL || node == NULL)
		return EINVAL;

	/* Starts at the root, or just after the begin-node token of after. */
	offset = fdt->structure_offset;
	if (after != DRV_FDT_NO_NODE) {
		/* Decodes the start node to learn where its token ends. */
		error = read_token(fdt, after, &token);
		if (error != 0)
			return error;

		/* Resumes the walk at the token after it. */
		offset = token.next;
	}

	/* Visits every later begin-node token until the end of the structure. */
	for (;;) {
		/* Decodes the token at the walk's position. */
		error = read_token(fdt, offset, &token);
		if (error != 0)
			return error;

		/* Reports that no node after the start matches. */
		if (token.kind == FDT_TOKEN_END)
			return ENOENT;

		/* Tests the compatible list of each node the walk enters. */
		if (token.kind == FDT_TOKEN_BEGIN_NODE) {
			/* A node without a compatible property matches nothing. */
			error = drv_fdt_property(fdt, offset, "compatible", &value, &length);
			if (error == 0) {
				/* Stops at the first node that lists the name. */
				matched = string_list_contains(value, length, compatible);
				if (matched) {
					*node = offset;
					return 0;
				}
			}
		}

		/* Moves on to the token after this one. */
		offset = token.next;
	}
}

/*
 * Finds the node that carries the given phandle.
 *
 * Interrupt maps and resets name other nodes by phandle, a number the
 * compiler gave each referenced node.  ENOENT means no node carries it.
 */
int
drv_fdt_find_phandle(
	const struct drv_fdt *fdt,
	uint32_t phandle,
	uint32_t *node)
{
	struct fdt_token token;
	const uint8_t *value;
	uint32_t length;
	uint32_t offset;
	uint32_t stored;
	int error;

	/* Refuses a missing blob or result. */
	if (fdt == NULL || node == NULL)
		return EINVAL;

	/* Visits every begin-node token of the structure. */
	offset = fdt->structure_offset;
	for (;;) {
		/* Decodes the token at the walk's position. */
		error = read_token(fdt, offset, &token);
		if (error != 0)
			return error;

		/* Reports that no node carries the phandle. */
		if (token.kind == FDT_TOKEN_END)
			return ENOENT;

		/* Compares the phandle property of each node the walk enters. */
		if (token.kind == FDT_TOKEN_BEGIN_NODE) {
			/* A node without a phandle is never referenced. */
			error = drv_fdt_property(fdt, offset, "phandle", &value, &length);
			if (error == 0 && length == 4U) {
				/* Stops at the node the number belongs to. */
				stored = read_be32(value);
				if (stored == phandle) {
					*node = offset;
					return 0;
				}
			}
		}

		/* Moves on to the token after this one. */
		offset = token.next;
	}
}

/*
 * Finds the node that contains the given node.
 *
 * The blob keeps no parent links, so the walk tracks the nodes it is inside
 * of.  ENOENT means the node is the root, which has no parent.
 */
int
drv_fdt_parent(
	const struct drv_fdt *fdt,
	uint32_t node,
	uint32_t *parent)
{
	uint32_t open_nodes[FDT_MAX_DEPTH];
	struct fdt_token token;
	uint32_t depth;
	uint32_t offset;
	int error;

	/* Refuses a missing blob, node or result. */
	if (fdt == NULL || parent == NULL || node == DRV_FDT_NO_NODE)
		return EINVAL;

	/* Walks the structure keeping the chain of nodes the walk is inside. */
	depth = 0;
	offset = fdt->structure_offset;
	for (;;) {
		/* Decodes the token at the walk's position. */
		error = read_token(fdt, offset, &token);
		if (error != 0)
			return error;

		/* Reports a node that does not exist in this blob. */
		if (token.kind == FDT_TOKEN_END)
			return ENOENT;

		/* Enters a node, which is the answer when it is the one asked for. */
		if (token.kind == FDT_TOKEN_BEGIN_NODE) {
			/* The root has no parent. */
			if (offset == node && depth == 0)
				return ENOENT;

			/* The innermost open node contains the node asked for. */
			if (offset == node) {
				*parent = open_nodes[depth - 1U];
				return 0;
			}

			/* Refuses nesting deeper than any real device tree has. */
			if (depth == FDT_MAX_DEPTH)
				return ERANGE;

			/* Records the node as the innermost one the walk is inside. */
			open_nodes[depth] = offset;
			depth++;
		} else if (token.kind == FDT_TOKEN_END_NODE) {
			/* Refuses an end-node token that closes nothing. */
			if (depth == 0)
				return EINVAL;

			/* Leaves the innermost node. */
			depth--;
		}

		/* Moves on to the token after this one. */
		offset = token.next;
	}
}

/*
 * Looks up one property of a node.
 *
 * On success value points at the property's bytes inside the blob and
 * length says how many there are.  ENOENT means the node lacks it.
 */
int
drv_fdt_property(
	const struct drv_fdt *fdt,
	uint32_t node,
	const char *name,
	const uint8_t **value,
	uint32_t *length)
{
	struct fdt_token token;
	uint32_t offset;
	bool named;
	int error;

	/* Refuses a missing blob, name or result. */
	if (fdt == NULL || name == NULL || value == NULL || length == NULL)
		return EINVAL;

	/* Finds where the node's own properties start. */
	error = node_content(fdt, node, &offset);
	if (error != 0)
		return error;

	/* Visits the properties, which come before any child node. */
	for (;;) {
		/* Decodes the token at the walk's position. */
		error = read_token(fdt, offset, &token);
		if (error != 0)
			return error;

		/* A child node or the node's end means the property is absent. */
		if (token.kind != FDT_TOKEN_PROPERTY && token.kind != FDT_TOKEN_NOP)
			return ENOENT;

		/* Stops at the property with the requested name. */
		if (token.kind == FDT_TOKEN_PROPERTY) {
			named = property_name_is(fdt, token.name_offset, name);
			if (named) {
				*value = fdt->blob + token.payload;
				*length = token.payload_length;
				return 0;
			}
		}

		/* Moves on to the token after this one. */
		offset = token.next;
	}
}

/*
 * Reports whether a node describes a device that is present.
 *
 * A node without a status property is present, as is one whose status is
 * "okay" or the older "ok".  Anything else, such as "disabled", is not.
 */
bool
drv_fdt_node_enabled(
	const struct drv_fdt *fdt,
	uint32_t node)
{
	const uint8_t *value;
	uint32_t length;
	int difference;
	int error;

	/* Treats a node without a status as present. */
	error = drv_fdt_property(fdt, node, "status", &value, &length);
	if (error == ENOENT)
		return true;

	/* Treats an unreadable node as absent. */
	if (error != 0)
		return false;

	/* Accepts "okay", the current spelling of an enabled status. */
	if (length == 5U) {
		difference = kern_memcmp(value, "okay", 5U);
		if (difference == 0)
			return true;
	}

	/* Accepts "ok", the spelling older trees use. */
	if (length == 3U) {
		difference = kern_memcmp(value, "ok", 3U);
		if (difference == 0)
			return true;
	}

	/* Reports every other status as absent. */
	return false;
}

/*
 * Reads a one-cell count property such as #address-cells of a node.
 *
 * The fallback is what the device tree specification says an absent
 * property means for the name asked about.
 */
uint32_t
drv_fdt_node_cells(
	const struct drv_fdt *fdt,
	uint32_t node,
	const char *name,
	uint32_t fallback)
{
	const uint8_t *value;
	uint32_t length;
	int error;

	/* Uses the fallback when the node does not say. */
	error = drv_fdt_property(fdt, node, name, &value, &length);
	if (error != 0)
		return fallback;
	if (length != 4U)
		return fallback;

	/* Reports the count the node states. */
	return read_be32(value);
}

/*
 * Reads a number stored in consecutive cells of a property value.
 *
 * Cell index counts 32-bit cells from the start of the value.  Only the low
 * 64 bits of a wider number are kept.
 */
uint64_t
drv_fdt_cells_value(
	const uint8_t *value,
	uint32_t cell_index,
	uint32_t cell_count)
{
	uint64_t number;
	uint32_t cell;

	/* Accumulates the cells most significant first. */
	number = 0;
	for (cell = 0; cell < cell_count; cell++)
		number = (number << 32) | read_be32(value + (cell_index + cell) * 4U);

	/* Reports the assembled number. */
	return number;
}

/*
 * Translates an address to the CPU's physical address space.
 *
 * The address is one that node's reg property would hold, so it is in the
 * space of node's parent bus.  Each bus above the node maps its children's
 * addresses to its own parent's through its ranges property; the walk applies
 * them up to the root.  ENOENT means some bus has no window for the address.
 */
int
drv_fdt_translate(
	const struct drv_fdt *fdt,
	uint32_t node,
	uint64_t address,
	uint64_t *result)
{
	uint32_t bus;
	uint32_t above;
	int error;

	/* Refuses a missing blob or result. */
	if (fdt == NULL || result == NULL)
		return EINVAL;

	/* Starts at the bus the node sits on. */
	error = drv_fdt_parent(fdt, node, &bus);
	if (error != 0)
		return error;

	/* Applies the window of every bus until the root is reached. */
	for (;;) {
		/* The root's address space is the CPU's. */
		error = drv_fdt_parent(fdt, bus, &above);
		if (error == ENOENT)
			break;
		if (error != 0)
			return error;

		/* Moves the address into the space of the bus above. */
		error = translate_through(fdt, bus, &address);
		if (error != 0)
			return error;

		/* Continues with the bus above. */
		bus = above;
	}

	/* Succeeded: the address is now a CPU physical address. */
	*result = address;
	return 0;
}

/*
 * Reads one address and size pair of a node's reg property.
 *
 * The address is translated to the CPU's physical address space.  ENOENT
 * means the node has fewer pairs than index.
 */
int
drv_fdt_reg(
	const struct drv_fdt *fdt,
	uint32_t node,
	unsigned index,
	uint64_t *address,
	uint64_t *size)
{
	const uint8_t *value;
	uint32_t length;
	uint32_t parent;
	uint32_t address_cells;
	uint32_t size_cells;
	uint32_t pair_cells;
	uint64_t bus_address;
	int error;

	/* Refuses a missing blob or result. */
	if (fdt == NULL || address == NULL || size == NULL)
		return EINVAL;

	/* Reads the cell counts the parent bus gives its children. */
	error = drv_fdt_parent(fdt, node, &parent);
	if (error != 0)
		return error;

	/* An absent count means two address cells and one size cell. */
	address_cells = drv_fdt_node_cells(fdt, parent, "#address-cells", 2U);
	size_cells = drv_fdt_node_cells(fdt, parent, "#size-cells", 1U);

	/* Refuses an address wider than translation handles. */
	if (address_cells == 0 || address_cells > FDT_MAX_ADDRESS_CELLS)
		return ERANGE;
	if (size_cells > 2U)
		return ERANGE;

	/* Finds the requested pair inside the reg property. */
	error = drv_fdt_property(fdt, node, "reg", &value, &length);
	if (error != 0)
		return error;

	/* Refuses an index past the last pair of the property. */
	pair_cells = address_cells + size_cells;
	if ((uint64_t)(index + 1U) * pair_cells * 4U > length)
		return ENOENT;

	/* Decodes the pair and moves the address into the CPU's space. */
	bus_address = drv_fdt_cells_value(value, index * pair_cells, address_cells);
	*size = drv_fdt_cells_value(value, index * pair_cells + address_cells, size_cells);
	error = drv_fdt_translate(fdt, node, bus_address, address);
	if (error != 0)
		return error;

	/* Succeeded: address and size describe the requested window. */
	return 0;
}

/* Reads a big-endian 32-bit number. */
static uint32_t
read_be32(
	const uint8_t *bytes)
{
	uint32_t number;

	/* Assembles the four bytes most significant first. */
	number = (uint32_t)bytes[0] << 24;
	number |= (uint32_t)bytes[1] << 16;
	number |= (uint32_t)bytes[2] << 8;
	number |= (uint32_t)bytes[3];

	/* Reports the host-order number. */
	return number;
}

/* Decodes the structure block token at an offset of the blob. */
static int
read_token(
	const struct drv_fdt *fdt,
	uint32_t offset,
	struct fdt_token *token)
{
	uint32_t structure_end;
	uint32_t name_end;
	uint32_t padded;

	/* Refuses an offset outside the structure block or not on a cell. */
	structure_end = fdt->structure_offset + fdt->structure_size;
	if ((offset & 3U) != 0)
		return EINVAL;
	if (offset < fdt->structure_offset)
		return EINVAL;
	if (offset > structure_end - 4U)
		return EINVAL;

	/* Reads the token kind; most tokens carry nothing more. */
	token->kind = read_be32(fdt->blob + offset);
	token->payload = offset + 4U;
	token->payload_length = 0;
	token->name_offset = 0;
	token->next = offset + 4U;

	/* Measures what each kind of token carries after its kind. */
	switch (token->kind) {
	case FDT_TOKEN_BEGIN_NODE:
		/* The node name runs to its terminating zero byte. */
		name_end = token->payload;
		while (name_end < structure_end && fdt->blob[name_end] != 0)
			name_end++;

		/* Refuses a name that runs past the structure block. */
		if (name_end == structure_end)
			return EINVAL;

		/* The next token starts on the cell after the name's zero byte. */
		token->payload_length = name_end - token->payload;
		token->next = (name_end + 1U + 3U) & ~3U;
		break;
	case FDT_TOKEN_PROPERTY:
		/* A property has its length and name offset before its value. */
		if (token->payload > structure_end - 8U)
			return EINVAL;

		/* Reads the value's length and the offset of its name. */
		token->payload_length = read_be32(fdt->blob + token->payload);
		token->name_offset = read_be32(fdt->blob + token->payload + 4U);
		token->payload += 8U;

		/* Refuses a value that runs past the structure block. */
		if (token->payload_length > structure_end - token->payload)
			return EINVAL;

		/* The next token starts on the cell after the value. */
		padded = (token->payload_length + 3U) & ~3U;
		token->next = token->payload + padded;
		break;
	case FDT_TOKEN_END_NODE:
	case FDT_TOKEN_NOP:
	case FDT_TOKEN_END:
		break;
	default:
		/* Refuses a token the format does not define. */
		return EINVAL;
	}

	/* Refuses a next token beyond the block, except after the last one. */
	if (token->kind != FDT_TOKEN_END && token->next > structure_end)
		return EINVAL;

	/* Succeeded: the token is decoded. */
	return 0;
}

/* Finds where the properties of a node start, just after its name. */
static int
node_content(
	const struct drv_fdt *fdt,
	uint32_t node,
	uint32_t *content)
{
	struct fdt_token token;
	int error;

	/* Refuses the offset of anything but a begin-node token. */
	error = read_token(fdt, node, &token);
	if (error != 0)
		return error;
	if (token.kind != FDT_TOKEN_BEGIN_NODE)
		return EINVAL;

	/* Succeeded: the properties follow the padded name. */
	*content = token.next;
	return 0;
}

/* Reports whether a strings block entry spells the given name. */
static bool
property_name_is(
	const struct drv_fdt *fdt,
	uint32_t name_offset,
	const char *name)
{
	uint32_t length;
	const char *stored;
	int difference;

	/* Refuses an entry outside the strings block. */
	length = (uint32_t)kern_strlen(name);
	if (name_offset >= fdt->strings_size)
		return false;
	if (length + 1U > fdt->strings_size - name_offset)
		return false;

	/* Compares the name and its terminating zero byte. */
	stored = (const char *)fdt->blob + fdt->strings_offset + name_offset;
	difference = kern_memcmp(stored, name, length + 1U);
	if (difference != 0)
		return false;

	/* Reports a match. */
	return true;
}

/* Reports whether a list of zero-terminated strings holds the wanted one. */
static bool
string_list_contains(
	const uint8_t *list,
	uint32_t length,
	const char *wanted)
{
	uint32_t wanted_length;
	uint32_t start;
	uint32_t end;
	int difference;

	/* Compares each string of the list in turn. */
	wanted_length = (uint32_t)kern_strlen(wanted);
	start = 0;
	while (start < length) {
		/* Finds the end of the current string. */
		end = start;
		while (end < length && list[end] != 0)
			end++;

		/* Stops at a string of the same length and bytes. */
		if (end - start == wanted_length) {
			/* The bytes decide once the lengths agree. */
			difference = kern_memcmp(list + start, wanted, wanted_length);
			if (difference == 0)
				return true;
		}

		/* Moves on to the string after this one's zero byte. */
		start = end + 1U;
	}

	/* Reports that no string of the list matched. */
	return false;
}

/*
 * Moves an address from a bus's child space into the bus's parent space.
 *
 * An empty ranges property means the two spaces are the same; an absent
 * one means the bus passes no addresses up.
 */
static int
translate_through(
	const struct drv_fdt *fdt,
	uint32_t bus,
	uint64_t *address)
{
	const uint8_t *value;
	uint32_t length;
	uint32_t above;
	uint32_t child_cells;
	uint32_t parent_cells;
	uint32_t size_cells;
	uint32_t entry_cells;
	uint32_t entry;
	uint64_t child_base;
	uint64_t parent_base;
	uint64_t window_size;
	int error;

	/* Reads the bus's ranges; an empty one passes addresses unchanged. */
	error = drv_fdt_property(fdt, bus, "ranges", &value, &length);
	if (error != 0)
		return error;
	if (length == 0)
		return 0;

	/* Reads how wide the entries of the ranges are. */
	error = drv_fdt_parent(fdt, bus, &above);
	if (error != 0)
		return error;

	/* Absent counts mean two address cells and one size cell. */
	child_cells = drv_fdt_node_cells(fdt, bus, "#address-cells", 2U);
	parent_cells = drv_fdt_node_cells(fdt, above, "#address-cells", 2U);
	size_cells = drv_fdt_node_cells(fdt, bus, "#size-cells", 1U);

	/* Refuses entries wider than translation handles. */
	if (child_cells == 0 || child_cells > FDT_MAX_ADDRESS_CELLS)
		return ERANGE;
	if (parent_cells == 0 || parent_cells > FDT_MAX_ADDRESS_CELLS)
		return ERANGE;
	if (size_cells == 0 || size_cells > 2U)
		return ERANGE;

	/* Finds the window that contains the address. */
	entry_cells = child_cells + parent_cells + size_cells;
	for (entry = 0; (entry + 1U) * entry_cells * 4U <= length; entry++) {
		child_base = drv_fdt_cells_value(value, entry * entry_cells, child_cells);
		parent_base = drv_fdt_cells_value(value, entry * entry_cells + child_cells, parent_cells);
		window_size = drv_fdt_cells_value(value, entry * entry_cells + child_cells + parent_cells, size_cells);

		/* Skips a window that starts after the address or ends before it. */
		if (*address < child_base)
			continue;
		if (*address - child_base >= window_size)
			continue;

		/* Succeeded: the address keeps its offset inside the window. */
		*address = parent_base + (*address - child_base);
		return 0;
	}

	/* Reports that no window of the bus covers the address. */
	return ENOENT;
}
