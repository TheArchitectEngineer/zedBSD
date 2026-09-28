/* Host fixture shim; production builds use include/kern/thread.h. */
#ifndef KERN_WS004_HOST_THREAD_H
#define KERN_WS004_HOST_THREAD_H

struct thread;
struct thread *thread_current(void);

#endif
