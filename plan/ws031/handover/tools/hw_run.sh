# WS031 regression helper. KVM_HOST = ssh alias of the KVM host that owns the vfio-pci GPU (run scripts live in ~/bigbang there).
# usage: bash -s NAME LOGNAME PATTERN  — real-GPU run of guest-parity-NAME.img, only if its GPU-free log completed the ktest
NAME="$1"; LOG="$2"; PAT="$3"
ssh ${KVM_HOST:-kvmhost} "cd ~/bigbang && if grep -q 'checks, 0 failures' run-parity-$NAME-nogpu.log; then cp -f guest-parity-$NAME.img guest-parity.img && sha256sum guest-parity.img | cut -c1-16 && lspci -nnk -s 00:02.0 | grep -i 'in use' && ./run-parity-ref.sh >/dev/null 2>&1; cp run-parity.log $LOG; grep -naE 'fatal|panic|ktest FAIL|checks,|gt_mem:|$PAT|EU-TEST (record|hang)|attach end|runner-result' $LOG | cut -c1-430; else echo 'GPU-free ktest did not complete: GPU run skipped'; fi"
