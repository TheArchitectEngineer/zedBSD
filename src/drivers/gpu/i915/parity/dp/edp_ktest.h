/* WS031 Linux-parity — GPU-free kernel checks of the eDP first stage (edp_ktest.c). */
#ifndef PARITY_EDP_KTEST_H
#define PARITY_EDP_KTEST_H

typedef void (*parity_edp_ktest_check)(int ok, const char *msg);
void parity_edp_ktest(parity_edp_ktest_check check);

#endif /* PARITY_EDP_KTEST_H */
