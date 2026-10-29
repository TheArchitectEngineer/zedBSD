<!-- awesome-plan project=zedbsd record=ws312-p003 -->

# ws312-p003 QEMU の試験（大きい UDP・ping6 の fragment、手で作る fragment と walk との結合）

- 親: [WS312](../ws.md)。Queue: q1672 / q1672-i01（担当 P6、2026-10-29）。
- Status: in-progress（image の build 中）

## 方法

- image: `plan/tools/guest/test-image.sh plan/ws312/tests/config-amd64-frag.mk build/ws312-frag`（SSH の guest に ifconfig・ping・sysctl・udp6-recv）。
- 試験: `plan/ws312/tests/qemu-frag.sh build/ws312-frag/hdd-image.img build/ws312-qemu`（1 本の script、項目は script の先頭）。host の側は `plan/ws312/tests/frag6.py`。
- 網: 新しい bridge p6br0（p6tap0 と veth p6v0）、veth の先 p6v1 は netns p6ws312（fd00:312::1/64）、guest は fd00:312::2/64。Q1 の承認（2026-10-29）の条件どおり、
  物理の NIC・既存の bridge・route・firewall・global の sysctl に触らず、終わりに作った link・netns を消して読み戻す（script の最後の `teardown:` の行）。
- guest の SSH は guest.py の自動の port、runtime は `build/ws312-qemu-run`。判定は SSH の出力と netns の道具の出力（QEMU・serial の log は使わない）。

## 結果

- 未実施。
