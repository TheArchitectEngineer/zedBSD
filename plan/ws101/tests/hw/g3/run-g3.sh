#!/bin/sh
# ws101-p011: G3 on the 5330: the demonstration's script (s13.sh: the CPU run, the GPU run, the times side by
# side), then Noct's program on its own over N = 4,000,000 in 8 calls on the CPU and on the GPU (libGLESv2 writes a
# line per dispatch), each in its log on the disk; the compositor's processes after each run; the kernel's messages.
run() {
	log=$1
	shift
	/bin/timeout -s KILL "$@" >> "$log" 2>&1 < /dev/null
	echo "RUN exit=$? $*" >> "$log"
	ps -A -o pid,args | grep '/bin/[w]ayland' >> /var/log/g3-ps.log
	echo "== after $log" >> /var/log/g3-ps.log
	sync
}
export KEI_GLES_COMPUTE_TRACE=1
ps -A -o pid,args | grep '/bin/[w]ayland' > /var/log/g3-ps.log
echo "== before" >> /var/log/g3-ps.log
run /var/log/g3-s13.log 120 /bin/sh /usr/share/gpudemo/s13.sh
run /var/log/g3-cpu.log 60 /bin/noct -O2 -j --gc-tenure-size=100000000 /usr/share/gpudemo/mix.nct 4000000 8
run /var/log/g3-gpu.log 60 /bin/noct -O2 -j --gc-tenure-size=100000000 --gpu /usr/share/gpudemo/mix.nct 4000000 8
echo "G3 HW DONE" >> /var/log/g3-ps.log
dmesg > /var/log/dmesg.log 2>&1
sync
