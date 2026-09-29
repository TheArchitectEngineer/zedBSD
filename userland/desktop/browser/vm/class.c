/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Classes (ws074-p080): making a class's constructor and prototype from
 * its heritage, defining its methods and accessors, and reading a
 * property through super.
 *
 * A method's home object (the prototype, or the constructor for a static
 * one) is kept in its function's data: super.x reads x from the home
 * object's prototype with the method's this as the receiver.
 */

#include "vm/bytecode.h"
#include "vm/internal.h"

#include <errno.h>
#include <string.h>

/*
 * Sets up a class: its prototype object (whose prototype is the parent's
 * prototype, Object.prototype without a heritage, or null for extends
 * null), the constructor's prototype property and the prototype's
 * constructor, and the constructor's own prototype (the parent for
 * extends).  parent is the empty value without a heritage.
 */
int
vm_class_setup(
	struct vm_realm *realm,
	vm_value constructor,
	vm_value parent,
	vm_value *prototype)
{
	struct vm_function *function;
	struct vm_object *made;
	struct vm_object *proto_parent;
	struct vm_object *constructor_parent;
	vm_value parent_prototype;
	vm_value key;
	int is_constructor;
	int is_object;
	int status;

	/* Without a heritage: Object.prototype and Function.prototype. */
	proto_parent = realm->object_prototype;
	constructor_parent = realm->function_prototype;

	/* extends null: a prototype with no prototype, and an ordinary constructor. */
	if (parent == VM_VALUE_NULL)
		proto_parent = NULL;

	/* extends a constructor: its prototype property (an object or null) and itself. */
	if (parent != VM_VALUE_EMPTY && parent != VM_VALUE_NULL) {
		is_constructor = vm_value_is_constructor(parent);
		if (!is_constructor) {
			status = vm_throw_type_error(realm, "Class extends value is not a constructor or null");
			return status;
		}
		key = vm_key_from_ascii(realm->heap, "prototype");
		if (key == VM_VALUE_EMPTY)
			return ENOMEM;
		status = vm_get(realm, parent, key, &parent_prototype);
		if (status != 0)
			return status;
		is_object = vm_value_is_object(parent_prototype);
		if (!is_object && parent_prototype != VM_VALUE_NULL) {
			status = vm_throw_type_error(realm, "Class extends value does not have valid prototype property");
			return status;
		}

		/* The chains. */
		proto_parent = NULL;
		if (is_object)
			proto_parent = (struct vm_object *)vm_value_as_cell(parent_prototype);
		constructor_parent = (struct vm_object *)vm_value_as_cell(parent);
	}

	/* The prototype object, the constructor's home object too (for super in it). */
	made = vm_object_create(realm->heap, proto_parent);
	if (made == NULL)
		return ENOMEM;
	function = (struct vm_function *)vm_value_as_cell(constructor);
	function->object.prototype = constructor_parent;
	function->data = vm_value_cell(made);

	/* constructor.prototype, fixed. */
	key = vm_key_from_ascii(realm->heap, "prototype");
	if (key == VM_VALUE_EMPTY)
		return ENOMEM;
	status = vm_object_define(realm->heap, &function->object, key, vm_value_cell(made), 0);
	if (status != 0)
		return status;

	/* prototype.constructor, not enumerable. */
	key = vm_key_from_ascii(realm->heap, "constructor");
	if (key == VM_VALUE_EMPTY)
		return ENOMEM;
	status = vm_object_define(realm->heap, made, key, constructor, VM_PROPERTY_WRITABLE | VM_PROPERTY_CONFIGURABLE);
	if (status != 0)
		return status;

	/* Succeeded: the prototype object. */
	*prototype = vm_value_cell(made);
	return 0;
}

/*
 * Defines a class's method (kind 0), getter (1) or setter (2) on its home
 * object, not enumerable, and makes that object the function's home
 * object.
 */
int
vm_define_method(
	struct vm_realm *realm,
	vm_value home,
	vm_value key,
	vm_value function,
	uint32_t kind)
{
	struct vm_function *method;
	struct vm_object *object;
	struct vm_property property;
	struct vm_accessor *accessor;
	vm_value getter;
	vm_value setter;
	int found;
	int status;

	/* The function remembers where it lives (a bytecode method's data is its home object). */
	method = (struct vm_function *)vm_value_as_cell(function);
	if (method->code != NULL)
		method->data = home;

	/* A method: a writable, configurable data property. */
	object = (struct vm_object *)vm_value_as_cell(home);
	if (kind == 0U) {
		status = vm_object_define(realm->heap, object, key, function, VM_PROPERTY_WRITABLE | VM_PROPERTY_CONFIGURABLE);
		if (status != 0)
			return status;
		return 0;
	}

	/* An accessor's half joins the other half already there. */
	getter = VM_VALUE_UNDEFINED;
	setter = VM_VALUE_UNDEFINED;
	found = vm_object_get_own(object, key, &property);
	if (found && (property.attributes & VM_PROPERTY_ACCESSOR) != 0U) {
		accessor = (struct vm_accessor *)vm_value_as_cell(*property.value);
		getter = accessor->getter;
		setter = accessor->setter;
	}
	if (kind == 1U) {
		getter = function;
	} else {
		setter = function;
	}

	/* The accessor property, configurable and not enumerable. */
	accessor = vm_accessor_create(realm->heap, getter, setter);
	if (accessor == NULL)
		return ENOMEM;
	status = vm_object_define(realm->heap, object, key, vm_value_cell(accessor), VM_PROPERTY_ACCESSOR | VM_PROPERTY_CONFIGURABLE);
	if (status != 0)
		return status;

	/* Succeeded: the accessor is defined. */
	return 0;
}

/*
 * Reads super.key in a method whose home object is home: the property of
 * the home object's prototype, a getter called with the method's this.
 */
int
vm_get_super(
	struct vm_realm *realm,
	vm_value home,
	vm_value key,
	vm_value this_value,
	vm_value *result)
{
	struct vm_object *object;
	struct vm_property property;
	struct vm_accessor *accessor;
	int is_object;
	int found;
	int status;

	/* A home object is needed (a method's), and its prototype may be null. */
	*result = VM_VALUE_UNDEFINED;
	is_object = vm_value_is_object(home);
	if (!is_object) {
		status = vm_throw_error(realm, VM_ERROR_SYNTAX, "'super' keyword unexpected here");
		return status;
	}
	object = ((struct vm_object *)vm_value_as_cell(home))->prototype;
	if (object == NULL) {
		status = vm_throw_type_error(realm, "Cannot read properties of null");
		return status;
	}

	/* The property on the prototype's chain. */
	found = vm_object_find(object, key, &property);
	if (!found)
		return 0;

	/* A data property's value. */
	if ((property.attributes & VM_PROPERTY_ACCESSOR) == 0U) {
		*result = *property.value;
		return 0;
	}

	/* A getter, called with the method's this (no getter reads undefined). */
	accessor = (struct vm_accessor *)vm_value_as_cell(*property.value);
	if (accessor->getter == VM_VALUE_UNDEFINED)
		return 0;
	status = vm_call(realm, accessor->getter, this_value, NULL, 0, result);
	if (status != 0)
		return status;

	/* Succeeded: the getter's value. */
	return 0;
}
