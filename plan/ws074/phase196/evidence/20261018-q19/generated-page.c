static int page_load_xhtml();
static int page_xhtml_inline_scripts();
static int page_xhtml_script_eligible();

static int
page_load_xhtml(
	struct page *page,
	const unsigned char *bytes,
	size_t length)
{
	struct xml_document *model;
	struct dom_document *parsed;
	struct dom_node *child;
	struct vm_cell *root;
	struct xml_error failure;
	int error;

	/* Strict parsing cannot publish a partial source tree. */
	model = NULL;
	memset(&failure, 0, sizeof(failure));
	error = xml_document_parse(&model, bytes, length, &failure);
	if (error != 0)
		return error;

	/* The existing XML projection retains all namespaces and self-closing tags. */
	parsed = NULL;
	error = xml_document_project(page->heap, model, DOM_CONTENT_XHTML, &parsed);
	xml_document_destroy(model);
	if (error != 0)
		return error;

	/* The projected owner must survive adoption allocations until its last child moves. */
	root = &parsed->node.cell;
	error = vm_heap_add_root(page->heap, &root);
	if (error != 0)
		return error;

	/* Preserve the original Window and Document identity already installed on this Page. */
	page->document->content = DOM_CONTENT_XHTML;
	while (parsed->node.first_child != NULL) {
		child = parsed->node.first_child;
		error = dom_adopt(page->document, child);
		if (error != 0)
			break;
		dom_append_child(&page->document->node, child);
	}

	/* The active Document now owns every adopted child or retains the precise adoption failure. */
	vm_heap_remove_root(page->heap, &root);
	if (error != 0)
		return error;

	/* Validated connected classic inline scripts run before document completion events. */
	error = page_xhtml_inline_scripts(page);
	if (error != 0)
		return error;

	/* Only the still-active owning Window can publish completion after script callbacks. */
	if (page->window->detached)
		return 0;

	/* Document readiness follows the supported inline source, as in the child XML path. */
	error = page_fire_load(page);
	if (error != 0)
		return error;

	/* Inline mutations determine the source order seen by the ordinary style gatherer. */
	error = page_gather_styles(page);
	if (error != 0)
		return error;

	/* Succeeded: the validated XHTML graph and supported inline lifecycle are installed. */
	return 0;
}

static int
page_xhtml_inline_scripts(
	struct page *page)
{
	struct wb_vector scripts;
	struct dom_node *walk;
	struct dom_node **entry;
	struct dom_node *saved;
	struct vm_cell **roots;
	struct bind_window *window;
	size_t index;
	size_t registered;
	size_t total;
	int eligible;
	int error;

	/* Snapshot collection uses only native allocation while the active Document owns its tree. */
	wb_vector_init(&scripts, sizeof(struct dom_node *));
	walk = page->document->node.first_child;
	error = 0;
	while (walk != NULL) {
		eligible = page_xhtml_script_eligible(walk, page->document);
		if (eligible) {
			error = wb_vector_push(&scripts, &walk);
			if (error != 0)
				break;
		}

		/* No callbacks run until every original candidate and saved context can be rooted. */
		walk = bind_following(walk, &page->document->node);
	}

	/* Native snapshot failure never starts even the first script. */
	if (error != 0) {
		wb_vector_release(&scripts);
		return error;
	}

	/* Script-free documents retain the old load path without any new root allocation. */
	if (scripts.count == 0U) {
		wb_vector_release(&scripts);
		return 0;
	}

	/* Reserve one extra stable slot for a currentScript replaced during nested evaluation. */
	if (scripts.count == SIZE_MAX) {
		wb_vector_release(&scripts);
		return ENOMEM;
	}

	/* calloc rejects any slot-table multiplication overflow before publication. */
	total = scripts.count + 1U;
	roots = calloc(total, sizeof(*roots));
	if (roots == NULL) {
		wb_vector_release(&scripts);
		return ENOMEM;
	}

	/* A removed original candidate remains alive until the last earlier callback has finished. */
	window = page->window;
	saved = window->current_script;
	for (index = 0; index < scripts.count; index++) {
		entry = wb_vector_at(&scripts, index);
		roots[index] = &(*entry)->cell;
	}

	/* Replacing the Window's traced currentScript must not lose an outer script's only edge. */
	if (saved != NULL)
		roots[scripts.count] = &saved->cell;

	/* Each successful registration becomes an obligation for the shared cleanup path. */
	registered = 0;
	for (index = 0; index < total; index++) {
		error = vm_heap_add_root(page->heap, &roots[index]);
		if (error != 0)
			break;
		registered++;
	}

	/* All registered storage remains fixed throughout arbitrary inline DOM mutation and GC. */
	if (error == 0) {
		for (index = 0; index < scripts.count; index++) {
			/* A script that retired this owner cannot trigger further old-document execution. */
			if (window->detached)
				break;

			/* Prior callbacks may remove, adopt or dynamically prepare a saved candidate. */
			entry = wb_vector_at(&scripts, index);
			eligible = page_xhtml_script_eligible(*entry, page->document);
			if (!eligible)
				continue;

			/* The shared inline backend excludes external/module/data source and accepts text or CDATA. */
			bind_window_set_current_script(window, *entry);
			error = bind_run_inline_script(window, (struct dom_element *)*entry);
			bind_window_set_current_script(window, saved);
			if (error != 0)
				break;
		}
	}

	/* Drop every successfully registered slot before releasing its native table. */
	while (registered != 0) {
		registered--;
		vm_heap_remove_root(page->heap, &roots[registered]);
	}

	/* Temporary ownership ends after execution or any root/evaluation failure. */
	free(roots);
	wb_vector_release(&scripts);
	if (error != 0)
		return error;

	/* Succeeded: only still-connected supported original inline candidates were prepared once. */
	return 0;
}

static int
page_xhtml_script_eligible(
	const struct dom_node *node,
	const struct dom_document *document)
{
	const struct dom_node *ancestor;
	const struct dom_element *element;
	int same;
	int connected;

	/* Only an unprepared element belonging to this current Document is a candidate. */
	if (node->type != DOM_ELEMENT || node->document != document)
		return 0;

	/* Dynamic insertion may already have prepared a snapshotted original node. */
	if ((node->flags & DOM_NODE_SCRIPT_STARTED) != 0U)
		return 0;

	/* XML preserves local-name case and actual namespace identity. */
	element = (const struct dom_element *)node;
	if (element->ns != DOM_NS_HTML)
		return 0;

	/* Uppercase or foreign script-looking tags are not HTML script elements. */
	same = vm_string_equal_ascii(element->local_name, "script");
	if (!same)
		return 0;

	/* A saved node must still reach the active Document without crossing an inert template. */
	connected = 0;
	for (ancestor = node->parent; ancestor != NULL; ancestor = ancestor->parent) {
		/* Reaching this root establishes the still-active ownership path. */
		if (ancestor == &document->node) {
			connected = 1;
			break;
		}

		/* Non-element ancestors carry no HTML-template inertness of their own. */
		if (ancestor->type != DOM_ELEMENT)
			continue;

		/* XML's exact lowercase HTML template name keeps its descendants inert. */
		element = (const struct dom_element *)ancestor;
		if (element->ns != DOM_NS_HTML)
			continue;

		/* Foreign or differently cased template-looking tags do not create this HTML boundary. */
		same = vm_string_equal_ascii(element->local_name, "template");
		if (same)
			return 0;
	}

	/* A removed or adopted snapshot has no connected execution path in this Document. */
	if (!connected)
		return 0;

	/* Succeeded: this exact unprepared HTML script remains connected outside inert templates. */
	return 1;
}
