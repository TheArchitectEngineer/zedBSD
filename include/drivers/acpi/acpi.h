/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The ACPI namespace and AML interpreter interface for drivers.
 *
 * The interpreter loads the DSDT and the SSDTs into one namespace.  A driver
 * finds a device node by path or by walking the namespace, evaluates its
 * control methods and reads the returned objects.  Every call that returns an
 * object hands the caller one reference, which drv_acpi_object_release()
 * gives back.  Errors are reported as zedBSD errno values.
 */

#ifndef KERN_DRIVERS_ACPI_ACPI_H
#define KERN_DRIVERS_ACPI_ACPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The object types.
 *
 * The numbers from 0 to 16 are the ones the ObjectType operator reports.
 * The rest are kinds of namespace entry that ObjectType folds into one of
 * those, or objects that only live inside the interpreter.
 */
enum drv_acpi_type {
	DRV_ACPI_TYPE_UNINITIALIZED = 0,
	DRV_ACPI_TYPE_INTEGER = 1,
	DRV_ACPI_TYPE_STRING = 2,
	DRV_ACPI_TYPE_BUFFER = 3,
	DRV_ACPI_TYPE_PACKAGE = 4,
	DRV_ACPI_TYPE_FIELD_UNIT = 5,
	DRV_ACPI_TYPE_DEVICE = 6,
	DRV_ACPI_TYPE_EVENT = 7,
	DRV_ACPI_TYPE_METHOD = 8,
	DRV_ACPI_TYPE_MUTEX = 9,
	DRV_ACPI_TYPE_REGION = 10,
	DRV_ACPI_TYPE_POWER_RESOURCE = 11,
	DRV_ACPI_TYPE_PROCESSOR = 12,
	DRV_ACPI_TYPE_THERMAL_ZONE = 13,
	DRV_ACPI_TYPE_BUFFER_FIELD = 14,
	DRV_ACPI_TYPE_DDB_HANDLE = 15,
	DRV_ACPI_TYPE_DEBUG = 16,
	DRV_ACPI_TYPE_SCOPE = 32,
	DRV_ACPI_TYPE_REFERENCE = 33,
	DRV_ACPI_TYPE_ALIAS = 34
};

/*
 * The address spaces of an operation region.
 */
enum drv_acpi_space {
	DRV_ACPI_SPACE_SYSTEM_MEMORY = 0,
	DRV_ACPI_SPACE_SYSTEM_IO = 1,
	DRV_ACPI_SPACE_PCI_CONFIG = 2,
	DRV_ACPI_SPACE_EMBEDDED_CONTROL = 3,
	DRV_ACPI_SPACE_SMBUS = 4,
	DRV_ACPI_SPACE_SYSTEM_CMOS = 5,
	DRV_ACPI_SPACE_PCI_BAR_TARGET = 6,
	DRV_ACPI_SPACE_IPMI = 7,
	DRV_ACPI_SPACE_GPIO = 8,
	DRV_ACPI_SPACE_GENERIC_SERIAL_BUS = 9,
	DRV_ACPI_SPACE_PCC = 10,
	DRV_ACPI_SPACE_PRM = 11,
	DRV_ACPI_SPACE_COUNT = 12
};

struct drv_acpi_node;
struct drv_acpi_object;

/*
 * A walk visitor.
 *
 * It returns 0 to continue into the node's children, a positive value to
 * skip them, and a negative value to stop the walk.
 */
typedef int (*drv_acpi_walk_visitor_t)(struct drv_acpi_node *node, unsigned depth, void *argument);

/*
 * An address space handler.
 *
 * address is the byte address inside the space (for PCI configuration
 * space, the offset in the function's space, with the function in pci), and
 * width is the access width in bits: 8, 16, 32 or 64.
 */
struct drv_acpi_region_access {
	uint64_t address;
	unsigned width;
	bool write;
	uint16_t pci_segment;
	uint8_t pci_bus;
	uint8_t pci_device;
	uint8_t pci_function;
};

typedef int (*drv_acpi_region_handler_t)(const struct drv_acpi_region_access *access, uint64_t *value, void *argument);

/*
 * A notification handler: Notify (node, value) reached the driver.
 */
typedef void (*drv_acpi_notify_handler_t)(struct drv_acpi_node *node, uint32_t value, void *argument);

int
drv_acpi_attach(void);

int
drv_acpi_initialize_namespace(void);

int
drv_acpi_load_table(
	const void *table,
	size_t length);

int
drv_acpi_initialize_objects(void);

void
drv_acpi_initialize_devices(void);

void
drv_acpi_reset(void);

int
drv_acpi_lookup(
	struct drv_acpi_node *scope,
	const char *path,
	struct drv_acpi_node **result);

struct drv_acpi_node *
drv_acpi_root(void);

int
drv_acpi_walk(
	struct drv_acpi_node *scope,
	drv_acpi_walk_visitor_t visitor,
	void *argument);

int
drv_acpi_node_path(
	const struct drv_acpi_node *node,
	char *buffer,
	size_t size);

enum drv_acpi_type
drv_acpi_node_type(
	const struct drv_acpi_node *node);

int
drv_acpi_evaluate(
	struct drv_acpi_node *scope,
	const char *path,
	struct drv_acpi_object **arguments,
	unsigned argument_count,
	struct drv_acpi_object **result);

int
drv_acpi_evaluate_integer(
	struct drv_acpi_node *scope,
	const char *path,
	uint64_t *value);

struct drv_acpi_object *
drv_acpi_object_integer_new(
	uint64_t value);

struct drv_acpi_object *
drv_acpi_object_string_new(
	const char *text);

struct drv_acpi_object *
drv_acpi_object_buffer_new(
	const void *bytes,
	size_t length);

void
drv_acpi_object_release(
	struct drv_acpi_object *object);

enum drv_acpi_type
drv_acpi_object_type(
	const struct drv_acpi_object *object);

uint64_t
drv_acpi_object_integer(
	const struct drv_acpi_object *object);

const char *
drv_acpi_object_string(
	const struct drv_acpi_object *object,
	size_t *length);

const uint8_t *
drv_acpi_object_buffer(
	const struct drv_acpi_object *object,
	size_t *length);

unsigned
drv_acpi_object_package_count(
	const struct drv_acpi_object *object);

struct drv_acpi_object *
drv_acpi_object_package_element(
	const struct drv_acpi_object *object,
	unsigned index);

struct drv_acpi_node *
drv_acpi_object_reference_node(
	const struct drv_acpi_object *object);

int
drv_acpi_region_install(
	enum drv_acpi_space space,
	drv_acpi_region_handler_t handler,
	void *argument);

int
drv_acpi_region_connect_all(void);

int
drv_acpi_notify_install(
	struct drv_acpi_node *node,
	drv_acpi_notify_handler_t handler,
	void *argument);

#endif
