# plio — the first PS–PL block of the original ZYBO

`plio` is a small AXI3 slave on the Zynq PS's `M_AXI_GP0` port, for the
original Digilent ZYBO (Rev B, XC7Z010-1CLG400). zedBSD uses it to check the
PL path end to end: configuring the PL, opening the level shifters, register
reads and writes over GP0, and the PL-to-PS interrupt.

- `plio.v` — the RTL (the `PS7` primitive, the slave and its registers; the
  register table is at the top of the file).
- `plio.xdc` — the ZYBO's LED, switch and button pins and the FCLK_CLK0
  clock constraint.

The window is GP0 at `0x43C00000`, 64 KiB; only the low 16 address bits are
decoded. The clock is FCLK_CLK0 (100 MHz as `ps7_init` sets it), the reset
FCLK_RESET0_N. `IRQ_F2P[0]` is the block's interrupt.

The block is not built by zedBSD's `make`: it needs a host FPGA toolchain.

- openXC7 (yosys, nextpnr-himbaechel, fpga-as):
  `plan/tools/zybo/pl-build.sh platform/armv7/zybo/pl/plio` writes
  `build/zybo-pl/plio/plio.bit`.
- The host simulation: `platform/armv7/zybo/pl/tests/plio-tb.sh` (Icarus Verilog).
- Over JTAG, with SPL holding the PS: `plan/tools/zybo/pl-jtag.sh plio.bit`,
  then OpenOCD's `zybo_pl_enable` (`plan/tools/zybo/openocd-zybo.cfg`).

The pins are from Digilent's `Zybo-Master.xdc` (MIT).
