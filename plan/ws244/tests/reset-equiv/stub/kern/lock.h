#ifndef RESET_EQUIV_LOCK_H
#define RESET_EQUIV_LOCK_H
struct spinlock { int held; };
struct mutex { int held; };
unsigned long spin_lock_irqsave(struct spinlock *l);
void spin_unlock_irqrestore(struct spinlock *l, unsigned long irq);
#endif
