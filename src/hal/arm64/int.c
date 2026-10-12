#include <hal/hal.h>
#include "asm.h"
#include "int.h"
#include "task.h"
#include "irq.h"
#include "watch.h"
#include "bsp-rpi4/gic.h"
#include <uapi/errno.h>

extern char arm64_vectors[];
void rpi4_timer_init(void);

void arm64_int_init(void)
{
	__asm__ volatile("msr vbar_el1, %0\n\tisb"::"r"(arm64_vectors):"memory");
	rpi4_gic_init();
	rpi4_timer_init();
	hal_puts("ARM64 EXCEPTION PASS\nARM64 IRQ READY\n");
}

/*
 * Prepares a secondary CPU's exception vectors and its part of the GIC.
 *
 * The distributor's shared lines were set up by the boot CPU; the
 * secondary's scheduling tick is started separately, once it has a task.
 */
void
arm64_int_init_secondary(
	void)
{
	/* Points this CPU's exceptions at the shared vector table. */
	__asm__ volatile("msr vbar_el1, %0\n\tisb" : : "r"(arm64_vectors) : "memory");

	/* Sets up this CPU's private lines and CPU interface. */
	rpi4_gic_init_cpu();
}

/* Maps an exception class onto the generic cause, mode and address. */
static void
arm64_classify(const struct arm64_exception_frame *f, uint32_t ec,
    int *cause, int *mode, uintptr_t *address)
{
	*mode = HAL_TRAP_MODE_NONE;
	*address = 0;

	/*
	 * Only an abort whose fault status is a translation, access flag or
	 * permission fault (0x04-0x0f) is a page fault the address space can
	 * resolve.  An alignment fault (0x21) and any other abort would fault
	 * again if the instruction were retried, so they become signals.
	 */
	if ((ec == 0x24 || ec == 0x25) && (f->esr & 0x3fULL) == 0x21U) {
		*cause = HAL_TRAP_CAUSE_ALIGNMENT;
		*address = (uintptr_t)f->far;
	} else if ((ec == 0x20 || ec == 0x21 || ec == 0x24 || ec == 0x25) &&
	    ((f->esr & 0x3fULL) < 0x04U || (f->esr & 0x3fULL) > 0x0fU)) {
		*cause = HAL_TRAP_CAUSE_OTHER;
		*address = (uintptr_t)f->far;
	} else if (ec == 0x20 || ec == 0x21 || ec == 0x24 || ec == 0x25) {
		*cause = HAL_TRAP_CAUSE_PAGE_FAULT;
		*address = (uintptr_t)f->far;
		if (ec == 0x20 || ec == 0x21)
			*mode = HAL_TRAP_MODE_EXEC;
		else if (f->esr & (1ULL << 6))
			*mode = HAL_TRAP_MODE_WRITE;
		else
			*mode = HAL_TRAP_MODE_READ;
	} else if (ec == 0x3c) {
		*cause = HAL_TRAP_CAUSE_BREAKPOINT;
	} else if (ec == 0x32) {
		/* A software step from EL0: one instruction has run. */
		*cause = HAL_TRAP_CAUSE_SINGLE_STEP;
	} else if (ec == 0x30) {
		/* A hardware breakpoint from EL0, before the instruction runs. */
		*cause = HAL_TRAP_CAUSE_DEBUG_POINT;
		*mode = HAL_TRAP_MODE_EXEC;
		*address = (uintptr_t)f->elr;
	} else if (ec == 0x34) {
		/* A watchpoint from EL0: FAR holds the address, WnR the direction. */
		*cause = HAL_TRAP_CAUSE_DEBUG_POINT;
		*address = (uintptr_t)f->far;
		if (f->esr & (1ULL << 6))
			*mode = HAL_TRAP_MODE_WRITE;
		else
			*mode = HAL_TRAP_MODE_READ;
	} else if (ec == 0x22 || ec == 0x26) {
		*cause = HAL_TRAP_CAUSE_ALIGNMENT;
		*address = (uintptr_t)f->far;
	} else if (ec == 0) {
		*cause = HAL_TRAP_CAUSE_ILLEGAL_INSN;
	} else {
		*cause = HAL_TRAP_CAUSE_OTHER;
	}
}

void arm64_sync_handler(struct arm64_exception_frame *f,uint64_t vector)
{
	uint32_t ec=(uint32_t)((f->esr>>26)&0x3f);
	int cause;
	int mode;
	uintptr_t address;

	if (vector == 8 && ec == 0x15) {
		uintptr_t args[HAL_SYSCALL_ARGS];
		unsigned i;

		for (i = 0; i < HAL_SYSCALL_ARGS; i++)
			args[i] = (uintptr_t)f->x[i];
		arm64_task_enter_user_frame(f);
		/* The generic kernel owns accounting and its interruptible window. */
		f->x[0] = (uint64_t)kernel_syscall_handler((uint32_t)f->x[8], args);
		kernel_user_return_handler();
		arm64_task_leave_user_frame();
		return;
	}
	arm64_classify(f, ec, &cause, &mode, &address);
	if (vector == 0) {
		if (kernel_sys_fault_handler(cause, mode, (uintptr_t)f->elr,
		    address, ec, (uintptr_t)f->esr) == HAL_TRAP_RET_SUCCESS)
			return;
	}
	if (vector == 8) {
		int handled;
		arm64_task_enter_user_frame(f);
		handled = kernel_user_fault_handler(cause, mode, (uintptr_t)f->elr,
		    address, ec, (uintptr_t)f->esr) == HAL_TRAP_RET_SUCCESS;
		if (handled) {
			kernel_user_return_handler();
			arm64_task_leave_user_frame();
			return;
		}
		arm64_task_leave_user_frame();
		HAL_FATAL("AArch64 user fault handler returned");
	}
	hal_printf("ARM64 sync vector=%u ec=%x esr=%llx elr=%llx far=%llx\n",
	    (uint32_t)vector,ec,f->esr,f->elr,f->far);

	/* Keeps the syndrome and the addresses for the stall watch's screen, then stops. */
	arm64_watch_fault(f->esr, f->elr, f->far);
	HAL_FATAL("unhandled AArch64 synchronous exception");
}

/*
 * Takes one interrupt from the GIC and hands it on.
 *
 * The stall watch is told which interrupt this CPU serves, since when and
 * what it interrupted; the interrupted code had interrupts on, so any
 * older note that they were off is stale and is dropped.
 */
void
arm64_irq_handler(
	struct arm64_exception_frame *f,
	int from_user)
{
	struct arm64_percpu *cpu;
	uint64_t now;
	uint32_t iar;
	uint32_t id;

	/* A user context is recorded before the kernel sees the interrupt. */
	if (from_user)
		arm64_task_enter_user_frame(f);

	/* Acknowledges the interrupt and reads its number. */
	iar = rpi4_gic_ack();
	id = iar & 0x3ffU;

	/* Tells the stall watch what this CPU serves now. */
	cpu = arm64_watch_record();
	now = arm64_watch_counter();
	__atomic_store_n(&cpu->watch_irq_off_time, 0U, __ATOMIC_RELAXED);
	__atomic_store_n(&cpu->watch_irq_elr, f->elr, __ATOMIC_RELAXED);
	__atomic_store_n(&cpu->watch_irq_time, now, __ATOMIC_RELAXED);
	__atomic_store_n(&cpu->watch_irq_id, (uint64_t)id + 1U, __ATOMIC_RELAXED);

	/* A real interrupt goes to its handler; a spurious one (1020-1023) is nothing. */
	if (id < 1020U)
		arm64_irq_dispatch(id, (hal_irq_ack_t)iar + 1U);

	/* The interrupt is served; a switch inside it already cleared this. */
	cpu = arm64_watch_record();
	__atomic_store_n(&cpu->watch_irq_id, 0U, __ATOMIC_RELAXED);

	/* A user context goes back through the kernel's return path. */
	if (from_user) {
		kernel_user_return_handler();
		arm64_task_leave_user_frame();
	}
}
