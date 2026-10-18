static int values_flex_basis(const struct css_token *token, struct css_value *value);
static struct css_length cascade_flex_basis(struct css_engine *engine, const struct css_value *value, float font_size);
static int computed_length(const struct css_length *length, struct wb_units *out);
static int flex_content_basis(struct layout_tree *tree, struct layout_box *container, struct layout_box *child, int row, layout_unit *basis);
static int flex_base_size(struct layout_tree *tree, struct layout_box *box, struct flex_item *item, int row, layout_unit available);

/* values_flex_basis full final generation scope. */
static int
values_flex_basis(
	const struct css_token *token,
	struct css_value *value)
{
	int content;
	int error;

	/* Preserves content rather than borrowing the preferred main size. */
	content = css_ident_equal(token, "content");
	if (content) {
		value->kind = CSS_VALUE_KEYWORD;
		value->keyword = CSS_UNIT_CONTENT;
		return 0;
	}

	/* Parses the existing width-like keywords and literal lengths. */
	error = values_length(token, 1, value);
	if (error != 0)
		return error;

	/* The generic length keywords none and normal are not flex bases. */
	if (value->kind == CSS_VALUE_KEYWORD) {
		/* These generic keywords do not belong to the width grammar. */
		if (value->keyword == CSS_UNIT_NONE || value->keyword == CSS_UNIT_NORMAL)
			return EINVAL;
	} else {
		/* Literal negatives are invalid; math functions use a separate parser. */
		if (value->number < 0)
			return EINVAL;
	}

	/* Succeeded: the token is one accepted flex basis. */
	return 0;
}

/* cascade_flex_basis full final generation scope. */
static struct css_length
cascade_flex_basis(
	struct css_engine *engine,
	const struct css_value *value,
	float font_size)
{
	struct css_length basis;

	/* Resolves absolute units while retaining the calc percentage type. */
	basis = cascade_length(engine, value, font_size);

	/* Absolute math results obey the nonnegative computed range. */
	if (basis.unit == CSS_UNIT_PX) {
		/* An absolute negative cannot be represented by this property. */
		if (basis.value < 0)
			basis.value = 0;
	}

	/* A pure percentage can be range-clamped without resolving its basis. */
	if (basis.unit == CSS_UNIT_PERCENT && basis.offset == 0) {
		/* No pixel term remains that could offset the negative percentage. */
		if (basis.value < 0)
			basis.value = 0;
	}

	/* Succeeded: mixed expressions defer their range check until use. */
	return basis;
}

/* computed_length full final generation scope. */
static int
computed_length(
	const struct css_length *length,
	struct wb_units *out)
{
	int error;

	/* Serializes the computed unit without resolving percentage dependence. */
	switch (length->unit) {
	case CSS_UNIT_PX:
		error = computed_pixels(length->value, out);
		break;
	case CSS_UNIT_PERCENT:
		error = computed_write_percent(length, out);
		break;
	case CSS_UNIT_AUTO:
		error = computed_ascii("auto", out);
		break;
	case CSS_UNIT_NONE:
		error = computed_ascii("none", out);
		break;
	case CSS_UNIT_NORMAL:
		error = computed_ascii("normal", out);
		break;
	case CSS_UNIT_NUMBER:
		error = computed_number(length->value, out);
		break;
	case CSS_UNIT_MAX_CONTENT:
		error = computed_ascii("max-content", out);
		break;
	case CSS_UNIT_MIN_CONTENT:
		error = computed_ascii("min-content", out);
		break;
	case CSS_UNIT_CONTENT:
		error = computed_ascii("content", out);
		break;
	default:
		error = computed_ascii("fit-content", out);
		break;
	}

	/* Propagates a failed append to the computed string. */
	if (error != 0)
		return error;

	/* Succeeded: the complete length or keyword is appended. */
	return 0;
}

/* flex_content_basis full final generation scope. */
static int
flex_content_basis(
	struct layout_tree *tree,
	struct layout_box *container,
	struct layout_box *child,
	int row,
	layout_unit *basis)
{
	struct css_length preferred;
	struct css_length minimum;
	struct css_length maximum;
	struct layout_context context;
	struct wb_vector floats;
	layout_unit cached_width;
	int cached_known;
	int error;

	/* Saves the preferred size, main limits, and cache owned by ordinary layout. */
	cached_width = child->max_content;
	cached_known = child->max_content_known;
	preferred = child->style.width;
	minimum = child->style.min_width;
	maximum = child->style.max_width;
	if (!row) {
		preferred = child->style.height;
		minimum = child->style.min_height;
		maximum = child->style.max_height;
	}

	/* Content is measured before main-axis limits or preferred sizes apply. */
	child->max_content_known = 0;
	if (row) {
		child->style.width.unit = CSS_UNIT_AUTO;
		child->style.min_width.unit = CSS_UNIT_AUTO;
		child->style.max_width.unit = CSS_UNIT_NONE;
	} else {
		child->style.height.unit = CSS_UNIT_AUTO;
		child->style.min_height.unit = CSS_UNIT_AUTO;
		child->style.max_height.unit = CSS_UNIT_NONE;
	}

	/* Isolates intrinsic width or natural height from the surrounding float context. */
	if (row) {
		error = layout_max_content(tree, child, basis);
	} else {
		tree->measuring++;
		layout_context_begin(tree, &context, &floats);
		error = layout_block(tree, child, container->width);
		layout_context_end(tree, &context);
		tree->measuring--;

		/* Reads the measured height only after the complete layout succeeded. */
		if (error == 0)
			*basis = child->height;
	}

	/* Restores ordinary layout state even when the measurement failed. */
	child->max_content = cached_width;
	child->max_content_known = cached_known;
	if (row) {
		child->style.width = preferred;
		child->style.min_width = minimum;
		child->style.max_width = maximum;
	} else {
		child->style.height = preferred;
		child->style.min_height = minimum;
		child->style.max_height = maximum;
	}

	/* Propagates a failed intrinsic measurement after restoring borrowed state. */
	if (error != 0)
		return error;

	/* Succeeded: the base contains only the unconstrained content measurement. */
	return 0;
}

/* flex_base_size full final generation scope. */
static int
flex_base_size(
	struct layout_tree *tree,
	struct layout_box *box,
	struct flex_item *item,
	int row,
	layout_unit available)
{
	struct layout_box *child;
	const struct css_length *basis;
	layout_unit sizing;
	layout_unit cross;
	layout_unit resolved;
	int start_side;
	int end_side;
	int error;

	/* The item's box model against the container's width. */
	child = item->box;
	layout_box_model(child, box->width);

	/* The sides the main axis starts and ends at: left and right for a row, top and bottom for a column. */
	start_side = CSS_TOP;
	end_side = CSS_BOTTOM;
	if (row) {
		start_side = CSS_LEFT;
		end_side = CSS_RIGHT;
	}

	/* Its frame and margins along the main axis (an auto margin counts as none until the free space is shared). */
	item->frame = child->border[start_side] + child->padding[start_side] + child->padding[end_side] + child->border[end_side];
	item->margin_start = child->margin[start_side];
	item->margin_end = child->margin[end_side];

	/* An auto margin at the start takes a share of the free space later. */
	item->auto_start = 0;
	if (child->style.margin[start_side].unit == CSS_UNIT_AUTO)
		item->auto_start = 1;

	/* So does one at the end. */
	item->auto_end = 0;
	if (child->style.margin[end_side].unit == CSS_UNIT_AUTO)
		item->auto_end = 1;

	/* Under border-box sizing the given sizes include the frame. */
	sizing = 0;
	if (child->style.box_sizing == CSS_BOX_SIZING_BORDER)
		sizing = item->frame;

	/* The basis: flex-basis, or the width or height when it is auto. */
	basis = &child->style.flex_basis;
	if (basis->unit == CSS_UNIT_AUTO) {
		basis = &child->style.width;

		/* A column borrows its preferred height instead of its width. */
		if (!row)
			basis = &child->style.height;
	}

	/*
	 * A definite basis: pixels, or a percentage of a definite main size (a
	 * container being measured has none: its width is only the measuring
	 * width).
	 */
	item->base = -1;
	if (basis->unit == CSS_UNIT_PX)
		item->base = layout_from_px(basis->value) - sizing;

	/* A percentage math result is clamped only after its basis becomes definite. */
	if (basis->unit == CSS_UNIT_PERCENT &&
	    available >= 0 &&
	    tree->measuring == 0) {
		resolved = flex_length(basis, available);

		/* A negative used length is zero before the box frame is removed. */
		if (resolved < 0)
			resolved = 0;

		/* The base is measured inside the frame for border-box sizing. */
		item->base = resolved - sizing;
	}

	/* A definite cross size supplies the content basis through the ratio. */
	if (item->base < 0) {
		/* Replaced content keeps its separate intrinsic sizing path. */
		if (!child->replaced) {
			/* A preferred ratio can transfer a definite cross size. */
			if (child->style.aspect_ratio > 0.0f) {
				/* Include single-line stretch before any flexing takes place. */
				cross = flex_ratio_cross_size(tree, box, child, row);
				if (cross >= 0)
					item->base = flex_ratio_main_size(child, row, cross);
			}
		}
	}

	/* An explicit content basis ignores the non-replaced preferred main size. */
	if (item->base < 0 &&
	    basis->unit == CSS_UNIT_CONTENT &&
	    !child->replaced) {
		error = flex_content_basis(tree, box, child, row, &item->base);
		if (error != 0)
			return error;
	}

	/* Otherwise the content's size: its widest line, or its height at the container's width. */
	if (item->base < 0 && row) {
		/* Replaced items obtain their width from ordinary replaced layout. */
		if (child->replaced) {
			error = layout_block(tree, child, box->width);
			if (error != 0)
				return error;

			/* Uses the replaced content width as its initial base. */
			item->base = child->width;
		} else {
			/* The content's width without a limit (measured once, its percentages indefinite). */
			error = layout_max_content(tree, child, &item->base);
			if (error != 0)
				return error;
		}
	} else if (item->base < 0) {
		error = layout_block(tree, child, box->width);
		if (error != 0)
			return error;

		/* Uses the natural block height as the column content size. */
		item->base = child->height;
	}

	/* A size is never negative. */
	if (item->base < 0)
		item->base = 0;

	/* The limits: min-width and max-width (min-height and max-height for a column). */
	item->minimum = 0;
	item->maximum = -1;
	item->auto_minimum = 0;
	if (row &&
	    child->style.min_width.unit == CSS_UNIT_AUTO &&
	    (child->style.overflow_x == CSS_OVERFLOW_VISIBLE || child->style.overflow_x == CSS_OVERFLOW_CLIP))
		item->auto_minimum = 1;

	/* Resolves the main-axis limits after the unconstrained base is measured. */
	if (row) {
		/* An explicit width minimum limits the eventual hypothetical size. */
		if (child->style.min_width.unit == CSS_UNIT_PX || child->style.min_width.unit == CSS_UNIT_PERCENT)
			item->minimum = flex_length(&child->style.min_width, box->width) - sizing;

		/* A finite width maximum limits the eventual hypothetical size. */
		if (child->style.max_width.unit == CSS_UNIT_PX || child->style.max_width.unit == CSS_UNIT_PERCENT)
			item->maximum = flex_length(&child->style.max_width, box->width) - sizing;
	} else {
		/* Explicit height limits apply on a column main axis. */
		if (child->style.min_height.unit == CSS_UNIT_PX)
			item->minimum = layout_from_px(child->style.min_height.value) - sizing;

		/* A finite height maximum is independent of the measured base. */
		if (child->style.max_height.unit == CSS_UNIT_PX)
			item->maximum = layout_from_px(child->style.max_height.value) - sizing;
	}

	/* Ratio items need their content floor before line breaking or flex growth. */
	error = flex_ratio_minimum(tree, box, item, row, available);
	if (error != 0)
		return error;

	/* Neither is the minimum. */
	if (item->minimum < 0)
		item->minimum = 0;

	/* The base stays unconstrained; the hypothetical size is limited for line breaking. */
	item->size = item->base;
	if (item->maximum >= 0 && item->size > item->maximum)
		item->size = item->maximum;

	/* The minimum overrides a conflicting maximum in the hypothetical size. */
	if (item->size < item->minimum)
		item->size = item->minimum;

	/* The container's measure of its content counts the item's margin box at that size. */
	child->flex_hypothetical = item->size + item->frame + item->margin_start + item->margin_end;

	/* Succeeded: the item has its hypothetical size. */
	return 0;
}
