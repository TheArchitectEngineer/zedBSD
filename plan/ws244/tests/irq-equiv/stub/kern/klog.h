#define kern_logf(...) irq_equiv_log(__VA_ARGS__)
void irq_equiv_log(const char *fmt, ...);
