cd /root
cp /boot/boot0/vmunix /root/src8m
i=0
while [ $i -lt 10 ]; do cp /root/src8m /root/cp$i; i=$((i+1)); done
df -k /root | tail -1
i=0
while [ $i -lt 10 ]; do rm /root/cp$i; i=$((i+1)); done
cp /root/src8m /root/keep1
rm /root/src8m
df -k /root | tail -1
sync
