#!/bin/sh
# ws036-p028: runs Noct on the Raspberry Pi 4 image in QEMU raspi4b, with the
# JIT and with the interpreter only, and a smoke of the file, process and
# shell APIs.  The image must carry /bin/noct.  Each command is kept short:
# the serial console reads one line at a time.
#   sh plan/tools/rpi4/noct-rpi4.sh build/rpi4-p028/hdd-image.img
set -e
IMAGE=${1:?image}
FIB='func fib(n) { if (n < 2) { return n; } return fib(n - 1) + fib(n - 2); }\nfunc main(args) { var s = 0; for (i in 0..200000) { s = s + i %% 7; } print("fib=" + fib(24) + " sum=" + s); }\n'
IO='func main(a) { FileUtil.writeText("/tmp/o", "file-ok "); var c = Process.spawn(["/bin/sh", "-c", "printf process-ok"]); var o = "";\n'
IO2='while (Process.isAlive(c) == 1) { o = o + Process.read(c, 100); } o = o + Process.read(c, 0); Process.wait(c); print(FileUtil.readText("/tmp/o") + o + " shell=" + System.shell("true")); }\n'
exec sh plan/tools/guest/rpi4-serial.sh "$IMAGE" \
	"printf '$FIB' > /tmp/fib.noct" \
	"noct -j /tmp/fib.noct && noct -j0 /tmp/fib.noct" \
	"printf '$IO' > /tmp/io.noct" \
	"printf '$IO2' >> /tmp/io.noct" \
	"noct /tmp/io.noct"
