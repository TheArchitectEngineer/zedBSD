#ifndef IRQ_EQUIV_WAITQ_H
#define IRQ_EQUIV_WAITQ_H
#include <stdint.h>
struct spinlock;
struct wait_queue { int unused; };
void waitq_init(struct wait_queue *q, const char *name);
uint64_t waitq_sequence(struct wait_queue *q);
int waitq_sleep(struct wait_queue *q, struct spinlock *l, uint64_t observed, uint64_t deadline, unsigned flags);
void waitq_wake_all(struct wait_queue *q);
#endif
