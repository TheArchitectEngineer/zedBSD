/* WS031 Linux-parity -- OP-NOTIFY GPU-free tests (opregion_ktest.c).  zedBSD project code. */
#ifndef PARITY_OPREGION_KTEST_H
#define PARITY_OPREGION_KTEST_H

typedef void (*parity_opregion_ktest_check)(int ok, const char *msg);
void parity_opregion_ktest(parity_opregion_ktest_check check);

#endif
