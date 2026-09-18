/* Read a fixed set of GPU MMIO registers via the PCI BAR0 resource, to diff
 * Linux's effective GT/engine state against zedBSD's.  Global registers only
 * (no context selection or MCR steering). */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

int main(void){
    int fd = open("/sys/bus/pci/devices/0000:00:02.0/resource0", O_RDONLY);
    if (fd < 0){ perror("open resource0"); return 1; }
    volatile uint8_t *bar = mmap(NULL, 0x1000000, PROT_READ, MAP_SHARED, fd, 0);
    if (bar == MAP_FAILED){ perror("mmap"); return 1; }
    struct { const char *name; uint32_t off; } regs[] = {
        {"GLOBAL_MOCS2   0x4008", 0x4008},
        {"GLOBAL_MOCS3   0x400c", 0x400c},
        {"LNCFCMOCS1     0xb024", 0xb024},
        {"CMD_CCTL       0x20c4", 0x20c4},
        {"CMD_BUF_CCTL   0x2084", 0x2084},
        {"MI_MODE        0x209c", 0x209c},
        {"CONTEXT_CONTROL0x2244", 0x2244},
        {"CS_CHICKEN1    0x2580", 0x2580},
        {"GFX_MODE       0x229c", 0x229c},
        {"MISCCPCTL      0x9424", 0x9424},
        {"DFR_CHICKEN    0x9550", 0x9550},
        {"L3ALLOC        0xb134", 0xb134},
        {"L3SQCREG1      0xb100", 0xb100},
        {"L3SQCREG4      0xb118", 0xb118},
        {"MCR_SELECTOR   0xfdc",  0x0fdc},
        {"ROW_CHICKEN2   0xe4f4", 0xe4f4},
        {"ROW_CHICKEN4   0xe48c", 0xe48c},
        {"SAMPLER_MODE   0xe18c", 0xe18c},
        {"GT_MODE        0x7008", 0x7008},
        {"RCU_MODE       0x14800",0x14800},
        {0,0}
    };
    for (int i=0; regs[i].name; i++)
        printf("MMIO %s = 0x%08x\n", regs[i].name, *(volatile uint32_t *)(bar + regs[i].off));
    return 0;
}
