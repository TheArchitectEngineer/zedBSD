# Vivado non-project batch build of one RTL directory (NAME/NAME.v, other
# *.v, NAME.xdc), for the KC705 by default.  Called by vivado-build.sh:
#   vivado -mode batch -source vivado-build.tcl -tclargs RTL_DIR OUT_DIR [PART]
# Writes OUT_DIR/NAME.bit, NAME.dcp (routed), utilization.rpt,
# timing_summary.rpt, drc.rpt.  No project, no IP, nothing in the tree.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

if {[llength $argv] < 2} {
	puts "usage: vivado-build.tcl RTL_DIR OUT_DIR \[PART\]"
	exit 2
}
set rtl [file normalize [lindex $argv 0]]
set out [file normalize [lindex $argv 1]]
set part xc7k325tffg900-2
if {[llength $argv] >= 3} {
	set part [lindex $argv 2]
}
set name [file tail $rtl]
file mkdir $out

read_verilog [glob -directory $rtl *.v]
read_xdc [file join $rtl $name.xdc]

synth_design -top $name -part $part
opt_design
place_design
phys_opt_design
route_design

report_utilization -file [file join $out utilization.rpt]
report_timing_summary -max_paths 10 -file [file join $out timing_summary.rpt]
report_drc -file [file join $out drc.rpt]
write_checkpoint -force [file join $out $name.dcp]
write_bitstream -force [file join $out $name.bit]

set wns [get_property SLACK [get_timing_paths -max_paths 1 -nworst 1 -setup]]
set whs [get_property SLACK [get_timing_paths -max_paths 1 -nworst 1 -hold]]
puts "vivado-build: $name on $part WNS $wns ns WHS $whs ns"
