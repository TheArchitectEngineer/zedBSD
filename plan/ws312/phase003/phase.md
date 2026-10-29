<!-- awesome-plan project=zedbsd record=ws312-p003 -->

# ws312-p003 QEMU の試験（大きい UDP・ping6 の fragment、手で作る fragment と walk との結合）

- 親: [WS312](../ws.md)。Queue: q1672 / q1672-i01（担当 P6、2026-10-29）。
- Status: cleared（2026-10-29。QEMU の試験 1 回で全項目 PASS、実機は使わない）

## 方法

- image: `plan/tools/guest/test-image.sh plan/ws312/tests/config-amd64-frag.mk build/ws312-frag`（SSH の guest に ifconfig・ping・sysctl・udp6-recv）。
- 試験: `plan/ws312/tests/qemu-frag.sh build/ws312-frag/hdd-image.img build/ws312-qemu`（1 本の script、項目は script の先頭）。host の側は `plan/ws312/tests/frag6.py`。
- 網: 新しい bridge p6br0（p6tap0 と veth p6v0）、veth の先 p6v1 は netns p6ws312（fd00:312::1/64）、guest は fd00:312::2/64。Q1 の承認（2026-10-29）の条件どおり、
  物理の NIC・既存の bridge・route・firewall・global の sysctl に触らず、終わりに作った link・netns を消して読み戻す（script の最後の `teardown:` の行）。
- guest の SSH は guest.py の自動の port、runtime は `build/ws312-qemu-run`。判定は SSH の出力と netns の道具の出力（QEMU・serial の log は使わない）。

## 結果（2026-10-29、QEMU。実機の証拠は無い）

- image: `build/p6-image.sh`（= `flock /tmp/zedbsd-image-build.lock sh plan/tools/guest/test-image.sh plan/ws312/tests/config-amd64-frag.mk build/ws312-frag`）、rc=0、
  tree は f28f69e72 と同じ source（fca883bb5 以後は記録だけ）。warning 477 は perl の locale の注意と外部の OpenSSH の物だけ（kernel・udp6-recv は 0）。
- 試験: `timeout 900 sh plan/ws312/tests/qemu-frag.sh build/ws312-frag/hdd-image.img build/ws312-qemu`、rc=0、最後の行 `qemu-frag: PASS`、`ok:` 20 行・`FAIL:` 0 行:
  - guest の address、udp6-recv の ready、namespace から guest への ping。
  - 1. `ping -6 -c 3 -s 1400`（namespace の route `fd00:312::2 dev p6v1 mtu lock 1280`）: `1408 bytes from` が 3 行。
  - 2. UDP 1400・4000・8000 byte が長さと FNV-1a（b2eb9e95・133e4645・74c0d745）の一致で届いた。12000 byte（上限の外）は届かない。
  - 3. 逆順の fragment（3000 byte、6a3deb15）が届く。重なりは届かない。atomic（500 byte）が届く。片の欠けた 2 つの datagram のうち、offset 0 を持つ物
    （id 0x31200003）にだけ Time Exceeded code 1 が 1 回（80 秒の listen で 1 件）。
  - 4. atomic の後の未知の next header（253）に Parameter Problem code 1、pointer 40。fragment header の後の hop-by-hop に同じく pointer 40。
    2 つの fragment header は届かない。平の NS には NA が返り、atomic の fragment の NS には返らない。
  - 5. `ping -6 -s 3000`: 2 送って 0 受け（判定しない。返りの 3048 byte は送りの fragment が要り範囲の外）。
  - 6. guest は最後まで SSH に答えた。
  - 最後の行 `teardown: p6tap0 p6v0 p6br0 netns:p6ws312 all gone`（作った link と netns を消して読み戻した）。
- 成果物: `build/ws312-qemu.log`（要約）、`build/ws312-qemu/`（各項目の出力、`udp6.log` は guest の受け手の出力）。git に入れない。

## 未実施・観察

- `ping -s 1400` が Linux で本当に 2 片に分かれた事は capture で見ていない（route の `mtu lock 1280` と 1448 byte の packet からの推定）。4000・8000・3000 byte と手で作った片は fragment に限られる。
- 最初の `ifconfig ue1` で `RX ... errors 4`（試験の前、tap の上の何かの frame）。原因は調べていない（この WS の範囲の外の観察）。
- jumbo の MTU の link（9000）での受け、32 bit の board の上の動き、実機は未実施。
