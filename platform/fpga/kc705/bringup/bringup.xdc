## zedBSD
## Copyright (C) 2026 Awe Morris
##
## SPDX-License-Identifier: Zlib
##
## Pins of bringup on the Xilinx KC705 (XC7K325T-2FFG900C).
## Values referenced: AMD XilinxBoardStore board file kc705 revision 1.7,
## part0_pins.xml (Apache-2.0), as shipped with Vivado 2026.1 under
## data/xhub/boards/XilinxBoardStore/boards/Xilinx/kc705/1.7/
## (SHA-256 4ddcee4ae1bb3822e2c3a3d6962c81931c0157e265b7b436aaa468bd82ea3948).
## The pin names in the comments are that file's.  Read by Vivado and by
## openXC7's nextpnr (which ignores the commands it does not know).

## SYSCLK_P/N, the 200 MHz LVDS oscillator (SiT9102) on bank 33 (1.5 V).
set_property -dict { PACKAGE_PIN AD12 IOSTANDARD DIFF_SSTL15 } [get_ports sysclk_p]
set_property -dict { PACKAGE_PIN AD11 IOSTANDARD DIFF_SSTL15 } [get_ports sysclk_n]
create_clock -name sysclk -period 5.000 [get_ports sysclk_p]

## CPU_RESET, active high.
set_property -dict { PACKAGE_PIN AB7 IOSTANDARD LVCMOS15 } [get_ports cpu_reset]

## GPIO_DIP_SW0..3
set_property -dict { PACKAGE_PIN Y29 IOSTANDARD LVCMOS25 } [get_ports {dip[0]}]
set_property -dict { PACKAGE_PIN W29 IOSTANDARD LVCMOS25 } [get_ports {dip[1]}]
set_property -dict { PACKAGE_PIN AA28 IOSTANDARD LVCMOS25 } [get_ports {dip[2]}]
set_property -dict { PACKAGE_PIN Y28 IOSTANDARD LVCMOS25 } [get_ports {dip[3]}]

## GPIO_SW_N, _E, _S, _W, _C
set_property -dict { PACKAGE_PIN AA12 IOSTANDARD LVCMOS15 } [get_ports btn_n]
set_property -dict { PACKAGE_PIN AG5 IOSTANDARD LVCMOS15 } [get_ports btn_e]
set_property -dict { PACKAGE_PIN AB12 IOSTANDARD LVCMOS15 } [get_ports btn_s]
set_property -dict { PACKAGE_PIN AC6 IOSTANDARD LVCMOS15 } [get_ports btn_w]
set_property -dict { PACKAGE_PIN G12 IOSTANDARD LVCMOS25 } [get_ports btn_c]

## GPIO_LED_0_LS..7_LS
set_property -dict { PACKAGE_PIN AB8 IOSTANDARD LVCMOS15 } [get_ports {led[0]}]
set_property -dict { PACKAGE_PIN AA8 IOSTANDARD LVCMOS15 } [get_ports {led[1]}]
set_property -dict { PACKAGE_PIN AC9 IOSTANDARD LVCMOS15 } [get_ports {led[2]}]
set_property -dict { PACKAGE_PIN AB9 IOSTANDARD LVCMOS15 } [get_ports {led[3]}]
set_property -dict { PACKAGE_PIN AE26 IOSTANDARD LVCMOS25 } [get_ports {led[4]}]
set_property -dict { PACKAGE_PIN G19 IOSTANDARD LVCMOS25 } [get_ports {led[5]}]
set_property -dict { PACKAGE_PIN E18 IOSTANDARD LVCMOS25 } [get_ports {led[6]}]
set_property -dict { PACKAGE_PIN F16 IOSTANDARD LVCMOS25 } [get_ports {led[7]}]

## TCK of the BSCANE2 (the Digilent cable runs at most 30 MHz).
create_clock -name tck -period 33.333 [get_nets tck_raw]

## The two domains share only quasi-static or Gray-coded values (bringup.v).
set_clock_groups -asynchronous -group sysclk -group tck

## The inputs are asynchronous and the LEDs are slow.
set_false_path -from [get_ports {cpu_reset dip[*] btn_*}]
set_false_path -to [get_ports {led[*]}]

