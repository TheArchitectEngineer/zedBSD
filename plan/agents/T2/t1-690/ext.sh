d=$1; n=$2
f=$(grep FRAMES $d/vp-$n.log | tail -1 | sed 's/.*shown=\([0-9]*\) time_ms=\([0-9]*\) late=\([0-9]*\)/\1\/\3 \2ms/')
c=$(grep "KWL PERF compose" $d/comp-$n.log | sed -n '2,$p' | awk '{for(i=1;i<=NF;i++){if($i~/^frame_ms=/){split($i,a,"=");s+=a[2];c++} if($i=="(acquire"){acq+=$(i+1)} if($i=="submit+present"){sp+=$(i+1)}}} END{printf "%.1f (acq %.1f sp %.1f)",s/c,acq/c,sp/c}')
dr=$(grep "VIDEOPLAYER DRAW" $d/vp-$n.log | sed -n '2,$p' | awk '{for(i=1;i<=NF;i++){split($i,a,"=");if(a[1]=="acquire_us")ac+=a[2];if(a[1]=="submit_us")su+=a[2];if(a[1]=="wait_us")w+=a[2]} c++} END{printf "%d/%d/%d",ac/c,su/c,w/c}')
ps=$(grep -h "v3d: perf" $d/dmesg-$n.log | sed -n '3,$p' | sed 's/.*passes=\([0-9]*\)\/\([0-9]*\)\/\([0-9]*\).*/\1 \3/' | awk '{r+=$1;i+=$2;c++} END{printf "R%d/I%d",r/c,i/c}')
wk=$(grep -h "v3d: perf" $d/dmesg-$n.log | sed -n '3,$p' | grep -o "worker=[0-9]*" | cut -d= -f2 | awk '{s+=$1;c++} END{printf "%d%%",s/c}')
bz=$(grep -h "v3d: perf" $d/dmesg-$n.log | sed -n '3,$p' | grep -o "busy=[0-9]*" | cut -d= -f2 | awk '{s+=$1;c++} END{printf "%d%%",s/c}')
vd=$(grep -h "v3d: video" $d/dmesg-$n.log | sed -n '2,$p' | sed 's/.*depth \([0-9.]*\)).*frame \([0-9]*\) place.*/\1 \2/' | awk '{d+=$1;f+=$2;c++} END{printf "depth %.1f frame_us %d",d/c,f/c}')
echo "| $d/$n | $f | $c | $dr | $ps | $wk | $bz | $vd |"
