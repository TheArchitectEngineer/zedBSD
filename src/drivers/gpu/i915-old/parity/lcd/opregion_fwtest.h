/* WS031 E-123: the OpRegion service on the real OpRegion (-DPARITY_OPREGION_FW_TEST=1).  zedBSD project code. */
#ifndef PARITY_OPREGION_FWTEST_H
#define PARITY_OPREGION_FWTEST_H

#include <stdint.h>

struct parity_vbt_state;
/* runs only when ASLS != 0; returns 0 when every step matched, -1 otherwise (the probe continues either way) */
int parity_opregion_fw_test(uint32_t asls, const struct parity_vbt_state *vbt);
/* the summary once more (the runner's end), when the test ran */
void parity_opregion_fw_log_again(void);

#endif
