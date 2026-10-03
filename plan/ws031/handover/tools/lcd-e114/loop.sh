cd ~/zedBSD
R=plan/ws031/linux-parity/linux-reference
for i in 1 2 3 4 5; do
  python3 plan/ws031/handover/tools/port_lcd_calc.py $R/ubu-i915-src $R/drm-v6.8.12 src/drivers/gpu/i915/parity/lcd > /tmp/gen.out 2>&1 || { tail -5 /tmp/gen.out; exit 1; }
  python3 /tmp/find_missing.py ~/zedBSD > /tmp/missing.txt 2>&1
  grep -q "roots added: 0" /tmp/missing.txt && break
done
grep "roots added" /tmp/missing.txt; echo "missing: $(grep -c MISSING /tmp/missing.txt)"
grep MISSING /tmp/missing.txt | cut -c1-130
grep OTHER /tmp/missing.txt | sort | uniq -c | sort -rn | head -${1:-25}
