/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The geometry of elements for scripts (ws074-p031): getBoundingClientRect
 * and getClientRects with DOMRect, the client, offset and scroll sizes,
 * and the window's scroll position.
 *
 * The host says where a node is on the page as it is laid out now (it
 * lays the page out first when the document changed, as other browsers
 * do when a script asks), in CSS pixels from the document's top left; the
 * rectangles a script gets are the viewport's, the document's scroll taken
 * off.  The root element's client size is the viewport's and its scroll
 * size the document's; any other element's scroll size is its client size
 * (the layout does not keep how far a box's content overflows it), and
 * its scroll position is 0 (boxes do not scroll).  A node without a box
 * has an empty rectangle and sizes of 0.
 */

#include "bind/internal.h"

#include <errno.h>
#include <math.h>
#include <string.h>

/*
 * The measures of an element the getters report (the part a getter asks
 * geometry_measure for).
 */
enum geometry_part {
	GEOMETRY_CLIENT_WIDTH,
	GEOMETRY_CLIENT_HEIGHT,
	GEOMETRY_CLIENT_TOP,
	GEOMETRY_CLIENT_LEFT,
	GEOMETRY_SCROLL_WIDTH,
	GEOMETRY_SCROLL_HEIGHT,
	GEOMETRY_SCROLL_TOP,
	GEOMETRY_SCROLL_LEFT,
	GEOMETRY_OFFSET_WIDTH,
	GEOMETRY_OFFSET_HEIGHT
};

/*
 * The parts of a DOMRect its getters report (the part is the getter
 * function's data).
 */
enum geometry_rect_part {
	GEOMETRY_RECT_X,
	GEOMETRY_RECT_Y,
	GEOMETRY_RECT_WIDTH,
	GEOMETRY_RECT_HEIGHT,
	GEOMETRY_RECT_TOP,
	GEOMETRY_RECT_RIGHT,
	GEOMETRY_RECT_BOTTOM,
	GEOMETRY_RECT_LEFT
};

/*
 * The state of a DOMRect, the cell its object wraps: its place and size
 * in CSS pixels.  The width and height may be negative when a script makes
 * the rectangle so; top, right, bottom and left are the edges either way.
 */
struct geometry_rect {
	struct vm_cell cell;
	double x;
	double y;
	double width;
	double height;
};

/*
 * One member of DOMRect: its name and the part its getter reports.
 */
struct geometry_rect_member {
	const char *name;
	int part;
	int writable;
};

static int geometry_measure(struct vm_realm *realm, vm_value this_value, int part, vm_value *result);
static int geometry_element_this(struct vm_realm *realm, vm_value this_value, struct dom_element **element);
static int geometry_box(struct bind_window *window, struct dom_element *element, struct bind_box *box);
static int geometry_is_root(const struct bind_window *window, const struct dom_element *element);
static void geometry_scroll(const struct bind_window *window, double *x, double *y);
static double geometry_round(double value);
static int geometry_rect_create(struct bind_window *window, double x, double y, double width, double height, vm_value *value);
static int geometry_rect_of(struct vm_realm *realm, vm_value value, struct geometry_rect **rect);
static int geometry_rect_construct(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int geometry_rect_get(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int geometry_rect_set(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int geometry_rect_to_json(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static double geometry_rect_value(const struct geometry_rect *rect, int part);

/* The state of a DOMRect, which refers to nothing. */
static const struct vm_cell_type geometry_rect_type = { "dom-rect", NULL, NULL };

/*
 * The members of DOMRect, in the order toJSON writes them.  The table is
 * constant for the life of the program.
 */
static const struct geometry_rect_member geometry_rect_members[] = {
	{ "x", GEOMETRY_RECT_X, 1 },
	{ "y", GEOMETRY_RECT_Y, 1 },
	{ "width", GEOMETRY_RECT_WIDTH, 1 },
	{ "height", GEOMETRY_RECT_HEIGHT, 1 },
	{ "top", GEOMETRY_RECT_TOP, 0 },
	{ "right", GEOMETRY_RECT_RIGHT, 0 },
	{ "bottom", GEOMETRY_RECT_BOTTOM, 0 },
	{ "left", GEOMETRY_RECT_LEFT, 0 },
	{ NULL, 0, 0 }
};

/*
 * The operations of DOMRect; its attributes are installed from
 * geometry_rect_members by bind_geometry_install.  The table is constant
 * for the life of the program.
 */
static const struct bind_operation geometry_rect_operations[] = {
	{ "toJSON", 0, geometry_rect_to_json },
	{ NULL, 0, NULL }
};

/*
 * The DOMRect interface.
 */
const struct bind_interface bind_dom_rect_interface = {
	"DOMRect", BIND_NO_PARENT, 0, geometry_rect_construct, NULL, geometry_rect_operations, NULL
};

/*
 * Adds DOMRect's attributes to its prototype: an accessor for each part,
 * whose getter and setter know the part.
 */
int
bind_geometry_install(
	struct bind_window *window)
{
	const struct geometry_rect_member *member;
	struct vm_realm *realm;
	struct vm_function *getter;
	struct vm_function *setter;
	struct vm_accessor *accessor;
	vm_value setter_value;
	int status;

	/* Each member of the table. */
	realm = window->realm;
	for (member = geometry_rect_members; member->name != NULL; member++) {
		status = js_builtin_function(realm, member->name, 0, geometry_rect_get, NULL, &getter);
		if (status != 0)
			return status;
		getter->data = vm_value_int32(member->part);

		/* x, y, width and height can be set. */
		setter_value = VM_VALUE_UNDEFINED;
		if (member->writable) {
			status = js_builtin_function(realm, member->name, 1, geometry_rect_set, NULL, &setter);
			if (status != 0)
				return status;
			setter->data = vm_value_int32(member->part);
			setter_value = vm_value_cell(setter);
		}

		/* The accessor on the prototype. */
		accessor = vm_accessor_create(realm->heap, vm_value_cell(getter), setter_value);
		if (accessor == NULL)
			return ENOMEM;
		status = js_builtin_value(realm, window->prototypes[BIND_DOM_RECT], member->name, vm_value_cell(accessor), VM_PROPERTY_ACCESSOR | VM_PROPERTY_ENUMERABLE | VM_PROPERTY_CONFIGURABLE);
		if (status != 0)
			return status;
	}

	/* Succeeded: the prototype has every member. */
	return 0;
}

/*
 * Reports the rectangle an element takes in the viewport, the union of
 * its boxes' border boxes (getBoundingClientRect).
 */
int
bind_get_bounding_client_rect(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct bind_window *window;
	struct dom_element *element;
	struct bind_box box;
	double scroll_x;
	double scroll_y;
	int found;
	int status;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The element, and where it is on the page (nowhere without a box). */
	window = bind_window_of(realm);
	status = geometry_element_this(realm, this_value, &element);
	if (status != 0)
		return status;
	found = geometry_box(window, element, &box);

	/* The viewport's rectangle: the scroll taken off, unless there is no box, which is all zero. */
	scroll_x = 0.0;
	scroll_y = 0.0;
	if (found)
		geometry_scroll(window, &scroll_x, &scroll_y);

	/* Succeeded: the rectangle. */
	status = geometry_rect_create(window, box.x - scroll_x, box.y - scroll_y, box.width, box.height, result);
	if (status != 0)
		return status;
	return 0;
}

/*
 * Lists the rectangles of an element's boxes in the viewport
 * (getClientRects): one, their union, for an element with a box, and none
 * for one without.
 */
int
bind_get_client_rects(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct bind_window *window;
	struct dom_element *element;
	struct vm_object *array;
	struct bind_box box;
	vm_value rect;
	double scroll_x;
	double scroll_y;
	int found;
	int status;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The element, its box, and an empty list. */
	window = bind_window_of(realm);
	status = geometry_element_this(realm, this_value, &element);
	if (status != 0)
		return status;
	found = geometry_box(window, element, &box);
	status = bind_array_create(realm, &array);
	if (status != 0)
		return status;

	/* An element without a box has no rectangle. */
	if (!found) {
		*result = vm_value_cell(array);
		return 0;
	}

	/* The rectangle in the viewport. */
	geometry_scroll(window, &scroll_x, &scroll_y);
	status = geometry_rect_create(window, box.x - scroll_x, box.y - scroll_y, box.width, box.height, &rect);
	if (status != 0)
		return status;
	status = vm_object_define(realm->heap, array, vm_value_int32(0), rect, VM_PROPERTY_DEFAULT);
	if (status != 0)
		return status;

	/* Succeeded: the list. */
	*result = vm_value_cell(array);
	return 0;
}

/* Reports the width of an element's padding box, or the viewport's for the root (clientWidth). */
int
bind_client_width(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	int status;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The measure. */
	status = geometry_measure(realm, this_value, GEOMETRY_CLIENT_WIDTH, result);
	if (status != 0)
		return status;

	/* Succeeded: the width. */
	return 0;
}

/* Reports the height of an element's padding box, or the viewport's for the root (clientHeight). */
int
bind_client_height(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	int status;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The measure. */
	status = geometry_measure(realm, this_value, GEOMETRY_CLIENT_HEIGHT, result);
	if (status != 0)
		return status;

	/* Succeeded: the height. */
	return 0;
}

/* Reports the width of an element's top border (clientTop). */
int
bind_client_top(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	int status;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The measure. */
	status = geometry_measure(realm, this_value, GEOMETRY_CLIENT_TOP, result);
	if (status != 0)
		return status;

	/* Succeeded: the border. */
	return 0;
}

/* Reports the width of an element's left border (clientLeft). */
int
bind_client_left(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	int status;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The measure. */
	status = geometry_measure(realm, this_value, GEOMETRY_CLIENT_LEFT, result);
	if (status != 0)
		return status;

	/* Succeeded: the border. */
	return 0;
}

/* Reports the width of what an element scrolls over, or the document's for the root (scrollWidth). */
int
bind_scroll_width(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	int status;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The measure. */
	status = geometry_measure(realm, this_value, GEOMETRY_SCROLL_WIDTH, result);
	if (status != 0)
		return status;

	/* Succeeded: the width. */
	return 0;
}

/* Reports the height of what an element scrolls over, or the document's for the root (scrollHeight). */
int
bind_scroll_height(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	int status;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The measure. */
	status = geometry_measure(realm, this_value, GEOMETRY_SCROLL_HEIGHT, result);
	if (status != 0)
		return status;

	/* Succeeded: the height. */
	return 0;
}

/* Reports how far an element is scrolled down, the document's for the root (scrollTop). */
int
bind_scroll_top(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	int status;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The measure. */
	status = geometry_measure(realm, this_value, GEOMETRY_SCROLL_TOP, result);
	if (status != 0)
		return status;

	/* Succeeded: the distance. */
	return 0;
}

/* Reports how far an element is scrolled across, the document's for the root (scrollLeft). */
int
bind_scroll_left(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	int status;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The measure. */
	status = geometry_measure(realm, this_value, GEOMETRY_SCROLL_LEFT, result);
	if (status != 0)
		return status;

	/* Succeeded: the distance. */
	return 0;
}

/*
 * Takes a new scroll position for an element (the setter of scrollTop and
 * scrollLeft), which is ignored: boxes do not scroll in this pass, and the
 * document is scrolled by its viewer.
 */
int
bind_scroll_position_set(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct dom_element *element;
	double position;
	int status;

	/* The element, and the number, whose conversion may throw. */
	*result = VM_VALUE_UNDEFINED;
	status = geometry_element_this(realm, this_value, &element);
	if (status != 0)
		return status;
	status = vm_to_number(realm, js_argument(args, count, 0), &position);
	if (status != 0)
		return status;

	/* Succeeded: nothing moves. */
	return 0;
}

/* Reports the width of an element's border box (offsetWidth). */
int
bind_offset_width(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	int status;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The measure. */
	status = geometry_measure(realm, this_value, GEOMETRY_OFFSET_WIDTH, result);
	if (status != 0)
		return status;

	/* Succeeded: the width. */
	return 0;
}

/* Reports the height of an element's border box (offsetHeight). */
int
bind_offset_height(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	int status;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The measure. */
	status = geometry_measure(realm, this_value, GEOMETRY_OFFSET_HEIGHT, result);
	if (status != 0)
		return status;

	/* Succeeded: the height. */
	return 0;
}

/* Reports how far the document is scrolled across (scrollX and pageXOffset). */
int
bind_window_scroll_x(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct bind_window *window;
	double scroll_x;
	double scroll_y;

	UNUSED_PARAMETER(this_value);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The document's scroll. */
	window = bind_window_of(realm);
	geometry_scroll(window, &scroll_x, &scroll_y);

	/* Succeeded: across. */
	*result = vm_value_number(scroll_x);
	return 0;
}

/* Reports how far the document is scrolled down (scrollY and pageYOffset). */
int
bind_window_scroll_y(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct bind_window *window;
	double scroll_x;
	double scroll_y;

	UNUSED_PARAMETER(this_value);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The document's scroll. */
	window = bind_window_of(realm);
	geometry_scroll(window, &scroll_x, &scroll_y);

	/* Succeeded: down. */
	*result = vm_value_number(scroll_y);
	return 0;
}

/*
 * Reports one of an element's client, scroll or offset measures, in whole
 * pixels as other browsers report them.
 */
static int
geometry_measure(
	struct vm_realm *realm,
	vm_value this_value,
	int part,
	vm_value *result)
{
	struct bind_window *window;
	struct dom_element *element;
	struct bind_box box;
	double document_width;
	double document_height;
	double scroll_x;
	double scroll_y;
	double value;
	int found;
	int root;
	int status;

	/* The element, where it is, and whether it is the root, whose client area is the viewport. */
	window = bind_window_of(realm);
	status = geometry_element_this(realm, this_value, &element);
	if (status != 0)
		return status;
	found = geometry_box(window, element, &box);
	root = geometry_is_root(window, element);

	/* The document's size and scroll, which the root reports. */
	document_width = 0.0;
	document_height = 0.0;
	if (root && window->host.document_size != NULL)
		window->host.document_size(window->host.context, &document_width, &document_height);
	geometry_scroll(window, &scroll_x, &scroll_y);

	/* Chooses the measure. */
	value = 0.0;
	switch (part) {
	case GEOMETRY_CLIENT_WIDTH:
	case GEOMETRY_SCROLL_WIDTH:
		/* The padding box's width, the viewport's for the root, and nothing for an inline box. */
		if (root) {
			value = window->viewport_width;
		} else if (found && box.block) {
			value = box.width - box.border_left - box.border_right;
		}

		/* The root scrolls over the document when it is wider. */
		if (part == GEOMETRY_SCROLL_WIDTH && root && document_width > value)
			value = document_width;
		break;
	case GEOMETRY_CLIENT_HEIGHT:
	case GEOMETRY_SCROLL_HEIGHT:
		/* The padding box's height, the viewport's for the root, and nothing for an inline box. */
		if (root) {
			value = window->viewport_height;
		} else if (found && box.block) {
			value = box.height - box.border_top - box.border_bottom;
		}

		/* The root scrolls over the document when it is taller. */
		if (part == GEOMETRY_SCROLL_HEIGHT && root && document_height > value)
			value = document_height;
		break;
	case GEOMETRY_CLIENT_TOP:
		/* The top border of a block. */
		if (found && box.block)
			value = box.border_top;
		break;
	case GEOMETRY_CLIENT_LEFT:
		/* The left border of a block. */
		if (found && box.block)
			value = box.border_left;
		break;
	case GEOMETRY_SCROLL_TOP:
		/* Only the root scrolls, with the document. */
		if (root)
			value = scroll_y;
		break;
	case GEOMETRY_SCROLL_LEFT:
		/* Only the root scrolls, with the document. */
		if (root)
			value = scroll_x;
		break;
	case GEOMETRY_OFFSET_WIDTH:
		/* The border box's width. */
		if (found)
			value = box.width;
		break;
	default:
		/* The border box's height. */
		if (found)
			value = box.height;
		break;
	}

	/* Succeeded: the measure in whole pixels. */
	*result = vm_value_number(geometry_round(value));
	return 0;
}

/* Finds the element a geometry method's this value stands for, throwing a TypeError otherwise. */
static int
geometry_element_this(
	struct vm_realm *realm,
	vm_value this_value,
	struct dom_element **element)
{
	struct dom_node *node;
	int status;

	/* The node, which must be an element. */
	node = bind_node_of(this_value);
	if (node == NULL || node->type != DOM_ELEMENT) {
		status = bind_throw_illegal(realm);
		return status;
	}

	/* Succeeded: the element. */
	*element = (struct dom_element *)node;
	return 0;
}

/*
 * Asks the host where an element is on the page; reports whether it has
 * a box (without one, the box is all zero).
 */
static int
geometry_box(
	struct bind_window *window,
	struct dom_element *element,
	struct bind_box *box)
{
	int found;

	/* Nothing until the host says. */
	memset(box, 0, sizeof(*box));
	if (window->host.node_box == NULL)
		return 0;

	/* The host's answer; a node without a box leaves it zero. */
	found = window->host.node_box(window->host.context, &element->node, box);
	if (!found) {
		memset(box, 0, sizeof(*box));
		return 0;
	}

	/* Succeeded: the element has a box. */
	return 1;
}

/* Tells whether an element is the document's root element (the html element). */
static int
geometry_is_root(
	const struct bind_window *window,
	const struct dom_element *element)
{
	/* The root is the document's child. */
	if (element->node.parent != &window->document->node)
		return 0;

	/* Succeeded: it is the root. */
	return 1;
}

/* Finds how far the document is scrolled, in CSS pixels (0 when the host does not say). */
static void
geometry_scroll(
	const struct bind_window *window,
	double *x,
	double *y)
{
	/* Nothing until the host says. */
	*x = 0.0;
	*y = 0.0;

	/* The host's scroll. */
	if (window->host.scroll != NULL)
		window->host.scroll(window->host.context, x, y);
}

/* Rounds a measure to whole pixels, halves up. */
static double
geometry_round(
	double value)
{
	/* The nearest whole number. */
	return floor(value + 0.5);
}

/* Makes a DOMRect with a place and a size. */
static int
geometry_rect_create(
	struct bind_window *window,
	double x,
	double y,
	double width,
	double height,
	vm_value *value)
{
	struct geometry_rect *rect;
	struct vm_object *object;

	/* The state. */
	rect = vm_heap_alloc(window->realm->heap, &geometry_rect_type, sizeof(*rect));
	if (rect == NULL)
		return ENOMEM;
	rect->x = x;
	rect->y = y;
	rect->width = width;
	rect->height = height;

	/* The object with DOMRect's prototype. */
	object = vm_object_create(window->realm->heap, window->prototypes[BIND_DOM_RECT]);
	if (object == NULL)
		return ENOMEM;
	object->kind = VM_KIND_PLATFORM;
	object->internal = vm_value_cell(rect);

	/* Succeeded: the rectangle. */
	*value = vm_value_cell(object);
	return 0;
}

/* Finds the state of the DOMRect a value is, throwing a TypeError otherwise. */
static int
geometry_rect_of(
	struct vm_realm *realm,
	vm_value value,
	struct geometry_rect **rect)
{
	struct vm_object *object;
	struct vm_cell *cell;
	int is_object;
	int is_cell;
	int status;

	/* A platform object. */
	is_object = vm_value_is_object(value);
	if (!is_object) {
		status = bind_throw_illegal(realm);
		return status;
	}

	/* One the binding made. */
	object = (struct vm_object *)vm_value_as_cell(value);
	if (object->kind != VM_KIND_PLATFORM) {
		status = bind_throw_illegal(realm);
		return status;
	}

	/* Whose cell is a rectangle's state. */
	is_cell = vm_value_is_cell(object->internal);
	if (!is_cell) {
		status = bind_throw_illegal(realm);
		return status;
	}

	/* A cell of a rectangle's type. */
	cell = vm_value_as_cell(object->internal);
	if (cell->type != &geometry_rect_type) {
		status = bind_throw_illegal(realm);
		return status;
	}

	/* Succeeded: the state. */
	*rect = (struct geometry_rect *)cell;
	return 0;
}

/* Makes a DOMRect from new DOMRect(x, y, width, height), each 0 when not given. */
static int
geometry_rect_construct(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct bind_window *window;
	double numbers[4];
	unsigned index;
	int status;

	UNUSED_PARAMETER(this_value);

	/* Each of the four numbers. */
	window = bind_window_of(realm);
	for (index = 0; index < 4U; index++) {
		numbers[index] = 0.0;
		if (index >= count)
			continue;

		/* The argument as a number. */
		status = vm_to_number(realm, args[index], &numbers[index]);
		if (status != 0)
			return status;
	}

	/* Succeeded: the rectangle. */
	status = geometry_rect_create(window, numbers[0], numbers[1], numbers[2], numbers[3], result);
	if (status != 0)
		return status;
	return 0;
}

/* Reports the part of a DOMRect the getter knows. */
static int
geometry_rect_get(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct geometry_rect *rect;
	struct vm_function *callee;
	int part;
	int status;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The rectangle and the part. */
	status = geometry_rect_of(realm, this_value, &rect);
	if (status != 0)
		return status;
	callee = js_builtin_callee(realm);
	part = vm_value_as_int32(callee->data);

	/* Succeeded: the part's number. */
	*result = vm_value_number(geometry_rect_value(rect, part));
	return 0;
}

/* Sets the place or the size of a DOMRect (x, y, width and height). */
static int
geometry_rect_set(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct geometry_rect *rect;
	struct vm_function *callee;
	double number;
	int part;
	int status;

	/* The rectangle, the part and the number. */
	*result = VM_VALUE_UNDEFINED;
	status = geometry_rect_of(realm, this_value, &rect);
	if (status != 0)
		return status;
	callee = js_builtin_callee(realm);
	part = vm_value_as_int32(callee->data);
	status = vm_to_number(realm, js_argument(args, count, 0), &number);
	if (status != 0)
		return status;

	/* Chooses the field the part is. */
	switch (part) {
	case GEOMETRY_RECT_X:
		rect->x = number;
		break;
	case GEOMETRY_RECT_Y:
		rect->y = number;
		break;
	case GEOMETRY_RECT_WIDTH:
		rect->width = number;
		break;
	default:
		rect->height = number;
		break;
	}

	/* Succeeded: the rectangle is changed. */
	return 0;
}

/* Makes a plain object of a DOMRect's eight numbers (toJSON). */
static int
geometry_rect_to_json(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	const struct geometry_rect_member *member;
	struct geometry_rect *rect;
	struct vm_object *object;
	double number;
	int status;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The rectangle and a plain object. */
	status = geometry_rect_of(realm, this_value, &rect);
	if (status != 0)
		return status;
	object = vm_object_create(realm->heap, realm->object_prototype);
	if (object == NULL)
		return ENOMEM;

	/* Each member, as a data property. */
	for (member = geometry_rect_members; member->name != NULL; member++) {
		number = geometry_rect_value(rect, member->part);
		status = js_builtin_value(realm, object, member->name, vm_value_number(number), VM_PROPERTY_DEFAULT);
		if (status != 0)
			return status;
	}

	/* Succeeded: the object. */
	*result = vm_value_cell(object);
	return 0;
}

/* Reports one part of a rectangle: its place, its size or one of its edges. */
static double
geometry_rect_value(
	const struct geometry_rect *rect,
	int part)
{
	/* Chooses the part. */
	switch (part) {
	case GEOMETRY_RECT_X:
		return rect->x;
	case GEOMETRY_RECT_Y:
		return rect->y;
	case GEOMETRY_RECT_WIDTH:
		return rect->width;
	case GEOMETRY_RECT_HEIGHT:
		return rect->height;
	case GEOMETRY_RECT_TOP:
		return fmin(rect->y, rect->y + rect->height);
	case GEOMETRY_RECT_RIGHT:
		return fmax(rect->x, rect->x + rect->width);
	case GEOMETRY_RECT_BOTTOM:
		return fmax(rect->y, rect->y + rect->height);
	default:
		break;
	}

	/* The left edge. */
	return fmin(rect->x, rect->x + rect->width);
}
