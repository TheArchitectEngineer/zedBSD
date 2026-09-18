/* WS031 Linux-parity OS adaptation layer — execution trace (see trace.h). */
#include "trace.h"

void
osdep_trace_init(struct osdep_trace *t)
{
	uint32_t i;

	t->next = 0u;
	t->seq = 0u;
	t->dropped = 0u;
	t->wrapped = 0;
	for (i = 0u; i < OSDEP_TRACE_CAPACITY; i++) {
		t->records[i].seq = 0u;
		t->records[i].stage = 0u;
		t->records[i].op = OSDEP_TR_NOTE;
		t->records[i].what = 0;
		t->records[i].arg0 = 0u;
		t->records[i].arg1 = 0u;
	}
}

void
osdep_trace_emit(struct osdep_trace *t, uint16_t stage, uint16_t op,
		 const char *what, uint64_t arg0, uint64_t arg1)
{
	struct osdep_trace_record *r;

	r = &t->records[t->next];
	/* Overwriting a record that was never read is data loss: count it. */
	if (t->wrapped)
		t->dropped++;

	r->seq = t->seq;
	r->stage = stage;
	r->op = op;
	r->what = what;
	r->arg0 = arg0;
	r->arg1 = arg1;

	t->seq++;
	t->next++;
	if (t->next == OSDEP_TRACE_CAPACITY) {
		t->next = 0u;
		t->wrapped = 1;
	}
}

uint32_t
osdep_trace_count(const struct osdep_trace *t)
{
	if (t->wrapped)
		return OSDEP_TRACE_CAPACITY;
	return t->next;
}

uint32_t
osdep_trace_snapshot(const struct osdep_trace *t,
		     struct osdep_trace_record *out, uint32_t max)
{
	uint32_t count;
	uint32_t start;
	uint32_t i;

	count = osdep_trace_count(t);
	if (count > max)
		count = max;

	/* Oldest live record: if wrapped, it is at next; otherwise at 0. */
	start = t->wrapped ? t->next : 0u;
	for (i = 0u; i < count; i++)
		out[i] = t->records[(start + i) % OSDEP_TRACE_CAPACITY];
	return count;
}

const char *
osdep_trace_op_name(uint16_t op)
{
	switch (op) {
	case OSDEP_TR_ENTRY:       return "entry";
	case OSDEP_TR_EXIT:        return "exit";
	case OSDEP_TR_ACQUIRE:     return "acquire";
	case OSDEP_TR_RELEASE:     return "release";
	case OSDEP_TR_MAP:         return "map";
	case OSDEP_TR_UNMAP:       return "unmap";
	case OSDEP_TR_SYNC_DEV:    return "sync_for_device";
	case OSDEP_TR_SYNC_CPU:    return "sync_for_cpu";
	case OSDEP_TR_WORK_ENQ:    return "work_enqueue";
	case OSDEP_TR_WORK_BEGIN:  return "work_begin";
	case OSDEP_TR_WORK_END:    return "work_end";
	case OSDEP_TR_WORK_CANCEL: return "work_cancel";
	case OSDEP_TR_UNIMPL:      return "unimplemented";
	case OSDEP_TR_FAIL:        return "fail";
	case OSDEP_TR_NOTE:        return "note";
	default:                   return "?";
	}
}
