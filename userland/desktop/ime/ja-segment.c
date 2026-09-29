/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Splitting a reading into segments, and each segment's candidates
 * (plan/ws095/design.md section 7.2).
 *
 * A segment is a word and the particles after it: a noun from a
 * dictionary, a verb or an adjective with its okurigana (ja-inflect.c),
 * する or 来る, a noun with する, a run of kana no dictionary knows, or a
 * run of literals.  The split is found by dynamic programming from left to
 * right, comparing the costs of the ways to reach each place in this
 * order: the fewest kana no dictionary knows and one-kana nouns counted
 * together (い 胃 and き 木 would otherwise swallow kana), then the fewest
 * segments, then the most kana found in a dictionary.
 */

#include "ja.h"

#include <stdlib.h>
#include <string.h>

/* The letters a verb's or an adjective's headword can end with. */
#define SEGMENT_CONSONANTS	"abdeghijkmnoprstuwyz"

/*
 * A word the dictionaries lack or get wrong that the engine always knows.
 */
struct segment_builtin {
	const char *reading;
	const char *candidates[3];
};

/*
 * The cost of the best way found so far to reach a place in the reading,
 * and the place its last segment started from.
 */
struct segment_cost {
	bool reached;
	unsigned int unknown;
	unsigned int single;
	unsigned int segments;
	unsigned int dictionary;
	size_t previous;
};

/*
 * What one segment adds to a cost.
 */
struct segment_item {
	unsigned int unknown;
	unsigned int single;
	unsigned int dictionary;
};

/*
 * The words the engine knows without a dictionary.
 */
static const struct segment_builtin segment_builtins[] = {
	{ "いい", { "いい", "良い", NULL } }
};

static bool *segment_particle_table(const struct ja_text *text, size_t start);
static bool segment_has_noun(const struct ja_lexicon *lexicon, const char *key, size_t length);
static bool segment_has_verb(const struct ja_lexicon *lexicon, const char *key, size_t length);
static bool segment_all_kana(const struct ja_text *text, size_t start, size_t end);
static void segment_item_better(struct segment_item *best, bool *valid, const struct segment_item *item);
static bool segment_cost_better(const struct segment_cost *left, const struct segment_cost *right);
static void segment_cores(const struct ja_lexicon *lexicon, const struct ja_text *text, size_t start, struct segment_item *cores, bool *valid);
static void segment_add_nouns(const struct ja_lexicon *lexicon, const char *key, size_t length, const char *suffix, size_t suffix_length, struct ja_segment *segment);
static void segment_add_entry(const struct ja_dict_entry *entry, const char *suffix, size_t suffix_length, struct ja_segment *segment);
static void segment_add_joined(struct ja_segment *segment, const char *word, size_t word_length, const char *suffix, size_t suffix_length);
static void segment_add_core_candidates(const struct ja_lexicon *lexicon, const struct ja_text *text, size_t start, size_t core_end, size_t end, struct ja_segment *segment);
static void segment_add_katakana(const struct ja_text *text, size_t start, size_t core_end, size_t end, struct ja_segment *segment);
static void segment_add_full_width(const struct ja_text *text, size_t start, size_t end, struct ja_segment *segment);

/*
 * Builds the segmenter's view of a composition.
 */
void
ja_text_build(
	struct ja_text *text,
	const struct ja_unit *units,
	size_t unit_count)
{
	size_t i;
	size_t used;

	/* Writes each character and notes where it starts. */
	text->length = 0;
	for (i = 0; i < unit_count; i++) {
		text->offsets[i] = text->length;
		used = ja_utf8_encode(units[i].code, text->bytes + text->length);
		text->length += used;
		text->kana[i] = false;
		if (units[i].kind == JA_UNIT_KANA)
			text->kana[i] = true;
	}

	/* The end is a boundary as well. */
	text->offsets[unit_count] = text->length;
	text->bytes[text->length] = '\0';
	text->unit_count = unit_count;
}

/*
 * Splits a reading from a unit to its end into segments.
 *
 * Returns the number of spans written, in order.
 */
size_t
ja_segment_split(
	const struct ja_lexicon *lexicon,
	const struct ja_text *text,
	size_t start,
	struct ja_span *spans,
	size_t spans_max)
{
	struct segment_cost costs[JA_UNITS_MAX + 1U];
	struct segment_cost candidate;
	struct segment_item cores[JA_UNITS_MAX + 1U];
	bool valid[JA_UNITS_MAX + 1U];
	struct ja_span reversed[JA_UNITS_MAX];
	bool *particles;
	size_t count;
	size_t position;
	size_t core_end;
	size_t end;
	size_t row;
	size_t i;
	bool better;

	/* Nothing to split. */
	if (start >= text->unit_count)
		return 0;

	/* Where particles can end, from every place. */
	particles = segment_particle_table(text, start);
	if (particles == NULL) {
		/* Without memory the whole reading is one segment. */
		spans[0].start = start;
		spans[0].end = text->unit_count;
		return 1;
	}

	/* Only the start is reached before anything is split. */
	memset(costs, 0, sizeof(costs));
	costs[start].reached = true;

	/* From each place reached, tries every segment that can start there. */
	row = text->unit_count + 1U;
	for (position = start; position < text->unit_count; position++) {
		if (!costs[position].reached)
			continue;

		/* The words that can start here, by where each ends. */
		memset(valid, 0, sizeof(valid));
		segment_cores(lexicon, text, position, cores, valid);

		/* Each word with each run of particles after it is one segment. */
		for (core_end = position + 1U; core_end <= text->unit_count; core_end++) {
			if (!valid[core_end])
				continue;

			for (end = core_end; end <= text->unit_count; end++) {
				if (!particles[core_end * row + end])
					continue;

				/* The cost of reaching the end through this segment. */
				candidate = costs[position];
				candidate.unknown += cores[core_end].unknown;
				candidate.single += cores[core_end].single;
				candidate.segments++;
				candidate.dictionary += cores[core_end].dictionary;
				candidate.previous = position;
				candidate.reached = true;

				/* Keeps the cheaper way to the end. */
				better = segment_cost_better(&candidate, &costs[end]);
				if (better)
					costs[end] = candidate;
			}
		}

		/* A run of particles alone is a segment too (the は left after a shortened segment). */
		for (end = position + 1U; end <= text->unit_count; end++) {
			if (!particles[position * row + end])
				continue;

			/* The cost of reaching the end through the particles. */
			candidate = costs[position];
			candidate.segments++;
			candidate.previous = position;
			candidate.reached = true;

			/* Keeps the cheaper way to the end. */
			better = segment_cost_better(&candidate, &costs[end]);
			if (better)
				costs[end] = candidate;
		}
	}

	free(particles);

	/* Walks back from the end along the cheapest way. */
	count = 0;
	end = text->unit_count;
	while (end > start && count < JA_UNITS_MAX) {
		reversed[count].start = costs[end].previous;
		reversed[count].end = end;
		count++;
		end = costs[end].previous;
	}

	/* Gives the spans in reading order, as many as fit. */
	if (count > spans_max)
		count = spans_max;

	for (i = 0; i < count; i++)
		spans[i] = reversed[count - 1U - i];

	/* The last span that fits reaches the end. */
	spans[count - 1U].end = text->unit_count;
	return count;
}

/*
 * Lists a segment's candidates: what the user chose before, the words of
 * the dictionaries with the segment's okurigana and particles (the longest
 * word first), the literal as typed, and the segment in hiragana and
 * katakana.
 */
void
ja_segment_candidates(
	const struct ja_lexicon *lexicon,
	const struct ja_text *text,
	struct ja_segment *segment)
{
	const struct ja_user_entry *learned;
	bool ends[JA_UNITS_MAX + 1U];
	size_t core_end;
	size_t i;
	const char *span_bytes;
	size_t span_length;
	bool particle;
	bool all_kana;

	segment->candidate_count = 0;
	segment->selected = 0;
	segment->presses = 0;
	span_bytes = text->bytes + text->offsets[segment->start];
	span_length = text->offsets[segment->end] - text->offsets[segment->start];

	/* What the user chose for this reading before comes first. */
	learned = NULL;
	if (lexicon->user != NULL)
		learned = ja_user_find(lexicon->user, span_bytes, span_length);
	if (learned != NULL) {
		for (i = 0; i < learned->candidate_count; i++)
			ja_segment_add_candidate(segment, learned->candidates[i], strlen(learned->candidates[i]));
	}

	/* A segment that is one particle is offered as typed first (the は left after shortening). */
	particle = ja_is_particle(text, segment->start, segment->end);
	if (particle)
		ja_segment_add_candidate(segment, span_bytes, span_length);

	/* The words, longest first, each with the particles after it. */
	for (core_end = segment->end; core_end > segment->start; core_end--) {
		memset(ends, 0, sizeof(ends));
		ja_particle_ends(text, core_end, ends);
		if (!ends[segment->end])
			continue;

		/* Only kana make words. */
		all_kana = segment_all_kana(text, segment->start, core_end);
		if (!all_kana)
			continue;

		segment_add_core_candidates(lexicon, text, segment->start, core_end, segment->end, segment);
	}

	/* A literal is offered as typed and in full width. */
	if (!text->kana[segment->start]) {
		ja_segment_add_candidate(segment, span_bytes, span_length);
		segment_add_full_width(text, segment->start, segment->end, segment);
	}

	/* The segment in hiragana. */
	ja_segment_add_candidate(segment, span_bytes, span_length);

	/* And in katakana, with the particles left in hiragana. */
	for (core_end = segment->end; core_end > segment->start; core_end--) {
		memset(ends, 0, sizeof(ends));
		ja_particle_ends(text, core_end, ends);
		if (!ends[segment->end])
			continue;

		/* Only a kana word is written in katakana. */
		all_kana = segment_all_kana(text, segment->start, core_end);
		if (!all_kana)
			continue;

		segment_add_katakana(text, segment->start, core_end, segment->end, segment);
	}
}

/*
 * Adds a candidate to a segment unless it has it already, or it is too
 * long, or the list is full.
 *
 * Returns whether the segment has the candidate afterwards.
 */
bool
ja_segment_add_candidate(
	struct ja_segment *segment,
	const char *text,
	size_t length)
{
	size_t i;
	bool same;

	/* An empty candidate or one too long for the list. */
	if (length == 0U || length >= IME_CANDIDATE_MAX)
		return false;

	/* A candidate listed already. */
	for (i = 0; i < segment->candidate_count; i++) {
		same = ja_bytes_equal(segment->candidates[i], strlen(segment->candidates[i]), text, length);
		if (same)
			return true;
	}

	/* No room for another. */
	if (segment->candidate_count >= IME_CANDIDATES_MAX)
		return false;

	/* Succeeded: the candidate is last in the list. */
	memcpy(segment->candidates[segment->candidate_count], text, length);
	segment->candidates[segment->candidate_count][length] = '\0';
	segment->candidate_count++;
	return true;
}

/*
 * Makes the table of where particles can end, from each place from a
 * start: entry [from * (units + 1) + to].
 *
 * Returns NULL without memory.
 */
static bool *
segment_particle_table(
	const struct ja_text *text,
	size_t start)
{
	bool *table;
	size_t row;
	size_t from;

	/* One row per place, each with a flag per place. */
	row = text->unit_count + 1U;
	table = calloc(row * row, sizeof(table[0]));
	if (table == NULL)
		return NULL;

	/* Fills each row from its place. */
	for (from = start; from <= text->unit_count; from++)
		ja_particle_ends(text, from, table + from * row);

	/* Succeeded: the table. */
	return table;
}

/*
 * Tells whether any dictionary has a reading as a noun.
 */
static bool
segment_has_noun(
	const struct ja_lexicon *lexicon,
	const char *key,
	size_t length)
{
	const struct ja_dict_entry *entry;
	const struct ja_user_entry *learned;
	size_t i;
	bool same;

	/* A reading the user converted before. */
	if (lexicon->user != NULL) {
		learned = ja_user_find(lexicon->user, key, length);
		if (learned != NULL)
			return true;
	}

	/* A word the engine knows itself. */
	for (i = 0; i < sizeof(segment_builtins) / sizeof(segment_builtins[0]); i++) {
		same = ja_bytes_equal(segment_builtins[i].reading, strlen(segment_builtins[i].reading), key, length);
		if (same)
			return true;
	}

	/* A headword of one of the dictionaries. */
	for (i = 0; i < lexicon->dict_count; i++) {
		entry = ja_dict_find(lexicon->dicts[i], key, length);
		if (entry != NULL)
			return true;
	}

	/* No dictionary knows it. */
	return false;
}

/*
 * Tells whether any dictionary has a verb's or an adjective's headword.
 */
static bool
segment_has_verb(
	const struct ja_lexicon *lexicon,
	const char *key,
	size_t length)
{
	const struct ja_dict_entry *entry;
	size_t i;

	/* A headword of one of the dictionaries. */
	for (i = 0; i < lexicon->dict_count; i++) {
		entry = ja_dict_find(lexicon->dicts[i], key, length);
		if (entry != NULL)
			return true;
	}

	/* No dictionary knows it. */
	return false;
}

/*
 * Tells whether every unit of a span is a kana.
 */
static bool
segment_all_kana(
	const struct ja_text *text,
	size_t start,
	size_t end)
{
	size_t i;

	/* Looks for a literal in the span. */
	for (i = start; i < end; i++) {
		if (!text->kana[i])
			return false;
	}

	/* Only kana. */
	return true;
}

/*
 * Keeps the cheaper of two ways to make one word.
 */
static void
segment_item_better(
	struct segment_item *best,
	bool *valid,
	const struct segment_item *item)
{
	/* The first way found. */
	if (!*valid) {
		*best = *item;
		*valid = true;
		return;
	}

	/* Fewer unknown kana and one-kana nouns win, then more dictionary kana. */
	if (item->unknown + item->single != best->unknown + best->single) {
		if (item->unknown + item->single < best->unknown + best->single)
			*best = *item;
		return;
	}

	if (item->dictionary > best->dictionary)
		*best = *item;
}

/*
 * Tells whether one way to reach a place is cheaper than another.
 */
static bool
segment_cost_better(
	const struct segment_cost *left,
	const struct segment_cost *right)
{
	unsigned int left_doubt;
	unsigned int right_doubt;

	/* Any way beats none. */
	if (!right->reached)
		return true;

	/*
	 * The fewest kana no dictionary knows and one-kana nouns together: a
	 * one-kana noun (子, 胃) that takes a kana out of an unknown word saves
	 * nothing this way.
	 */
	left_doubt = left->unknown + left->single;
	right_doubt = right->unknown + right->single;
	if (left_doubt != right_doubt) {
		if (left_doubt < right_doubt)
			return true;
		return false;
	}

	/* Then the fewest segments. */
	if (left->segments != right->segments) {
		if (left->segments < right->segments)
			return true;
		return false;
	}

	/* Then the most kana found in a dictionary. */
	if (left->dictionary > right->dictionary)
		return true;

	/* Not cheaper: the way found first stays. */
	return false;
}

/*
 * Finds every word that can start at a place, keeping for each end the
 * cheapest way to make it.
 */
static void
segment_cores(
	const struct ja_lexicon *lexicon,
	const struct ja_text *text,
	size_t start,
	struct segment_item *cores,
	bool *valid)
{
	struct segment_item item;
	bool ends[JA_UNITS_MAX + 1U];
	char key[JA_HEADWORD_MAX * 4U + 2U];
	size_t run_end;
	size_t length;
	size_t key_length;
	size_t end;
	size_t i;
	bool found;

	/* A run of literals is one word of its own. */
	if (!text->kana[start]) {
		end = start;
		while (end < text->unit_count && !text->kana[end])
			end++;

		memset(&item, 0, sizeof(item));
		segment_item_better(&cores[end], &valid[end], &item);
		return;
	}

	/* Words are made of the run of kana from here. */
	run_end = start;
	while (run_end < text->unit_count && text->kana[run_end])
		run_end++;

	/* Kana no dictionary knows, any number of them. */
	for (end = start + 1U; end <= run_end; end++) {
		item.unknown = (unsigned int)(end - start);
		item.single = 0;
		item.dictionary = 0;
		segment_item_better(&cores[end], &valid[end], &item);
	}

	/* する and 来る in their forms. */
	memset(ends, 0, sizeof(ends));
	ja_inflect_suru_ends(text, start, ends);
	ja_inflect_kuru_ends(text, start, ends);
	for (end = start + 1U; end <= run_end; end++) {
		if (!ends[end])
			continue;

		item.unknown = 0;
		item.single = 0;
		item.dictionary = (unsigned int)(end - start);
		segment_item_better(&cores[end], &valid[end], &item);
	}

	/* Each reading from here that is a noun, alone or with する. */
	for (length = 1; length <= JA_HEADWORD_MAX && start + length <= run_end; length++) {
		key_length = text->offsets[start + length] - text->offsets[start];
		found = segment_has_noun(lexicon, text->bytes + text->offsets[start], key_length);
		if (!found)
			continue;

		/* The noun alone; a noun of one kana costs more. */
		item.unknown = 0;
		item.single = 0;
		if (length == 1U)
			item.single = 1;
		item.dictionary = (unsigned int)length;
		segment_item_better(&cores[start + length], &valid[start + length], &item);

		/* The noun with a form of する (勉強します). */
		memset(ends, 0, sizeof(ends));
		ja_inflect_suru_ends(text, start + length, ends);
		for (end = start + length + 1U; end <= run_end; end++) {
			if (!ends[end])
				continue;

			item.dictionary = (unsigned int)(end - start);
			segment_item_better(&cores[end], &valid[end], &item);
		}
	}

	/* Each reading from here that is a verb's or an adjective's stem. */
	for (length = 1; length <= JA_HEADWORD_MAX && start + length < run_end; length++) {
		key_length = text->offsets[start + length] - text->offsets[start];
		memcpy(key, text->bytes + text->offsets[start], key_length);
		for (i = 0; SEGMENT_CONSONANTS[i] != '\0'; i++) {
			key[key_length] = SEGMENT_CONSONANTS[i];
			found = segment_has_verb(lexicon, key, key_length + 1U);
			if (!found)
				continue;

			/* Every okurigana the headword can take here. */
			memset(ends, 0, sizeof(ends));
			ja_inflect_ends(text, start + length, SEGMENT_CONSONANTS[i], ends);
			for (end = start + length + 1U; end <= run_end; end++) {
				if (!ends[end])
					continue;

				item.unknown = 0;
				item.single = 0;
				item.dictionary = (unsigned int)(end - start);
				segment_item_better(&cores[end], &valid[end], &item);
			}
		}
	}
}

/*
 * Adds the nouns of a reading from every dictionary, each followed by a
 * suffix.
 */
static void
segment_add_nouns(
	const struct ja_lexicon *lexicon,
	const char *key,
	size_t length,
	const char *suffix,
	size_t suffix_length,
	struct ja_segment *segment)
{
	const struct ja_dict_entry *entry;
	const struct ja_user_entry *learned;
	size_t i;
	size_t j;
	bool same;

	/* What the user chose for the word alone. */
	if (lexicon->user != NULL) {
		learned = ja_user_find(lexicon->user, key, length);
		if (learned != NULL) {
			for (i = 0; i < learned->candidate_count; i++)
				segment_add_joined(segment, learned->candidates[i], strlen(learned->candidates[i]), suffix, suffix_length);
		}
	}

	/* The words the engine knows itself. */
	for (i = 0; i < sizeof(segment_builtins) / sizeof(segment_builtins[0]); i++) {
		same = ja_bytes_equal(segment_builtins[i].reading, strlen(segment_builtins[i].reading), key, length);
		if (!same)
			continue;

		for (j = 0; segment_builtins[i].candidates[j] != NULL; j++) {
			segment_add_joined(segment, segment_builtins[i].candidates[j], strlen(segment_builtins[i].candidates[j]),
					   suffix, suffix_length);
		}
	}

	/* Each dictionary in order. */
	for (i = 0; i < lexicon->dict_count; i++) {
		entry = ja_dict_find(lexicon->dicts[i], key, length);
		if (entry != NULL)
			segment_add_entry(entry, suffix, suffix_length, segment);
	}
}

/*
 * Adds every candidate of a dictionary entry, each followed by a suffix.
 */
static void
segment_add_entry(
	const struct ja_dict_entry *entry,
	const char *suffix,
	size_t suffix_length,
	struct ja_segment *segment)
{
	const char *word;
	size_t word_length;
	size_t position;
	bool more;

	/* Walks the entry's candidates. */
	position = 0;
	for (;;) {
		more = ja_dict_next_candidate(entry, &position, &word, &word_length);
		if (!more)
			break;

		segment_add_joined(segment, word, word_length, suffix, suffix_length);
	}
}

/*
 * Adds a word followed by a suffix as one candidate.
 */
static void
segment_add_joined(
	struct ja_segment *segment,
	const char *word,
	size_t word_length,
	const char *suffix,
	size_t suffix_length)
{
	char joined[IME_CANDIDATE_MAX];

	/* A candidate that would be too long is left out. */
	if (word_length + suffix_length >= sizeof(joined))
		return;

	/* The word, then the suffix. */
	memcpy(joined, word, word_length);
	memcpy(joined + word_length, suffix, suffix_length);
	(void)ja_segment_add_candidate(segment, joined, word_length + suffix_length);
}

/*
 * Adds the candidates whose word ends before a place, followed by the rest
 * of the segment (okurigana and particles) as typed.
 */
static void
segment_add_core_candidates(
	const struct ja_lexicon *lexicon,
	const struct ja_text *text,
	size_t start,
	size_t core_end,
	size_t end,
	struct ja_segment *segment)
{
	const struct ja_dict_entry *entry;
	bool ends[JA_UNITS_MAX + 1U];
	char key[JA_HEADWORD_MAX * 4U + 2U];
	const char *start_bytes;
	size_t key_length;
	size_t length;
	size_t noun_end;
	size_t i;
	size_t d;

	start_bytes = text->bytes + text->offsets[start];

	/* A noun that is the whole word. */
	key_length = text->offsets[core_end] - text->offsets[start];
	if (core_end - start <= JA_HEADWORD_MAX) {
		segment_add_nouns(lexicon, start_bytes, key_length, text->bytes + text->offsets[core_end],
				  text->offsets[end] - text->offsets[core_end], segment);
	}

	/* A verb or an adjective whose okurigana ends the word, the longest stem first. */
	for (length = core_end - start - 1U; length >= 1U; length--) {
		if (length > JA_HEADWORD_MAX)
			continue;

		key_length = text->offsets[start + length] - text->offsets[start];
		memcpy(key, start_bytes, key_length);
		for (i = 0; SEGMENT_CONSONANTS[i] != '\0'; i++) {
			key[key_length] = SEGMENT_CONSONANTS[i];

			/* The headword's okurigana must end the word. */
			memset(ends, 0, sizeof(ends));
			ja_inflect_ends(text, start + length, SEGMENT_CONSONANTS[i], ends);
			if (!ends[core_end])
				continue;

			for (d = 0; d < lexicon->dict_count; d++) {
				entry = ja_dict_find(lexicon->dicts[d], key, key_length + 1U);
				if (entry != NULL) {
					segment_add_entry(entry, text->bytes + text->offsets[start + length],
							  text->offsets[end] - text->offsets[start + length], segment);
				}
			}
		}
	}

	/* A noun with a form of する that ends the word. */
	for (noun_end = core_end - 1U; noun_end > start; noun_end--) {
		if (noun_end - start > JA_HEADWORD_MAX)
			continue;

		memset(ends, 0, sizeof(ends));
		ja_inflect_suru_ends(text, noun_end, ends);
		if (!ends[core_end])
			continue;

		segment_add_nouns(lexicon, start_bytes, text->offsets[noun_end] - text->offsets[start],
				  text->bytes + text->offsets[noun_end], text->offsets[end] - text->offsets[noun_end], segment);
	}

	/* A form of 来る, written with its kanji. */
	memset(ends, 0, sizeof(ends));
	ja_inflect_kuru_ends(text, start, ends);
	if (ends[core_end]) {
		segment_add_joined(segment, "来", strlen("来"), text->bytes + text->offsets[start + 1U],
				   text->offsets[end] - text->offsets[start + 1U]);
	}
}

/*
 * Adds a segment written in katakana up to a place and as typed after it.
 */
static void
segment_add_katakana(
	const struct ja_text *text,
	size_t start,
	size_t core_end,
	size_t end,
	struct ja_segment *segment)
{
	char written[IME_CANDIDATE_MAX];
	size_t length;
	size_t position;
	size_t used;
	size_t suffix_length;
	uint32_t code;
	uint32_t katakana;

	/* The word in katakana, character by character. */
	length = 0;
	position = text->offsets[start];
	while (position < text->offsets[core_end]) {
		used = ja_utf8_decode(text->bytes + position, text->offsets[core_end] - position, &code);
		position += used;
		if (length + 4U >= sizeof(written))
			return;

		katakana = ja_to_katakana(code);
		length += ja_utf8_encode(katakana, written + length);
	}

	/* The particles as typed. */
	suffix_length = text->offsets[end] - text->offsets[core_end];
	if (length + suffix_length >= sizeof(written))
		return;

	memcpy(written + length, text->bytes + text->offsets[core_end], suffix_length);
	(void)ja_segment_add_candidate(segment, written, length + suffix_length);
}

/*
 * Adds a span written in full-width characters.
 */
static void
segment_add_full_width(
	const struct ja_text *text,
	size_t start,
	size_t end,
	struct ja_segment *segment)
{
	char written[IME_CANDIDATE_MAX];
	size_t length;
	size_t position;
	size_t used;
	uint32_t code;
	uint32_t full;

	/* Each character in its full-width form. */
	length = 0;
	position = text->offsets[start];
	while (position < text->offsets[end]) {
		used = ja_utf8_decode(text->bytes + position, text->offsets[end] - position, &code);
		position += used;
		if (length + 4U >= sizeof(written))
			return;

		full = ja_to_full_ascii(code);
		length += ja_utf8_encode(full, written + length);
	}

	(void)ja_segment_add_candidate(segment, written, length);
}
