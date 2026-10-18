#include <stdbool.h>
#include <stdint.h>
bool kern_rtc_read_counter(uint64_t *counter, uint64_t *frequency_hz);
void kern_usleep_range(unsigned min_us, unsigned max_us);
