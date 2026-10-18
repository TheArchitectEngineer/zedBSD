#ifndef PPGTT_EQUIV_PMEM_H
#define PPGTT_EQUIV_PMEM_H
#include <stdint.h>
typedef uint64_t hal_physaddr_t;
struct kern_pmem { hal_physaddr_t paddr; uint64_t size; };
int kern_pmem_alloc_limited(uint64_t size, uint64_t align, uint64_t limit, unsigned flags, struct kern_pmem *run);
int kern_pmem_free(struct kern_pmem *run);
void *kern_pmem_to_kernel(hal_physaddr_t paddr);
#endif
