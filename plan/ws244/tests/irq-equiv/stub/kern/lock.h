#ifndef IRQ_EQUIV_LOCK_H
#define IRQ_EQUIV_LOCK_H
#define LOCK_RANK_DEVICE 0
struct spinlock { int held; };
void spin_init(struct spinlock *l, int rank, const char *name);
unsigned long spin_lock_irqsave(struct spinlock *l);
void spin_unlock_irqrestore(struct spinlock *l, unsigned long irq);
#endif
