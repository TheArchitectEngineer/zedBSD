## zedBSD
## Copyright (C) 2026 Awe Morris
##
## SPDX-License-Identifier: Zlib
##
## Pins of plio on the original ZYBO (Rev B, XC7Z010-1CLG400).
## Values referenced: Digilent digilent-xdc Zybo-Master.xdc (MIT),
## commit c8644a9fe31cc803789643124545d56b3bef4600.

## The clock is the PS's FCLK_CLK0 after its BUFG, 100 MHz.
create_clock -period 10.000 [get_nets clk]

## Switches
set_property -dict { PACKAGE_PIN G15 IOSTANDARD LVCMOS33 } [get_ports {sw[0]}]
set_property -dict { PACKAGE_PIN P15 IOSTANDARD LVCMOS33 } [get_ports {sw[1]}]
set_property -dict { PACKAGE_PIN W13 IOSTANDARD LVCMOS33 } [get_ports {sw[2]}]
set_property -dict { PACKAGE_PIN T16 IOSTANDARD LVCMOS33 } [get_ports {sw[3]}]

## Buttons
set_property -dict { PACKAGE_PIN R18 IOSTANDARD LVCMOS33 } [get_ports {btn[0]}]
set_property -dict { PACKAGE_PIN P16 IOSTANDARD LVCMOS33 } [get_ports {btn[1]}]
set_property -dict { PACKAGE_PIN V16 IOSTANDARD LVCMOS33 } [get_ports {btn[2]}]
set_property -dict { PACKAGE_PIN Y16 IOSTANDARD LVCMOS33 } [get_ports {btn[3]}]

## LEDs
set_property -dict { PACKAGE_PIN M14 IOSTANDARD LVCMOS33 } [get_ports {led[0]}]
set_property -dict { PACKAGE_PIN M15 IOSTANDARD LVCMOS33 } [get_ports {led[1]}]
set_property -dict { PACKAGE_PIN G14 IOSTANDARD LVCMOS33 } [get_ports {led[2]}]
set_property -dict { PACKAGE_PIN D18 IOSTANDARD LVCMOS33 } [get_ports {led[3]}]
