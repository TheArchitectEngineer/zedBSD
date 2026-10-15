#!/bin/sh
D=/tmp/t690on2; mkdir -p $D
export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0
V=/tmp/ws236
mark() { echo $(wc -l < /run/user/1000/session.log) $(echo kei | sudo -S dmesg 2>/dev/null | wc -l) > $D/mark.$1; }
slice() { set -- $1 $(cat $D/mark.$1)
  tail -n +$(($2+1)) /run/user/1000/session.log > $D/comp-$1.log
  echo kei | sudo -S dmesg 2>/dev/null | tail -n +$(($3+1)) > $D/dmesg-$1.log; }
run() { n=$1; shot=$2; shift 2
  mark $n
  ps -e -o pid,time,comm > $D/ps-$n-pre.txt; sysctl hw.cputimes > $D/cpu-$n-pre.txt
  env $VENV videoplayer "$@" > $D/vp-$n.log 2>&1 &
  pid=$!
  if [ $shot = 1 ]; then sleep 5; keiland-shot /tmp/t690on2-$n.png > $D/shot-$n.txt 2>&1; cp /tmp/t690-$n.png $D/ 2>/dev/null; fi
  wait $pid; echo $? > $D/rc-$n.txt
  ps -e -o pid,time,comm > $D/ps-$n-post.txt; sysctl hw.cputimes > $D/cpu-$n-post.txt
  sleep 2; slice $n; echo "$n done"; }
run b60full 0 --timeout-s=15 --fullscreen $V/high-1080p60-10s.mp4
run b60win 0 --timeout-s=15 $V/high-1080p60-10s.mp4
run p60full 0 --timeout-s=15 --fullscreen $V/high-720p60-10s.mp4
run p60win 0 --timeout-s=15 $V/high-720p60-10s.mp4
run p30full 0 --timeout-s=15 --fullscreen $V/high-720p30-10s.mp4
run b60full 0 --timeout-s=15 --fullscreen $V/high-1080p60-10s.mp4
run b60win 0 --timeout-s=15 $V/high-1080p60-10s.mp4
echo kei | sudo -S dmesg > $D/dmesg-all.log 2>/dev/null
echo ALLDONE > $D/done
