# analyze.tcl DCP OUTPREFIX CELL... : per-cell worst setup path (and its depth-1 children)
# with logic/net split and the largest fanout on the path; extra path reports.
lassign $argv dcp pre
set cells [lrange $argv 2 end]
if {[info exists ::env(VIVADO_THREADS)]} { set_param general.maxThreads $::env(VIVADO_THREADS) }
open_checkpoint $dcp
if {[info exists ::env(IODELAY)]} {
	# Standalone module: time the paths from and to its ports as register boundaries (0 ns outside).
	set_input_delay -clock clk 0 [filter [all_inputs] {NAME != clk}]
	set_output_delay -clock clk 0 [all_outputs]
}
proc pathrow {name p} {
	set fo 0; set fonet ""
	foreach n [get_nets -quiet -of_objects $p] {
		set c [get_property FLAT_PIN_COUNT $n]
		if {$c > $fo} { set fo $c; set fonet $n }
	}
	set v {}
	foreach k {SLACK DATAPATH_DELAY DATAPATH_LOGIC_DELAY DATAPATH_NET_DELAY} {
		set x [get_property $k $p]
		if {$x eq ""} { lappend v unconstrained } else { lappend v [format %.3f $x] }
	}
	return [join [list $name {*}$v [get_property LOGIC_LEVELS $p] [get_property STARTPOINT_PIN $p] \
		[get_property ENDPOINT_PIN $p] [expr {$fo - 1}] $fonet] |]
}
set f [open ${pre}-blocks.psv w]
puts $f "cell|slack|delay|logic|net|levels|start|end|maxfanout|net"
set all {}
foreach c $cells {
	lappend all $c
	foreach ch [lsort [get_cells -quiet -filter {IS_PRIMITIVE == 0} "$c/*"]] {
		set n [get_property NAME $ch]
		if {[string first "/" [string range $n [string length $c] end]] >= 0} {
			# keep names that are direct children
		}
		lappend all [string map {[ ? ] ?} $n]
	}
}
foreach c [lsort -unique $all] {
	set regs [filter [all_registers] "NAME =~ $c/*"]
	if {[llength $regs] == 0} { continue }
	set p [get_timing_paths -quiet -max_paths 1 -nworst 1 -setup -to $regs]
	if {[llength $p] == 0} { puts $f "$c|no path"; continue }
	puts $f [pathrow $c $p]
}
if {[info exists ::env(IODELAY)]} {
	set p [get_timing_paths -quiet -max_paths 1 -nworst 1 -setup -from [filter [all_inputs] {NAME != clk}]]
	if {[llength $p]} { puts $f [pathrow port-in $p] }
	set p [get_timing_paths -quiet -max_paths 1 -nworst 1 -setup -to [all_outputs]]
	if {[llength $p]} { puts $f [pathrow port-out $p] }
	set p [get_timing_paths -quiet -max_paths 1 -nworst 1 -setup]
	puts $f [pathrow all $p]
	report_timing_summary -max_paths 10 -file ${pre}-io-timing_summary.rpt
}
close $f
report_timing -max_paths 10 -nworst 1 -sort_by slack -file ${pre}-worst10.rpt
report_high_fanout_nets -max_nets 30 -timing -file ${pre}-fanout.rpt
if {[info exists ::env(EXTRA)]} { source $::env(EXTRA) }
