/*
 * Host shim for the DesignWare Ethernet QoS check: the kernel's
 * kern/thread.h pulls in the kernel's signal and type headers, which
 * clash with the host's.  The driver needs only these two calls; the
 * check supplies them.  Production builds use include/kern/thread.h.
 */
#ifndef KERN_WS256_DW_EQOS_HOST_THREAD_H
#define KERN_WS256_DW_EQOS_HOST_THREAD_H

struct thread;

int kthread_create(void (*entry)(void *), void *arg, int priority, struct thread **result);
void thread_start(struct thread *thread);

#endif
