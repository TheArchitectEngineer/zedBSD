# board_synth.tcl ROOT TOPV TOPDIR OUT PART IPDIR: synthesis only (no P&R) of the board top,
# the same reads as tools/vivado-build.tcl; reports after synth_design and after opt_design.
lassign $argv root topv topdir out part ipdir
set top [file rootname [file tail $topv]]
if {[info exists ::env(VIVADO_THREADS)]} { set_param general.maxThreads $::env(VIVADO_THREADS) }
set_part $part
proc rtl_files {dir} {
	set files [glob -nocomplain -directory $dir *.v]
	foreach sub [glob -nocomplain -type d -directory $dir *] { set files [concat $files [rtl_files $sub]] }
	return [lsort $files]
}
read_verilog [concat [rtl_files [file join $root rtl]] [list $topv] \
	[lsort [glob -nocomplain -directory [file join $topdir lib] *.v]]]
read_xdc [file join $topdir $top.xdc]
foreach xci [lsort [glob -directory $ipdir */*.xci]] { read_ip $xci }
foreach image [glob -nocomplain -directory [file join $root fw cp images] *.hex] { file copy -force $image $out }
synth_design -top $top -part $part -include_dirs [list [file join $root include]]
report_utilization -file [file join $out synth_utilization.rpt]
report_utilization -hierarchical -hierarchical_depth 4 -file [file join $out synth_utilization_hier.rpt]
write_checkpoint -force [file join $out $top.synth.dcp]
opt_design
report_utilization -file [file join $out opt_utilization.rpt]
report_utilization -hierarchical -hierarchical_depth 4 -file [file join $out opt_utilization_hier.rpt]
report_timing_summary -max_paths 10 -file [file join $out opt_timing_summary.rpt]
report_clock_interaction -file [file join $out opt_clock_interaction.rpt]
write_checkpoint -force [file join $out $top.opt.dcp]
puts "board_synth: done"
