S="ssh -o UserKnownHostsFile=/home/awe/zedBSD-claude1/build/q1/pi-known_hosts -o ConnectTimeout=5 kei@10.0.30.2"
n=$1
$S 'echo kei | sudo -S reboot' >/dev/null 2>&1
sleep 25
for i in $(seq 1 40); do
  if $S 'true' 2>/dev/null; then break; fi; sleep 5
done
$S 'uname -v; df -k /root | tail -1; dmesg | grep -E "ufs:|vfs:"; echo kei | sudo -S sh -c "touch /root/t && rm /root/t && echo WRITE_OK"' 2>&1 | tee build/t2/t670/boot$n.log
