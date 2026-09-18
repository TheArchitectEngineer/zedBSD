# WS031 E-108: regression of the six accepted GPU modes on the current source (after the DC_off fix and the eDP
# first stage were added).  Builds every mode, GPU-free ktest first, then one real-GPU run each, in order;
# stops at the first mode that does not print its PASS line (no further submissions after an anomaly).
# usage (on agent-1): bash /tmp/sweep_e108.sh > /tmp/sweep_e108.log 2>&1
set -u
cd ~/zedBSD
TAG=e108
build() {
  NAME="$1-$TAG"; FLAG="$2"
  echo "########## build $NAME $FLAG"
  rm -rf build/$NAME
  make -j"$(nproc)" BUILD=build/$NAME CONFIG_DRIVER_PCI_I915_PARITY=y ZEDBSD_TEST_CPPFLAGS=$FLAG disk-image > /tmp/$NAME-build.log 2>&1
  echo "make_rc=$?"
  sha256sum build/$NAME/vmunix build/$NAME/hdd-image.img | cut -c1-90
  [ -f build/$NAME/hdd-image.img ] || return 1
  scp -q build/$NAME/hdd-image.img solaris10-man:bigbang/guest-parity-$NAME.img
  ssh solaris10-man "cd ~/bigbang && sed -e 's/guest-parity-e98b.img/guest-parity-$NAME.img/; s/run-parity-e98b-nogpu.log/run-parity-$NAME-nogpu.log/' run-parity-nogpu-e98b.sh > run-parity-nogpu-$NAME.sh && chmod +x run-parity-nogpu-$NAME.sh && ./run-parity-nogpu-$NAME.sh > /dev/null 2>&1; grep -a 'checks,' run-parity-$NAME-nogpu.log | cut -c1-120"
}
run() {
  NAME="$1-$TAG"; LOG="run-parity-hw-$TAG-$1.log"; PAT="$2"; PASSRE="$3"
  echo "########## run $NAME"
  ssh solaris10-man "cd ~/bigbang && grep -qa 'checks, 0 failures' run-parity-$NAME-nogpu.log && cp -f guest-parity-$NAME.img guest-parity.img && sha256sum guest-parity.img | cut -c1-16 && ./run-parity-ref.sh >/dev/null 2>&1; cp run-parity.log $LOG; grep -naE 'fatal|panic|ktest FAIL|checks,|gt_mem:|DC state mismatch|$PAT|attach end|runner-result' $LOG | cut -c1-260"
  ssh solaris10-man "grep -qaE '$PASSRE' ~/bigbang/$LOG && grep -qa 'runner-result: selftest=PASS.*probe=COMPLETE.*cleanup=1' ~/bigbang/$LOG" || { echo "STOP: $NAME did not pass; no further submissions"; exit 1; }
  scp -q solaris10-man:bigbang/$LOG plan/ws031/handover/increment-results/$TAG-run-parity-hw-$1.log
}
build eu   -DPARITY_EU_TEST=1   || exit 1
build draw -DPARITY_DRAW_TEST=1 || exit 1
build r1   -DPARITY_R1_TEST=1   || exit 1
build tex  -DPARITY_TEX_TEST=1  || exit 1
build t3   -DPARITY_T3_TEST=1   || exit 1
build bl   -DPARITY_BL_TEST=1   || exit 1
run eu   'EU-TEST (PASS|HANG|ERROR)|EU-REPEAT (PASS|HANG|ERROR)' 'EU-REPEAT PASS: .*rounds=5 passed=5' &&
run draw 'DRAW-TEST (PASS|HANG|ERROR)' 'DRAW-TEST PASS: .*pixels match=1024/1024' &&
run r1   'parity R1 (PASS|HANG|ERROR)' 'R1 PASS: .*steps=12/12 passed=12' &&
run tex  'TEX-TEST (PASS|HANG|ERROR)' 'TEX-TEST PASS: .*pixels match=1024/1024.*changed_bytes=0 guard_bad_bytes=0' &&
run t3   'parity T3 (PASS|HANG|ERROR)' 'T3 PASS: .*steps=9/9 passed=9' &&
run bl   'parity BL (PASS|HANG|ERROR)' 'BL PASS: .*steps=4/4 passed=4' &&
echo "SWEEP COMPLETE: 6/6"
