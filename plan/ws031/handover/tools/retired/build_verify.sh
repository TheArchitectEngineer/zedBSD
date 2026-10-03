# WS031 regression helper. KVM_HOST = ssh alias of the KVM host that owns the vfio-pci GPU (run scripts live in ~/bigbang there).
# usage: bash -s NAME FLAG   (e.g. r1-e102 -DPARITY_R1_TEST=1)
set -u
NAME="$1"; FLAG="$2"
cd ~/zedBSD
rm -rf build/$NAME
make -j"$(nproc)" BUILD=build/$NAME CONFIG_DRIVER_PCI_I915_PARITY=y ZEDBSD_TEST_CPPFLAGS=$FLAG disk-image > /tmp/$NAME-build.log 2>&1
echo "make_rc=$?"
grep -nE ": (fatal )?error:|warning:|undefined symbol|\*\*\* " /tmp/$NAME-build.log | head -20
sha256sum build/$NAME/vmunix build/$NAME/hdd-image.img build/$NAME/uefi/BOOTX64.EFI
tr ' ' '\n' < build/$NAME/.platform-config 2>/dev/null | grep -E "PARITY" | sort -u
[ -f build/$NAME/hdd-image.img ] || exit 1
scp -q build/$NAME/hdd-image.img ${KVM_HOST:-kvmhost}:bigbang/guest-parity-$NAME.img
ssh ${KVM_HOST:-kvmhost} "
cd ~/bigbang
sed -e 's/guest-parity-e98b.img/guest-parity-$NAME.img/; s/run-parity-e98b-nogpu.log/run-parity-$NAME-nogpu.log/' run-parity-nogpu-e98b.sh > run-parity-nogpu-$NAME.sh
chmod +x run-parity-nogpu-$NAME.sh
./run-parity-nogpu-$NAME.sh > /dev/null 2>&1
echo '##### nogpu'
grep -n 'checks,\|ktest FAIL\|runner-result\|panic' run-parity-$NAME-nogpu.log | head -20
"
