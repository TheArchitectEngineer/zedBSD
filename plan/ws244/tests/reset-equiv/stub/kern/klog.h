#define kern_logf(...) reset_equiv_log(__VA_ARGS__)
void reset_equiv_log(const char *fmt, ...);
