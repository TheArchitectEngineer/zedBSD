#!/bin/sh
# VF2 の板へ kernel を入れて電源を入れ直し、render node が上がるまで dmesg を連続で採取して繋ぐ（32 KiB の ring の溢れ対策）。
#   plan/tools/vf2/run.sh N [OUTDIR]   → OUTDIR/vf2-dmesgN.txt
# 前提: build/uat/vf2-gpu/vmunix、ssh kei@10.0.30.2（password kei、sudo は同じ）、SwitchBot のプラグ AC276E42B252（plan/tools/switchbot.py）。
# 2026-10-20 ユーザー許可（SSH の全操作、電源の入れ直し、3 分で SSH が戻らなければ再度）。sshpass が要る。
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
N=$1
S=${2:-build/uat/vf2-gpu/runs}
mkdir -p "$S"
SSH="sshpass -p kei ssh -o StrictHostKeyChecking=no -o ConnectTimeout=5 kei@10.0.30.2"
SCP="sshpass -p kei scp -o StrictHostKeyChecking=no -o ConnectTimeout=5"
$SCP build/uat/vf2-gpu/vmunix kei@10.0.30.2:/tmp/vmunix.new || exit 1
$SSH 'echo kei | sudo -S sh -c "cp /tmp/vmunix.new /boot/boot0/vmunix && sync"' 2>/dev/null || exit 1
$SCP kei@10.0.30.2:/boot/boot0/vmunix $S/vmunix.back || exit 1
cmp build/uat/vf2-gpu/vmunix $S/vmunix.back || exit 1
echo deployed
python3 -I plan/tools/switchbot.py cycle AC276E42B252 10 >/dev/null
for try in 1 2; do
  for i in $(seq 1 36); do sleep 5; $SSH true 2>/dev/null && break 2; done
  echo "no ssh after 3 min, cycling again"; python3 -I plan/tools/switchbot.py cycle AC276E42B252 10 >/dev/null
done
: > $S/vf2-dmesg$N-all.txt
for i in $(seq 1 80); do
  $SSH dmesg >> $S/vf2-dmesg$N-all.txt 2>/dev/null
  st=$($SSH sysctl hw.gpu.powervr.stages 2>/dev/null)
  case "$st" in *"render-node=ok"*) break;; esac
  sleep 2
done
echo "$st"
awk '!seen[$0]++' $S/vf2-dmesg$N-all.txt > $S/vf2-dmesg$N.txt
echo "dmesg lines: $(wc -l < $S/vf2-dmesg$N.txt)"
