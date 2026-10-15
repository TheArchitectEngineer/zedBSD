#!/bin/sh
D=/tmp/t690; mkdir -p $D
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
  if [ $shot = 1 ]; then sleep 5; keiland-shot /tmp/t690-$n.png > $D/shot-$n.txt 2>&1; cp /tmp/t690-$n.png $D/ 2>/dev/null; fi
  wait $pid; echo $? > $D/rc-$n.txt
  ps -e -o pid,time,comm > $D/ps-$n-post.txt; sysctl hw.cputimes > $D/cpu-$n-post.txt
  sleep 2; slice $n; echo "$n done"; }
run b60full 0 --timeout-s=15 --fullscreen $V/high-1080p60-10s.mp4
run b60win 0 --timeout-s=15 $V/high-1080p60-10s.mp4
run b30full 1 --timeout-s=15 --fullscreen $V/high-1080p30-10s.mp4
run b30win 0 --timeout-s=15 $V/high-1080p30-10s.mp4
run p60full 0 --timeout-s=15 --fullscreen $V/high-720p60-10s.mp4
run p60win 0 --timeout-s=15 $V/high-720p60-10s.mp4
run p30full 0 --timeout-s=15 --fullscreen $V/high-720p30-10s.mp4
VENV=VULKAN_ZEDBSD_DIRECT=0 run p60fullcopy 0 --timeout-s=15 --fullscreen $V/high-720p60-10s.mp4
VENV=VULKAN_ZEDBSD_DIRECT=0 run p60wincopy 0 --timeout-s=15 $V/high-720p60-10s.mp4
VENV=
# (c) apps
mark apps
for a in "settings --timeout-s=8" "settings --timeout-s=8 --width=900 --height=500" "settings --timeout-s=8 --width=1100 --height=650" "terminal --timeout-s=8" "files --timeout-s=8" "files --timeout-s=8 --width=700 --height=450" "browser --width=800 --height=500 file:///etc/hostname"; do
  echo "== $a" >> $D/apps-rc.txt; timeout 12 $a >> $D/apps-out.txt 2>&1 & p=$!
  sleep 4; [ "$a" = "settings --timeout-s=8" ] && keiland-shot /tmp/t690-app.png > $D/shot-app.txt 2>&1 && cp /tmp/t690-app.png $D/
  wait $p; echo rc=$? >> $D/apps-rc.txt; done
xserver :0 --size 640x480 > $D/xserver.log 2>&1 & xp=$!; sleep 6; ps -e -o pid,comm | grep -i xserver >> $D/apps-rc.txt; kill $xp; sleep 2
slice apps
# (e) rebuild
mark rebuild
echo kei | sudo -S dmesg 2>/dev/null | grep -E "v3d: memory kib=|v3d: alloc" | tail -3 > $D/mem-pre.txt
i=0; while [ $i -lt 50 ]; do videoplayer --fullscreen --timeout-s=2 $V/high-1080p60-10s.mp4 >> $D/vp-rebuild.log 2>&1; videoplayer --timeout-s=2 $V/high-1080p60-10s.mp4 >> $D/vp-rebuild.log 2>&1; i=$((i+1)); done
sleep 2; slice rebuild
echo kei | sudo -S dmesg 2>/dev/null | grep -E "v3d: memory kib=|v3d: alloc" | tail -3 > $D/mem-post.txt
# (f) control
VENV=VULKAN_ZEDBSD_DIRECT=0 run f0 0 --fullscreen --timeout-s=15 $V/high-1080p60-10s.mp4
echo kei | sudo -S dmesg > $D/dmesg-all.log 2>/dev/null
echo ALLDONE > $D/done
