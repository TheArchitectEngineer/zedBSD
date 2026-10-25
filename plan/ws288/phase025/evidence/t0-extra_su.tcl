# Extra SU reports: the sb issue path, opd's operand select and its fanout.
set P $::env(SUP_PREFIX)
set g [string map {[ ? ] ?} $P]
set fx [open ${pre}-su-extra.psv w]
puts $fx "what|slack|delay|logic|net|levels|start|end|maxfanout|net"
foreach {what from to} [list \
	sb-from "${g}sb/*" "" \
	sb-to "" "${g}sb/*" \
	sched-to-opd "${g}sched/*" "${g}opd/*" \
	sb-to-sched "${g}sb/*" "${g}sched/*" \
	dec-to-sb "${g}dec/*" "${g}sb/*" \
	opd-from "${g}opd/*" "" \
	opd-to "" "${g}opd/*" \
	opd-to-fpu "${g}opd/*" "${g}fpu/*" \
	opd-to-alu "${g}opd/*" "${g}alu/*" \
	rf-to-opd "${g}rf/*" "${g}opd/*" \
	sched-to-sched "${g}sched/*" "${g}sched/*" \
	wb-to-rf "${g}wb/*" "${g}rf/*" ] {
	set args [list -quiet -max_paths 1 -nworst 1 -setup]
	if {$from ne ""} { set r [filter [all_registers] "NAME =~ $from"]; if {[llength $r]==0} {puts $fx "$what|no regs"; continue}; lappend args -from $r }
	if {$to ne ""} { set r [filter [all_registers] "NAME =~ $to"]; if {[llength $r]==0} {puts $fx "$what|no regs"; continue}; lappend args -to $r }
	set p [get_timing_paths {*}$args]
	if {[llength $p] == 0} { puts $fx "$what|no path"; continue }
	puts $fx [pathrow $what $p]
	report_timing -of_objects $p -file ${pre}-path-$what.rpt
}
close $fx
