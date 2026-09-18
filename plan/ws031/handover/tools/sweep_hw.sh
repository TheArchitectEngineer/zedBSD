# sequential real-GPU regression of the four accepted modes; stops at the first mode that does not print its PASS line
run() {
  NAME="$1"; LOG="$2"; PAT="$3"; PASSRE="$4"
  echo "########## $NAME"
  ssh solaris10-man "cd ~/bigbang && grep -q 'checks, 0 failures' run-parity-$NAME-nogpu.log && cp -f guest-parity-$NAME.img guest-parity.img && sha256sum guest-parity.img | cut -c1-16 && ./run-parity-ref.sh >/dev/null 2>&1; cp run-parity.log $LOG; grep -naE 'fatal|panic|ktest FAIL|checks,|gt_mem:|$PAT|attach end|runner-result' $LOG | cut -c1-260"
  ssh solaris10-man "grep -qaE '$PASSRE' ~/bigbang/$LOG && grep -qa 'runner-result: selftest=PASS.*probe=COMPLETE.*cleanup=1' ~/bigbang/$LOG" || { echo "STOP: $NAME did not pass; no further submissions"; exit 1; }
}
run eu-e105   run-parity-hw-e105-eu.log   'EU-TEST (PASS|HANG|ERROR)|EU-REPEAT (PASS|HANG|ERROR)' 'EU-REPEAT PASS: .*rounds=5 passed=5' &&
run draw-e105 run-parity-hw-e105-draw.log 'DRAW-TEST (PASS|HANG|ERROR)' 'DRAW-TEST PASS: .*pixels match=1024/1024' &&
run r1-e105   run-parity-hw-e105-r1.log   'parity R1 (PASS|HANG|ERROR)' 'R1 PASS: .*steps=12/12 passed=12' &&
run tex-e105  run-parity-hw-e105-tex.log  'TEX-TEST (PASS|HANG|ERROR)' 'TEX-TEST PASS: .*pixels match=1024/1024.*changed_bytes=0 guard_bad_bytes=0' &&
echo "SWEEP COMPLETE: 4/4"
