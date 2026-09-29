/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The window's environment (ws074-p077): navigator, screen, performance
 * and location, the Image constructor, and the window's other plain
 * properties (devicePixelRatio, the outer size, top and the like) that
 * page scripts read before they do anything else.
 *
 * navigator, screen, performance and location are one object each per
 * window, made from the prototypes of their interfaces.  What never
 * changes (the navigator's names and flags, the screen's depth) is a
 * data property of the prototype, so an own-property test on the object
 * fails as it does in other browsers; what changes (the screen's size,
 * the clock, the location) is an accessor.  The location is read from the
 * host (the page's URL); setting it, which navigates, is not in this pass.
 * navigator.sendBeacon accepts a beacon without sending it.
 */

#include "bind/internal.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* The processor's name in navigator.platform, as the compiler targets it. */
#if defined(__x86_64__)
#define ENVIRONMENT_MACHINE	"x86_64"
#elif defined(__aarch64__)
#define ENVIRONMENT_MACHINE	"aarch64"
#elif defined(__i386__)
#define ENVIRONMENT_MACHINE	"i686"
#else
#define ENVIRONMENT_MACHINE	"unknown"
#endif

/* The colour depth of the screen, in bits per pixel (the surfaces are 32-bit, with 24 bits of colour). */
#define ENVIRONMENT_COLOR_DEPTH	24

/*
 * A string property of an interface's prototype that does not change for
 * the life of the program.
 */
struct environment_string {
	const char *name;
	const char *value;
};

/*
 * A number property of an interface's prototype that does not change for
 * the life of the program.
 */
struct environment_number {
	const char *name;
	double value;
};

/*
 * A Boolean property of an interface's prototype that does not change for
 * the life of the program.
 */
struct environment_flag {
	const char *name;
	int value;
};

/*
 * One accessor whose getter reads one part of something (the part is the
 * getter function's data): its name and the part.
 */
struct environment_part {
	const char *name;
	int part;
};

static int environment_install_navigator(struct bind_window *window);
static int environment_install_screen(struct bind_window *window);
static int environment_install_performance(struct bind_window *window);
static int environment_install_location(struct bind_window *window);
static int environment_install_image(struct bind_window *window);
static int environment_install_window(struct bind_window *window);
static int environment_instance(struct bind_window *window, int interface, const char *global, struct vm_object **object);
static int environment_strings(struct vm_realm *realm, struct vm_object *object, const struct environment_string *table);
static int environment_numbers(struct vm_realm *realm, struct vm_object *object, const struct environment_number *table);
static int environment_flags(struct vm_realm *realm, struct vm_object *object, const struct environment_flag *table);
static int environment_part_accessor(struct vm_realm *realm, struct vm_object *object, const char *name, vm_native getter, int part);
static int environment_empty_array(struct vm_realm *realm, vm_value *value);
static int environment_navigator_app_version(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int environment_navigator_user_agent(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int environment_navigator_languages(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int environment_navigator_list(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int environment_navigator_java_enabled(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int environment_navigator_send_beacon(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int environment_screen_size(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int environment_performance_now(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int environment_performance_time_origin(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int environment_performance_entries(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int environment_performance_nothing(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int environment_location_part(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int environment_location_to_string(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int environment_image_construct(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int environment_outer_size(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);

/*
 * The screen's sizes (the part is 0 for the width and 1 for the height).
 * The table is constant for the life of the program.
 */
static const struct environment_part environment_screen_sizes[] = {
	{ "width", 0 },
	{ "height", 1 },
	{ "availWidth", 0 },
	{ "availHeight", 1 },
	{ NULL, 0 }
};

/*
 * The parts of the location, each a BIND_LOCATION_* the host reads.  The
 * table is constant for the life of the program.
 */
static const struct environment_part environment_location_parts[] = {
	{ "href", BIND_LOCATION_HREF },
	{ "origin", BIND_LOCATION_ORIGIN },
	{ "protocol", BIND_LOCATION_PROTOCOL },
	{ "host", BIND_LOCATION_HOST },
	{ "hostname", BIND_LOCATION_HOSTNAME },
	{ "port", BIND_LOCATION_PORT },
	{ "pathname", BIND_LOCATION_PATHNAME },
	{ "search", BIND_LOCATION_SEARCH },
	{ "hash", BIND_LOCATION_HASH },
	{ NULL, 0 }
};

/*
 * The navigator's names that never change.  The table is constant for the
 * life of the program.
 */
static const struct environment_string environment_navigator_strings[] = {
	{ "appCodeName", "Mozilla" },
	{ "appName", "Netscape" },
	{ "platform", "Kei " ENVIRONMENT_MACHINE },
	{ "product", "Gecko" },
	{ "productSub", "20030107" },
	{ "vendor", "" },
	{ "vendorSub", "" },
	{ "language", "en-US" },
	{ NULL, NULL }
};

/*
 * The navigator's flags: cookies are kept, the network is taken to be
 * there, and nothing drives the browser.  The table is constant for the
 * life of the program.
 */
static const struct environment_flag environment_navigator_flags[] = {
	{ "cookieEnabled", 1 },
	{ "onLine", 1 },
	{ "webdriver", 0 },
	{ "pdfViewerEnabled", 0 },
	{ NULL, 0 }
};

/*
 * The navigator's numbers: one processor for the page's scripts, and no
 * touch points.  The table is constant for the life of the program.
 */
static const struct environment_number environment_navigator_numbers[] = {
	{ "hardwareConcurrency", 1.0 },
	{ "maxTouchPoints", 0.0 },
	{ NULL, 0.0 }
};

/*
 * The screen's numbers that never change.  The table is constant for the
 * life of the program.
 */
static const struct environment_number environment_screen_numbers[] = {
	{ "colorDepth", ENVIRONMENT_COLOR_DEPTH },
	{ "pixelDepth", ENVIRONMENT_COLOR_DEPTH },
	{ "availLeft", 0.0 },
	{ "availTop", 0.0 },
	{ NULL, 0.0 }
};

/*
 * The names of the moments of performance.timing, which are all the
 * moment the window was made in this pass (the loader does not report its
 * moments).  The table is constant for the life of the program.
 */
static const char *const environment_timing_names[] = {
	"navigationStart", "unloadEventStart", "unloadEventEnd", "redirectStart", "redirectEnd", "fetchStart",
	"domainLookupStart", "domainLookupEnd", "connectStart", "connectEnd", "secureConnectionStart", "requestStart",
	"responseStart", "responseEnd", "domLoading", "domInteractive", "domContentLoadedEventStart",
	"domContentLoadedEventEnd", "domComplete", "loadEventStart", "loadEventEnd", NULL
};

/*
 * The operations of Navigator.  The table is constant for the life of the
 * program.
 */
static const struct bind_operation environment_navigator_operations[] = {
	{ "javaEnabled", 0, environment_navigator_java_enabled },
	{ "sendBeacon", 1, environment_navigator_send_beacon },
	{ NULL, 0, NULL }
};

/*
 * The attributes of Navigator that depend on the host.  The table is
 * constant for the life of the program.
 */
static const struct bind_attribute environment_navigator_attributes[] = {
	{ "userAgent", environment_navigator_user_agent, NULL },
	{ "appVersion", environment_navigator_app_version, NULL },
	{ "languages", environment_navigator_languages, NULL },
	{ "plugins", environment_navigator_list, NULL },
	{ "mimeTypes", environment_navigator_list, NULL },
	{ NULL, NULL, NULL }
};

/*
 * The Navigator interface (the names and flags are added to its
 * prototype by environment_install_navigator).
 */
const struct bind_interface bind_navigator_interface = {
	"Navigator", BIND_NO_PARENT, 0, NULL, environment_navigator_attributes, environment_navigator_operations, NULL
};

/*
 * The Screen interface (its sizes and depths are added to its prototype
 * by environment_install_screen).
 */
const struct bind_interface bind_screen_interface = {
	"Screen", BIND_NO_PARENT, 0, NULL, NULL, NULL, NULL
};

/*
 * The attributes of Performance.  The table is constant for the life of
 * the program.
 */
static const struct bind_attribute environment_performance_attributes[] = {
	{ "timeOrigin", environment_performance_time_origin, NULL },
	{ NULL, NULL, NULL }
};

/*
 * The operations of Performance: the clock, the entries (there are none
 * in this pass) and the methods that record them (which record nothing).
 * The table is constant for the life of the program.
 */
static const struct bind_operation environment_performance_operations[] = {
	{ "now", 0, environment_performance_now },
	{ "getEntries", 0, environment_performance_entries },
	{ "getEntriesByType", 1, environment_performance_entries },
	{ "getEntriesByName", 1, environment_performance_entries },
	{ "mark", 1, environment_performance_nothing },
	{ "measure", 1, environment_performance_nothing },
	{ "clearMarks", 0, environment_performance_nothing },
	{ "clearMeasures", 0, environment_performance_nothing },
	{ "clearResourceTimings", 0, environment_performance_nothing },
	{ "setResourceTimingBufferSize", 1, environment_performance_nothing },
	{ NULL, 0, NULL }
};

/*
 * The Performance interface.
 */
const struct bind_interface bind_performance_interface = {
	"Performance", BIND_NO_PARENT, 0, NULL, environment_performance_attributes, environment_performance_operations, NULL
};

/*
 * The operations of Location (the parts are accessors added by
 * environment_install_location).  The table is constant for the life of
 * the program.
 */
static const struct bind_operation environment_location_operations[] = {
	{ "toString", 0, environment_location_to_string },
	{ NULL, 0, NULL }
};

/*
 * The Location interface.
 */
const struct bind_interface bind_location_interface = {
	"Location", BIND_NO_PARENT, 0, NULL, NULL, environment_location_operations, NULL
};

/*
 * Makes the window's environment: navigator, screen, performance,
 * location, the Image constructor and the window's plain properties.
 */
int
bind_environment_install(
	struct bind_window *window)
{
	int error;

	/* The moment the window was made, which performance.timeOrigin reports. */
	window->time_origin = bind_epoch_milliseconds();

	/* navigator. */
	error = environment_install_navigator(window);
	if (error != 0)
		return error;

	/* screen. */
	error = environment_install_screen(window);
	if (error != 0)
		return error;

	/* performance. */
	error = environment_install_performance(window);
	if (error != 0)
		return error;

	/* location, which the document reports too. */
	error = environment_install_location(window);
	if (error != 0)
		return error;

	/* Image. */
	error = environment_install_image(window);
	if (error != 0)
		return error;

	/* The window's own plain properties. */
	error = environment_install_window(window);
	if (error != 0)
		return error;

	/* Succeeded: the environment is on the global object. */
	return 0;
}

/*
 * Reads the real-time clock in milliseconds since 1970 (a whole number;
 * 0 when there is no clock).
 */
double
bind_epoch_milliseconds(void)
{
	struct timespec now;
	int status;

	/* The system's clock. */
	status = clock_gettime(CLOCK_REALTIME, &now);
	if (status != 0)
		return 0.0;

	/* Succeeded: the whole milliseconds. */
	return (double)now.tv_sec * 1000.0 + (double)(now.tv_nsec / 1000000L);
}

/*
 * Makes a string of one part of the document's location (a
 * BIND_LOCATION_*), as the host reads it from the page's URL; the empty
 * string when the host has no location.
 */
int
bind_location_part(
	struct bind_window *window,
	int part,
	vm_value *value)
{
	struct wb_buffer text;
	struct vm_string *string;
	int error;

	/* The host writes the part. */
	wb_buffer_init(&text);
	error = 0;
	if (window->host.location != NULL)
		error = window->host.location(window->host.context, part, &text);
	if (error != 0) {
		wb_buffer_release(&text);
		return error;
	}

	/* The string, from the UTF-8 text. */
	string = vm_string_from_utf8(window->realm->heap, wb_buffer_string(&text), text.length);
	wb_buffer_release(&text);
	if (string == NULL)
		return ENOMEM;

	/* Succeeded: the part's string. */
	*value = vm_value_cell(string);
	return 0;
}

/* Makes navigator with its names, flags and numbers. */
static int
environment_install_navigator(
	struct bind_window *window)
{
	struct vm_realm *realm;
	struct vm_object *prototype;
	struct vm_object *navigator;
	int error;

	/* The prototype carries what never changes. */
	realm = window->realm;
	prototype = window->prototypes[BIND_NAVIGATOR];
	error = environment_strings(realm, prototype, environment_navigator_strings);
	if (error != 0)
		return error;
	error = environment_flags(realm, prototype, environment_navigator_flags);
	if (error != 0)
		return error;
	error = environment_numbers(realm, prototype, environment_navigator_numbers);
	if (error != 0)
		return error;

	/* doNotTrack is null: the user has not said. */
	error = js_builtin_value(realm, prototype, "doNotTrack", VM_VALUE_NULL, VM_PROPERTY_ENUMERABLE | VM_PROPERTY_CONFIGURABLE);
	if (error != 0)
		return error;

	/* The window's navigator. */
	error = environment_instance(window, BIND_NAVIGATOR, "navigator", &navigator);
	if (error != 0)
		return error;

	/* Succeeded: navigator is on the global object. */
	return 0;
}

/* Makes screen with its sizes and depths. */
static int
environment_install_screen(
	struct bind_window *window)
{
	const struct environment_part *size;
	struct vm_realm *realm;
	struct vm_object *prototype;
	struct vm_object *screen;
	int error;

	/* The depths never change. */
	realm = window->realm;
	prototype = window->prototypes[BIND_SCREEN];
	error = environment_numbers(realm, prototype, environment_screen_numbers);
	if (error != 0)
		return error;

	/* The sizes follow the window's viewport, which is the whole screen the page can have. */
	for (size = environment_screen_sizes; size->name != NULL; size++) {
		error = environment_part_accessor(realm, prototype, size->name, environment_screen_size, size->part);
		if (error != 0)
			return error;
	}

	/* The window's screen. */
	error = environment_instance(window, BIND_SCREEN, "screen", &screen);
	if (error != 0)
		return error;

	/* Succeeded: screen is on the global object. */
	return 0;
}

/* Makes performance with its timing and navigation records. */
static int
environment_install_performance(
	struct bind_window *window)
{
	struct vm_realm *realm;
	struct vm_object *performance;
	struct vm_object *timing;
	struct vm_object *navigation;
	vm_value origin;
	size_t index;
	int error;

	/* The window's performance. */
	realm = window->realm;
	error = environment_instance(window, BIND_PERFORMANCE, "performance", &performance);
	if (error != 0)
		return error;

	/* timing: every moment is the time origin in this pass. */
	timing = vm_object_create(realm->heap, realm->object_prototype);
	if (timing == NULL)
		return ENOMEM;
	origin = vm_value_number(window->time_origin);
	for (index = 0; environment_timing_names[index] != NULL; index++) {
		error = js_builtin_value(realm, timing, environment_timing_names[index], origin, VM_PROPERTY_DEFAULT);
		if (error != 0)
			return error;
	}
	error = js_builtin_value(realm, performance, "timing", vm_value_cell(timing), VM_PROPERTY_DEFAULT);
	if (error != 0)
		return error;

	/* navigation: an ordinary navigation (type 0) without redirects. */
	navigation = vm_object_create(realm->heap, realm->object_prototype);
	if (navigation == NULL)
		return ENOMEM;
	error = js_builtin_value(realm, navigation, "type", vm_value_int32(0), VM_PROPERTY_DEFAULT);
	if (error != 0)
		return error;
	error = js_builtin_value(realm, navigation, "redirectCount", vm_value_int32(0), VM_PROPERTY_DEFAULT);
	if (error != 0)
		return error;
	error = js_builtin_value(realm, performance, "navigation", vm_value_cell(navigation), VM_PROPERTY_DEFAULT);
	if (error != 0)
		return error;

	/* Succeeded: performance is on the global object. */
	return 0;
}

/* Makes location, whose parts are read from the host. */
static int
environment_install_location(
	struct bind_window *window)
{
	const struct environment_part *part;
	struct vm_realm *realm;
	struct vm_object *prototype;
	int error;

	/* Each part is an accessor that asks the host. */
	realm = window->realm;
	prototype = window->prototypes[BIND_LOCATION];
	for (part = environment_location_parts; part->name != NULL; part++) {
		error = environment_part_accessor(realm, prototype, part->name, environment_location_part, part->part);
		if (error != 0)
			return error;
	}

	/* The window's location, which document.location reports as well. */
	error = environment_instance(window, BIND_LOCATION, "location", &window->location);
	if (error != 0)
		return error;

	/* Succeeded: location is on the global object. */
	return 0;
}

/* Makes the Image constructor, whose objects are img elements. */
static int
environment_install_image(
	struct bind_window *window)
{
	struct vm_realm *realm;
	struct vm_function *image;
	vm_value prototype;
	int error;

	/* The constructor, which throws when called without new. */
	realm = window->realm;
	error = js_builtin_function(realm, "Image", 0, bind_illegal_constructor, environment_image_construct, &image);
	if (error != 0)
		return error;

	/* Its prototype is HTMLImageElement's (which keeps its own constructor). */
	prototype = vm_value_cell(window->prototypes[BIND_HTML_IMAGE_ELEMENT]);
	error = js_builtin_value(realm, &image->object, "prototype", prototype, 0);
	if (error != 0)
		return error;

	/* The global. */
	error = js_builtin_value(realm, realm->global, "Image", vm_value_cell(image), JS_BUILTIN_METHOD);
	if (error != 0)
		return error;

	/* Succeeded: Image is on the global object. */
	return 0;
}

/*
 * Defines the window's plain properties: the pixel ratio, the outer size
 * and position, and the window's relatives (a page without frames is its
 * own top and parent).
 */
static int
environment_install_window(
	struct bind_window *window)
{
	struct vm_realm *realm;
	struct vm_object *global;
	vm_value self;
	int error;

	/* One device pixel is one CSS pixel. */
	realm = window->realm;
	global = realm->global;
	error = js_builtin_value(realm, global, "devicePixelRatio", vm_value_int32(1), VM_PROPERTY_DEFAULT);
	if (error != 0)
		return error;

	/* The outer size is the viewport's (the window's frame is not counted). */
	error = environment_part_accessor(realm, global, "outerWidth", environment_outer_size, 0);
	if (error != 0)
		return error;
	error = environment_part_accessor(realm, global, "outerHeight", environment_outer_size, 1);
	if (error != 0)
		return error;

	/* The window's place on the screen is its corner. */
	error = js_builtin_value(realm, global, "screenX", vm_value_int32(0), VM_PROPERTY_DEFAULT);
	if (error == 0)
		error = js_builtin_value(realm, global, "screenY", vm_value_int32(0), VM_PROPERTY_DEFAULT);
	if (error == 0)
		error = js_builtin_value(realm, global, "screenLeft", vm_value_int32(0), VM_PROPERTY_DEFAULT);
	if (error == 0)
		error = js_builtin_value(realm, global, "screenTop", vm_value_int32(0), VM_PROPERTY_DEFAULT);
	if (error != 0)
		return error;

	/* A page without frames is its own top, parent and frames, and nothing opened it. */
	self = vm_value_cell(global);
	error = js_builtin_value(realm, global, "top", self, VM_PROPERTY_ENUMERABLE);
	if (error == 0)
		error = js_builtin_value(realm, global, "parent", self, VM_PROPERTY_DEFAULT);
	if (error == 0)
		error = js_builtin_value(realm, global, "frames", self, VM_PROPERTY_DEFAULT);
	if (error == 0)
		error = js_builtin_value(realm, global, "opener", VM_VALUE_NULL, VM_PROPERTY_DEFAULT);
	if (error == 0)
		error = js_builtin_value(realm, global, "length", vm_value_int32(0), VM_PROPERTY_DEFAULT);
	if (error != 0)
		return error;

	/* The window has no name and is open. */
	error = bind_string(realm, "", &self);
	if (error == 0)
		error = js_builtin_value(realm, global, "name", self, VM_PROPERTY_DEFAULT);
	if (error == 0)
		error = js_builtin_value(realm, global, "closed", VM_VALUE_FALSE, VM_PROPERTY_DEFAULT);
	if (error != 0)
		return error;

	/* Succeeded: the window's properties are defined. */
	return 0;
}

/*
 * Makes the window's object of an interface (from its prototype) and puts
 * it on the global object under a name that cannot be replaced.
 */
static int
environment_instance(
	struct bind_window *window,
	int interface,
	const char *global,
	struct vm_object **object)
{
	struct vm_realm *realm;
	int error;

	/* The object. */
	realm = window->realm;
	*object = vm_object_create(realm->heap, window->prototypes[interface]);
	if (*object == NULL)
		return ENOMEM;

	/* The global property, enumerable and fixed as the window's own objects are. */
	error = js_builtin_value(realm, realm->global, global, vm_value_cell(*object), VM_PROPERTY_ENUMERABLE | VM_PROPERTY_CONFIGURABLE);
	if (error != 0)
		return error;

	/* Succeeded: the object is on the global object. */
	return 0;
}

/* Defines each string of a table on an object. */
static int
environment_strings(
	struct vm_realm *realm,
	struct vm_object *object,
	const struct environment_string *table)
{
	vm_value value;
	int error;

	/* Each entry, a string that cannot be assigned. */
	for (; table->name != NULL; table++) {
		error = bind_string(realm, table->value, &value);
		if (error != 0)
			return error;
		error = js_builtin_value(realm, object, table->name, value, VM_PROPERTY_ENUMERABLE | VM_PROPERTY_CONFIGURABLE);
		if (error != 0)
			return error;
	}

	/* Succeeded: the strings are defined. */
	return 0;
}

/* Defines each number of a table on an object. */
static int
environment_numbers(
	struct vm_realm *realm,
	struct vm_object *object,
	const struct environment_number *table)
{
	int error;

	/* Each entry, a number that cannot be assigned. */
	for (; table->name != NULL; table++) {
		error = js_builtin_value(realm, object, table->name, vm_value_number(table->value), VM_PROPERTY_ENUMERABLE | VM_PROPERTY_CONFIGURABLE);
		if (error != 0)
			return error;
	}

	/* Succeeded: the numbers are defined. */
	return 0;
}

/* Defines each flag of a table on an object. */
static int
environment_flags(
	struct vm_realm *realm,
	struct vm_object *object,
	const struct environment_flag *table)
{
	vm_value value;
	int error;

	/* Each entry, a Boolean that cannot be assigned. */
	for (; table->name != NULL; table++) {
		value = VM_VALUE_FALSE;
		if (table->value)
			value = VM_VALUE_TRUE;
		error = js_builtin_value(realm, object, table->name, value, VM_PROPERTY_ENUMERABLE | VM_PROPERTY_CONFIGURABLE);
		if (error != 0)
			return error;
	}

	/* Succeeded: the flags are defined. */
	return 0;
}

/*
 * Defines a read-only accessor whose getter reads one part of something;
 * the getter finds the part in its function's data.
 */
static int
environment_part_accessor(
	struct vm_realm *realm,
	struct vm_object *object,
	const char *name,
	vm_native getter,
	int part)
{
	struct vm_function *function;
	struct vm_accessor *accessor;
	char getter_name[80];
	int error;

	/* The getter, named "get NAME", which knows its part. */
	snprintf(getter_name, sizeof(getter_name), "get %s", name);
	error = js_builtin_function(realm, getter_name, 0, getter, NULL, &function);
	if (error != 0)
		return error;
	function->data = vm_value_int32(part);

	/* The accessor, without a setter. */
	accessor = vm_accessor_create(realm->heap, vm_value_cell(function), VM_VALUE_UNDEFINED);
	if (accessor == NULL)
		return ENOMEM;
	error = js_builtin_value(realm, object, name, vm_value_cell(accessor), VM_PROPERTY_ACCESSOR | VM_PROPERTY_ENUMERABLE | VM_PROPERTY_CONFIGURABLE);
	if (error != 0)
		return error;

	/* Succeeded: the accessor is defined. */
	return 0;
}

/* Makes a new empty array (a list with nothing in it). */
static int
environment_empty_array(
	struct vm_realm *realm,
	vm_value *value)
{
	struct vm_object *array;
	int error;

	/* The array of the realm. */
	error = bind_array_create(realm, &array);
	if (error != 0)
		return error;

	/* Succeeded: the array. */
	*value = vm_value_cell(array);
	return 0;
}

/* Reports navigator.userAgent: the User-Agent the host sends. */
static int
environment_navigator_user_agent(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct bind_window *window;
	const char *agent;
	int error;

	UNUSED_PARAMETER(this_value);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* A host that names no agent leaves it empty. */
	window = bind_window_of(realm);
	agent = window->host.user_agent;
	if (agent == NULL)
		agent = "";

	/* The string. */
	error = bind_string(realm, agent, result);
	if (error != 0)
		return error;

	/* Succeeded: the agent is reported. */
	return 0;
}

/* Reports navigator.appVersion: the User-Agent after its first slash. */
static int
environment_navigator_app_version(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct bind_window *window;
	const char *agent;
	const char *slash;
	int error;

	UNUSED_PARAMETER(this_value);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The agent, from after the product's name. */
	window = bind_window_of(realm);
	agent = window->host.user_agent;
	if (agent == NULL)
		agent = "";
	slash = strchr(agent, '/');
	if (slash != NULL)
		agent = slash + 1;

	/* The string. */
	error = bind_string(realm, agent, result);
	if (error != 0)
		return error;

	/* Succeeded: the version is reported. */
	return 0;
}

/* Reports navigator.languages: the one language, in a new array. */
static int
environment_navigator_languages(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	vm_value language;
	int error;

	UNUSED_PARAMETER(this_value);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The language navigator.language names. */
	error = bind_string(realm, "en-US", &language);
	if (error != 0)
		return error;

	/* The array of it. */
	error = js_builtin_array(realm, &language, 1, result);
	if (error != 0)
		return error;

	/* Succeeded: the languages are reported. */
	return 0;
}

/* Reports navigator.plugins and navigator.mimeTypes: there are none. */
static int
environment_navigator_list(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	int error;

	UNUSED_PARAMETER(this_value);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* An empty list. */
	error = environment_empty_array(realm, result);
	if (error != 0)
		return error;

	/* Succeeded: the list is reported. */
	return 0;
}

/* Reports navigator.javaEnabled(): there is no Java. */
static int
environment_navigator_java_enabled(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	UNUSED_PARAMETER(realm);
	UNUSED_PARAMETER(this_value);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* Succeeded: false. */
	*result = VM_VALUE_FALSE;
	return 0;
}

/*
 * Takes navigator.sendBeacon(url, data): the beacon is accepted (true)
 * and not sent, since the page's measurements are for the site alone.
 */
static int
environment_navigator_send_beacon(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct vm_string *url;
	int error;

	UNUSED_PARAMETER(this_value);

	/* The URL is converted as the method's argument is, which may throw. */
	error = bind_to_string(realm, js_argument(args, count, 0), &url);
	if (error != 0)
		return error;

	/* Succeeded: the beacon is taken. */
	*result = VM_VALUE_TRUE;
	return 0;
}

/* Reports one of the screen's sizes (the function's data: 0 for a width, 1 for a height). */
static int
environment_screen_size(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct bind_window *window;
	struct vm_function *callee;
	int part;

	UNUSED_PARAMETER(this_value);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* Which size. */
	callee = js_builtin_callee(realm);
	part = vm_value_as_int32(callee->data);
	window = bind_window_of(realm);

	/* The height. */
	if (part == 1) {
		*result = vm_value_int32(window->viewport_height);
		return 0;
	}

	/* Succeeded: the width. */
	*result = vm_value_int32(window->viewport_width);
	return 0;
}

/* Reports performance.now(): the page's clock in milliseconds. */
static int
environment_performance_now(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct bind_window *window;

	UNUSED_PARAMETER(this_value);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The time the page gave the window. */
	window = bind_window_of(realm);
	*result = vm_value_number(window->now);

	/* Succeeded: the time is reported. */
	return 0;
}

/* Reports performance.timeOrigin: when the window was made, in milliseconds since 1970. */
static int
environment_performance_time_origin(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct bind_window *window;

	UNUSED_PARAMETER(this_value);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The moment kept when the window was made. */
	window = bind_window_of(realm);
	*result = vm_value_number(window->time_origin);

	/* Succeeded: the origin is reported. */
	return 0;
}

/* Reports the performance entries asked for: none are recorded in this pass. */
static int
environment_performance_entries(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	int error;

	UNUSED_PARAMETER(this_value);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* An empty list. */
	error = environment_empty_array(realm, result);
	if (error != 0)
		return error;

	/* Succeeded: the list is reported. */
	return 0;
}

/* Takes a performance method that records something: nothing is recorded in this pass. */
static int
environment_performance_nothing(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	UNUSED_PARAMETER(realm);
	UNUSED_PARAMETER(this_value);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* Succeeded: undefined. */
	*result = VM_VALUE_UNDEFINED;
	return 0;
}

/* Reports one part of the location (the function's data is a BIND_LOCATION_*). */
static int
environment_location_part(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct vm_function *callee;
	int part;
	int error;

	UNUSED_PARAMETER(this_value);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* Which part. */
	callee = js_builtin_callee(realm);
	part = vm_value_as_int32(callee->data);

	/* The part's text from the host. */
	error = bind_location_part(bind_window_of(realm), part, result);
	if (error != 0)
		return error;

	/* Succeeded: the part is reported. */
	return 0;
}

/* Reports location.toString(): the whole URL. */
static int
environment_location_to_string(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	int error;

	UNUSED_PARAMETER(this_value);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The href. */
	error = bind_location_part(bind_window_of(realm), BIND_LOCATION_HREF, result);
	if (error != 0)
		return error;

	/* Succeeded: the URL is reported. */
	return 0;
}

/* Makes new Image(width, height): an img element with the sizes given as its attributes. */
static int
environment_image_construct(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct bind_window *window;
	struct dom_element *element;
	struct vm_string *name;
	struct vm_string *value;
	int status;

	UNUSED_PARAMETER(this_value);

	/* The element, in the HTML namespace of the window's document. */
	window = bind_window_of(realm);
	name = vm_atom_from_ascii(realm->heap, "img");
	if (name == NULL)
		return ENOMEM;
	element = dom_element_create(window->document, DOM_NS_HTML, name, NULL);
	if (element == NULL)
		return ENOMEM;

	/* The width, when given. */
	if (count > 0) {
		status = bind_to_string(realm, args[0], &value);
		if (status != 0)
			return status;
		name = vm_atom_from_ascii(realm->heap, "width");
		if (name == NULL)
			return ENOMEM;
		status = dom_element_set_attribute(element, name, value);
		if (status != 0)
			return status;
	}

	/* The height, when given. */
	if (count > 1) {
		status = bind_to_string(realm, args[1], &value);
		if (status != 0)
			return status;
		name = vm_atom_from_ascii(realm->heap, "height");
		if (name == NULL)
			return ENOMEM;
		status = dom_element_set_attribute(element, name, value);
		if (status != 0)
			return status;
	}

	/* Its object. */
	status = bind_wrap(window, &element->node, result);
	if (status != 0)
		return status;

	/* Succeeded: the image element is made. */
	return 0;
}

/* Reports outerWidth or outerHeight (the function's data: 0 for the width, 1 for the height). */
static int
environment_outer_size(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	int error;

	/* The window's outer size is the screen's in this pass. */
	error = environment_screen_size(realm, this_value, args, count, result);
	if (error != 0)
		return error;

	/* Succeeded: the size is reported. */
	return 0;
}
