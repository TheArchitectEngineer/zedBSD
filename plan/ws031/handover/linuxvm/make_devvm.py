import os
d = "/home/awe/linuxvm"
pub = open(os.path.expanduser("~/.ssh/id_ed25519.pub")).read().strip()

setup = r'''#!/bin/bash
exec > /root/setup.log 2>&1
echo "SETUP-START $(date)"
echo "options i915 enable_guc=0" > /etc/modprobe.d/i915-euctl.conf
apt-get update
apt-get install -y --no-install-recommends linux-modules-extra-$(uname -r) linux-firmware \
    build-essential libdrm-dev
depmod -a
modprobe i915
sleep 5
echo "SETUP i915 driver: $(ls -l /sys/bus/pci/devices/0000:00:02.0/driver 2>&1)"
echo "SETUP dri: $(ls /dev/dri/ 2>&1)"
echo "SETUP enable_guc=$(cat /sys/module/i915/parameters/enable_guc 2>&1)"
dmesg | grep -aiE "i915|wedged" | tail -10
echo "SETUP-DONE"
touch /root/setup-done
'''
import base64
setup_b64 = base64.b64encode(setup.encode()).decode()

ud = """#cloud-config
users:
  - default
  - name: dev
    sudo: ALL=(ALL) NOPASSWD:ALL
    shell: /bin/bash
    ssh_authorized_keys:
      - {pub}
ssh_authorized_keys:
  - {pub}
write_files:
  - path: /root/setup.sh
    permissions: "0755"
    encoding: b64
    content: {setup_b64}
runcmd:
  - [ bash, -c, "bash /root/setup.sh" ]
""".format(pub=pub, setup_b64=setup_b64)
open(os.path.join(d, "user-data"), "w").write(ud)
print("dev user-data written %d bytes" % len(ud))
