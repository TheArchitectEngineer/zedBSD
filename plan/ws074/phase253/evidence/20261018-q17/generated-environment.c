static int bind_environment_attribute_mutation();
static int environment_observer_observe();
static int environment_observer_disconnect();
static int environment_mutation_clear();
static int environment_mutation_options();
static int environment_mutation_boolean();
static int environment_mutation_filter();
static int environment_attribute_record();

int
bind_environment_attribute_mutation(
	struct dom_document *document,
	struct dom_element *element,
	struct vm_string *name,
	struct vm_string *old_value)
{
	struct bind_window *window;
	struct vm_heap *heap;
	struct environment_mutation_observer *observer;
	struct vm_object *filter;
	struct vm_string *wanted;
	struct dom_node *target;
	struct dom_node *ancestor;
	struct vm_cell *roots[3];
	size_t position;
	unsigned index;
	unsigned registered;
	int matched;
	int allowed;
	int same;
	int status;

	/* Detached or standalone Documents have no active native callback owner. */
	window = document->view;
	if (window == NULL || window->detached)
		return 0;

	/* The DOM commit lent these retired strings without any intervening VM allocation. */
	heap = window->realm->heap;
	roots[0] = &element->node.cell;
	roots[1] = &name->cell;
	roots[2] = NULL;
	if (old_value != NULL)
		roots[2] = &old_value->cell;
	registered = 0;
	status = 0;
	for (index = 0; index < 3U; index++) {
		status = vm_heap_add_root(heap, &roots[index]);
		if (status != 0)
			break;
		registered++;
	}

	/* A single native mutation produces independent records in normal registration order. */
	if (status == 0) {
		for (observer = window->mutation_observers;
		     observer != NULL;
		     observer = observer->next) {
			/* Disabled kinds and disconnected targets cannot observe this ordinary attribute. */
			if (!observer->attributes || observer->target == NULL)
				continue;

			/* Match the current inclusive ancestors without crossing a shadow root boundary. */
			target = bind_node_of(vm_value_cell(observer->target));
			matched = 0;
			for (ancestor = &element->node;
			     ancestor != NULL;
			     ancestor = ancestor->parent) {
				/* The registered target is either this element or a permitted subtree ancestor. */
				if (ancestor == target) {
					matched = 1;
					break;
				}

				/* A non-subtree registration never consults the element's parents. */
				if (!observer->subtree)
					break;
			}

			/* A registration in a different branch has no matching record. */
			if (!matched)
				continue;

			/* A copied filter uses exact names; an empty list intentionally observes none. */
			if (observer->attribute_filter != NULL) {
				filter = (struct vm_object *)observer->attribute_filter;
				allowed = 0;
				for (position = 0; position < filter->length; position++) {
					wanted = (struct vm_string *)vm_value_as_cell(filter->elements[position]);
					same = vm_string_equal(wanted, name);
					if (same) {
						allowed = 1;
						break;
					}
				}

				/* Excluded attribute names never allocate or enqueue a record. */
				if (!allowed)
					continue;
			}

			/* Only a complete payload enters the already owned microtask record array. */
			status = environment_attribute_record(window, observer, element, name, old_value);
			if (status != 0)
				break;
		}
	}

	/* Borrowed retired strings leave temporary ownership only after every observer attempt. */
	while (registered != 0) {
		registered--;
		vm_heap_remove_root(heap, &roots[registered]);
	}

	/* An allocation failure retains its precise native status after the committed mutation. */
	if (status != 0)
		return status;

	/* Succeeded: all matching ordinary attribute registrations own complete snapshots. */
	return 0;
}

static int
environment_observer_observe(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct environment_mutation_observer *observer;
	struct environment_mutation_settings settings;
	struct bind_window *window;
	struct dom_node *node;
	struct vm_function *callee;
	struct vm_object *data;
	struct vm_cell *roots[4];
	vm_value entry;
	vm_value entries;
	vm_value callback;
	vm_value options;
	vm_value target;
	unsigned index;
	unsigned registered;
	int is_cell;
	int type;
	int status;

	/* Constructor-owned data distinguishes mutation registration from layout observation. */
	*result = VM_VALUE_UNDEFINED;
	callee = js_builtin_callee(realm);
	data = (struct vm_object *)vm_value_as_cell(callee->data);
	callback = data->elements[0];
	type = vm_value_as_int32(data->elements[1]);
	window = bind_window_of(realm);
	if (type == 0) {
		/* Reject a borrowed receiver before running any option getter. */
		observer = environment_mutation_find(window, this_value);
		if (observer == NULL) {
			status = bind_throw_illegal(realm);
			if (status != VM_THROWN)
				return status;

			/* Succeeded: the invalid receiver is refused by its native exception. */
			return VM_THROWN;
		}

		/* Attribute and child-list registration both require an actual Node. */
		target = js_argument(args, count, 0);
		node = bind_node_of(target);
		if (node == NULL) {
			status = vm_throw_type_error(realm, "The observation target is not a Node.");
			if (status != VM_THROWN)
				return status;

			/* Succeeded: a non-Node target is refused. */
			return VM_THROWN;
		}

		/* Getter/iterator calls retain the receiver, target, dictionary and unpublished filter. */
		options = js_argument(args, count, 1);
		memset(&settings, 0, sizeof(settings));
		memset(roots, 0, sizeof(roots));
		roots[0] = vm_value_as_cell(target);
		is_cell = vm_value_is_cell(options);
		if (is_cell)
			roots[1] = vm_value_as_cell(options);
		roots[3] = observer->observer;
		registered = 0;
		status = 0;
		for (index = 0; index < 4U; index++) {
			status = vm_heap_add_root(realm->heap, &roots[index]);
			if (status != 0)
				break;
			registered++;
		}

		/* No invalid conversion can discard an earlier registration or its pending records. */
		if (status == 0) {
			do {
				status = environment_mutation_options(realm, options, &settings, &roots[2]);
				if (status != 0)
					break;

				/* This native implementation updates its one registration without deleting queued records. */
				environment_mutation_clear(window, observer, 0);
				observer->target = roots[0];
				status = environment_root(window, &observer->target);
				if (status != 0) {
					observer->target = NULL;
					break;
				}

				/* Only a completely copied filter enters persistent or managed ownership. */
				observer->attribute_filter = settings.attribute_filter;
				if (observer->attribute_filter != NULL) {
					status = environment_root(window, &observer->attribute_filter);
					if (status != 0) {
						observer->attribute_filter = NULL;
						environment_mutation_clear(window, observer, 0);
						break;
					}
				}

				/* All flags become visible together after conversion and ownership succeed. */
				observer->child_list = settings.child_list;
				observer->subtree = settings.subtree;
				observer->attributes = settings.attributes;
				observer->attribute_old_value = settings.attribute_old_value;
			} while (0);
		}

		/* Every temporary native slot is retired before this C invocation returns. */
		while (registered != 0) {
			registered--;
			vm_heap_remove_root(realm->heap, &roots[registered]);
		}

		/* Allocation, conversion or property failures keep their original status. */
		if (status != 0)
			return status;

		/* Succeeded: the requested ordinary mutation flags and copied filter are registered. */
		return 0;
	}

	/* Layout observations retain their existing initial-entry delivery path. */
	target = js_argument(args, count, 0);
	status = environment_observer_entry(window, target, type, &entry);
	if (status != 0)
		return status;

	/* The initial entry is the one argument of the ordinary queued layout callback. */
	status = js_builtin_array(realm, &entry, 1, &entries);
	if (status != 0)
		return status;

	/* The existing callback remains asynchronous rather than entering the observer now. */
	status = vm_enqueue_job(realm, callback, entries);
	if (status != 0)
		return status;

	/* Succeeded: the initial layout-observer callback is queued. */
	return 0;
}

static int
environment_observer_disconnect(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct environment_mutation_observer *observer;
	struct bind_window *window;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* Layout observers retain their existing no-op disconnect; mutation observers release registrations. */
	*result = VM_VALUE_UNDEFINED;
	window = bind_window_of(realm);
	observer = environment_mutation_find(window, this_value);
	if (observer != NULL)
		environment_mutation_clear(window, observer, 1);

	/* Succeeded: any mutation target, filter and undelivered records are disconnected. */
	return 0;
}

static void
environment_mutation_clear(
	struct bind_window *window,
	struct environment_mutation_observer *observer,
	int discard_records)
{
	/* The old target is no longer an active observation edge. */
	if (observer->target != NULL) {
		vm_heap_remove_root(window->realm->heap, &observer->target);
		observer->target = NULL;
	}

	/* A copied name filter has the same owner policy as the registered target. */
	if (observer->attribute_filter != NULL) {
		vm_heap_remove_root(window->realm->heap, &observer->attribute_filter);
		observer->attribute_filter = NULL;
	}

	/* Disconnect discards records; updating one registration preserves already queued snapshots. */
	if (discard_records && observer->records != NULL) {
		vm_heap_remove_root(window->realm->heap, &observer->records);
		observer->records = NULL;
	}

	/* A later observe call supplies a complete new set of flags. */
	observer->child_list = 0;
	observer->subtree = 0;
	observer->attributes = 0;
	observer->attribute_old_value = 0;
}

static int
environment_mutation_options(
	struct vm_realm *realm,
	vm_value options,
	struct environment_mutation_settings *settings,
	struct vm_cell **filter)
{
	vm_value value;
	int filter_present;
	int old_present;
	int attributes_present;
	int character_present;
	int character_old_present;
	int character_data;
	int character_old_value;
	int present;
	int status;

	/* Dictionary members are read in WebIDL lexicographic order, treating undefined as absent. */
	memset(settings, 0, sizeof(*settings));
	status = bind_get_option(realm, options, "attributeFilter", &filter_present, &value);
	if (status != 0)
		return status;

	/* Sequence conversion happens before any later dictionary getter can run. */
	if (filter_present) {
		status = environment_mutation_filter(realm, value, filter);
		if (status != 0)
			return status;
	}

	/* A present old-value option also affects the default attributes flag. */
	status = environment_mutation_boolean(realm, options, "attributeOldValue", &old_present, &settings->attribute_old_value);
	if (status != 0)
		return status;

	/* An explicit attributes value must not be confused with its omitted default. */
	status = environment_mutation_boolean(realm, options, "attributes", &attributes_present, &settings->attributes);
	if (status != 0)
		return status;

	/* Character data remains a recognized but separately unimplemented notification kind. */
	status = environment_mutation_boolean(realm, options, "characterData", &character_present, &character_data);
	if (status != 0)
		return status;

	/* Its dictionary validation follows the same optional-boolean default rule. */
	status = environment_mutation_boolean(realm, options, "characterDataOldValue", &character_old_present, &character_old_value);
	if (status != 0)
		return status;

	/* Existing child-list registration keeps its ordinary default and scope. */
	status = environment_mutation_boolean(realm, options, "childList", &present, &settings->child_list);
	if (status != 0)
		return status;

	/* Subtree applies independently to the enabled ordinary mutation kinds. */
	status = environment_mutation_boolean(realm, options, "subtree", &present, &settings->subtree);
	if (status != 0)
		return status;

	/* Presence, including false oldValue and an empty filter, enables an omitted attributes flag. */
	if (!attributes_present && (old_present || filter_present))
		settings->attributes = 1;

	/* Character-data default inference validates options without claiming record support. */
	if (!character_present && character_old_present)
		character_data = 1;

	/* A dictionary with no enabled mutation kind cannot establish an observation. */
	if (!settings->child_list && !settings->attributes && !character_data) {
		status = vm_throw_type_error(realm, "MutationObserver has no enabled mutation type.");
		if (status != VM_THROWN)
			return status;

		/* Succeeded: the empty observation request is refused. */
		return VM_THROWN;
	}

	/* Explicitly disabling attributes is incompatible with requesting old attribute values. */
	if (settings->attribute_old_value && !settings->attributes) {
		status = vm_throw_type_error(realm, "attributeOldValue requires attributes.");
		if (status != VM_THROWN)
			return status;

		/* Succeeded: the contradictory old-value request is refused. */
		return VM_THROWN;
	}

	/* A present filter likewise requires attribute observation, even when it is empty. */
	if (filter_present && !settings->attributes) {
		status = vm_throw_type_error(realm, "attributeFilter requires attributes.");
		if (status != VM_THROWN)
			return status;

		/* Succeeded: the contradictory filter request is refused. */
		return VM_THROWN;
	}

	/* The recognized character-data options cannot contradict their own enabled flag. */
	if (character_old_value && !character_data) {
		status = vm_throw_type_error(realm, "characterDataOldValue requires characterData.");
		if (status != VM_THROWN)
			return status;

		/* Succeeded: the contradictory character-data dictionary is refused. */
		return VM_THROWN;
	}

	/* Succeeded: every flag is valid and the caller still roots the copied sequence. */
	settings->attribute_filter = *filter;
	return 0;
}

static int
environment_mutation_boolean(
	struct vm_realm *realm,
	vm_value options,
	const char *name,
	int *present,
	int *enabled)
{
	vm_value value;
	int status;

	/* Dictionary lookup runs the actual inherited getter exactly once. */
	*enabled = 0;
	status = bind_get_option(realm, options, name, present, &value);
	if (status != 0)
		return status;

	/* Only a defined member contributes its Boolean conversion. */
	if (*present)
		*enabled = vm_to_boolean(value);

	/* Succeeded: absence and the converted value remain independent outputs. */
	return 0;
}

static int
environment_mutation_filter(
	struct vm_realm *realm,
	vm_value source,
	struct vm_cell **filter)
{
	struct vm_cell *roots[4];
	struct vm_object *array;
	struct vm_string *name;
	vm_value iterator;
	vm_value member;
	vm_value made;
	unsigned index;
	unsigned registered;
	int object;
	int cell;
	int done;
	int status;

	/* WebIDL sequences require an object before consulting its iterator. */
	object = vm_value_is_object(source);
	if (!object) {
		status = vm_throw_type_error(realm, "attributeFilter must be an iterable object.");
		if (status != VM_THROWN)
			return status;

		/* Succeeded: a primitive filter is refused by the required native exception. */
		return VM_THROWN;
	}

	/* The source, iterator, yielded value and converted string survive callbacks. */
	memset(roots, 0, sizeof(roots));
	roots[0] = vm_value_as_cell(source);
	registered = 0;
	status = 0;
	for (index = 0; index < 4U; index++) {
		status = vm_heap_add_root(realm->heap, &roots[index]);
		if (status != 0)
			break;
		registered++;
	}

	/* Conversion publishes only a private caller-rooted array, never the original sequence. */
	if (status == 0) {
		do {
			status = vm_iter_start(realm, source, &iterator);
			if (status != 0)
				break;
			roots[1] = vm_value_as_cell(iterator);

			/* The copied sequence becomes rooted before converting its first member. */
			status = js_builtin_array(realm, NULL, 0, &made);
			if (status != 0)
				break;
			*filter = vm_value_as_cell(made);
			array = (struct vm_object *)*filter;

			/* Each member becomes a string before the next observable iterator step. */
			for (;;) {
				roots[2] = NULL;
				roots[3] = NULL;
				status = vm_iter_next(realm, iterator, &member, &done);
				if (status != 0 || done)
					break;
				cell = vm_value_is_cell(member);
				if (cell)
					roots[2] = vm_value_as_cell(member);

				/* A user conversion may allocate, mutate the original sequence, or throw. */
				status = vm_to_string(realm, member, &name);
				if (status != 0)
					break;
				roots[3] = &name->cell;

				/* The private array uses the VM's bounded integer-index representation. */
				if (array->length > INT32_MAX) {
					status = EOVERFLOW;
					break;
				}

				/* Store the converted string, not the original mutable member. */
				status = vm_object_define(realm->heap, array, vm_value_int32((int32_t)array->length), vm_value_cell(name), VM_PROPERTY_DEFAULT);
				if (status != 0)
					break;
			}
		} while (0);
	}

	/* Every root has stable storage until the iteration or conversion has ended. */
	while (registered != 0) {
		registered--;
		vm_heap_remove_root(realm->heap, &roots[registered]);
	}

	/* Preserve conversion, iteration and allocation failures precisely. */
	if (status != 0)
		return status;

	/* Succeeded: the caller owns an independent sequence of converted names. */
	return 0;
}

static int
environment_attribute_record(
	struct bind_window *window,
	struct environment_mutation_observer *observer,
	struct dom_element *element,
	struct vm_string *name,
	struct vm_string *old_value)
{
	struct vm_realm *realm;
	struct vm_object *record;
	struct vm_object *array;
	struct vm_cell *roots[5];
	vm_value target;
	vm_value added;
	vm_value removed;
	vm_value type;
	vm_value value;
	vm_value pending;
	unsigned index;
	unsigned registered;
	int new_pending;
	int status;

	/* The caller retains the native target and retired strings through all allocations. */
	realm = window->realm;
	memset(roots, 0, sizeof(roots));
	registered = 0;
	new_pending = 0;
	status = 0;
	for (index = 0; index < 5U; index++) {
		status = vm_heap_add_root(realm->heap, &roots[index]);
		if (status != 0)
			break;
		registered++;
	}

	/* No incomplete record reaches the observer's pending sequence. */
	if (status == 0) {
		do {
			status = bind_wrap(window, &element->node, &target);
			if (status != 0)
				break;
			roots[0] = vm_value_as_cell(target);

			/* Independent native lists preserve their real NodeList prototype and owner. */
			status = bind_empty_node_list(window, &added);
			if (status != 0)
				break;
			roots[1] = vm_value_as_cell(added);

			/* Removed nodes are independently empty for an attribute mutation. */
			status = bind_empty_node_list(window, &removed);
			if (status != 0)
				break;
			roots[2] = vm_value_as_cell(removed);

			/* The type string remains rooted while property names are allocated. */
			status = bind_string(realm, "attributes", &type);
			if (status != 0)
				break;
			roots[3] = vm_value_as_cell(type);

			/* Construct the complete snapshot before publishing it. */
			record = vm_object_create(realm->heap, realm->object_prototype);
			if (record == NULL) {
				status = ENOMEM;
				break;
			}

			/* Keep the unpublished payload alive while each property key is allocated. */
			roots[4] = &record->cell;

			/* Identify the ordinary attribute mutation. */
			status = js_builtin_value(realm, record, "type", type, VM_PROPERTY_DEFAULT);
			if (status != 0)
				break;

			/* Retain the actual changed element wrapper. */
			status = js_builtin_value(realm, record, "target", target, VM_PROPERTY_DEFAULT);
			if (status != 0)
				break;

			/* Attribute changes add no children. */
			status = js_builtin_value(realm, record, "addedNodes", added, VM_PROPERTY_DEFAULT);
			if (status != 0)
				break;

			/* Attribute changes remove no children. */
			status = js_builtin_value(realm, record, "removedNodes", removed, VM_PROPERTY_DEFAULT);
			if (status != 0)
				break;

			/* Attribute changes have no preceding child boundary. */
			status = js_builtin_value(realm, record, "previousSibling", VM_VALUE_NULL, VM_PROPERTY_DEFAULT);
			if (status != 0)
				break;

			/* Attribute changes have no following child boundary. */
			status = js_builtin_value(realm, record, "nextSibling", VM_VALUE_NULL, VM_PROPERTY_DEFAULT);
			if (status != 0)
				break;

			/* Preserve the committed ordinary attribute name. */
			status = js_builtin_value(realm, record, "attributeName", vm_value_cell(name), VM_PROPERTY_DEFAULT);
			if (status != 0)
				break;

			/* The ordinary mutation path has no attribute namespace. */
			status = js_builtin_value(realm, record, "attributeNamespace", VM_VALUE_NULL, VM_PROPERTY_DEFAULT);
			if (status != 0)
				break;

			/* Each observer independently requests or suppresses the retired value. */
			value = VM_VALUE_NULL;
			if (observer->attribute_old_value && old_value != NULL)
				value = vm_value_cell(old_value);
			status = js_builtin_value(realm, record, "oldValue", value, VM_PROPERTY_DEFAULT);
			if (status != 0)
				break;

			/* Allocate pending ownership only after every payload field is complete. */
			if (observer->records == NULL) {
				status = js_builtin_array(realm, NULL, 0, &pending);
				if (status != 0)
					break;
				observer->records = vm_value_as_cell(pending);
				status = environment_root(window, &observer->records);
				if (status != 0) {
					observer->records = NULL;
					break;
				}

				/* A failed first append must undo this newly established pending ownership. */
				new_pending = 1;
			}

			/* Guard the VM's integer-index limit before appending the completed payload. */
			array = (struct vm_object *)observer->records;
			if (array->length > INT32_MAX) {
				status = EOVERFLOW;
				break;
			}

			/* The observer now retains the complete record and both independent native lists. */
			status = vm_object_define(realm->heap, array, vm_value_int32((int32_t)array->length), vm_value_cell(record), VM_PROPERTY_DEFAULT);
		} while (0);
	}

	/* A failed first publication must not schedule an empty observer callback. */
	if (status != 0 && new_pending) {
		vm_heap_remove_root(realm->heap, &observer->records);
		observer->records = NULL;
	}

	/* Temporary payload roots end only after successful publication or complete failure. */
	while (registered != 0) {
		registered--;
		vm_heap_remove_root(realm->heap, &roots[registered]);
	}

	/* Preserve the exact allocation or property-definition failure. */
	if (status != 0)
		return status;

	/* Succeeded: a matching observer owns its requested attribute snapshot. */
	return 0;
}
