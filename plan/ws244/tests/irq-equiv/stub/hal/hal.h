#ifndef IRQ_EQUIV_HAL_H
#define IRQ_EQUIV_HAL_H
#define HAL_OK 0
typedef void *hal_irq_ack_t;
typedef void (*hal_irq_handler_t)(int vector, hal_irq_ack_t acknowledge, void *argument);
int hal_irq_attach_msi(int irq, hal_irq_handler_t handler, void *argument);
int hal_irq_detach_msi_sync(int irq, hal_irq_handler_t handler, void *argument);
void hal_irq_send_eoi(hal_irq_ack_t acknowledge);
#endif
