<!-- awesome-plan project=zedbsd record=ws305-p002 -->

# ws305-p002 passthrough の環境の構築と RDNA の guest

- 親: [WS305](../ws.md)。Queue q1619 / q1619-i01（P4、p001 の後）。
- 状態: planned（2026-10-27 11:35 P4: script と image は用意済み、host での構築は Debian と lock の空きを待つ）

## 範囲

[p001](../phase001/phase.md) の設計で host（10.0.10.3）に「Debian VFIO」の起動の項目・qemu・OVMF を入れ、RX 5600 XT を vfio-pci で guest の zedBSD に渡す。
guest の amdrdna が付き desktop が出ること、guest を止めて立て直せること（host は止まらない）を確かめる。

## 受け入れ

- guest の dmesg で amdrdna の段が最後まで通り、session（kei の Keiland）が動く。guest の keiland-shot の PNG に desktop（Q1 の判断の形、p001 の 6）。
- R1 の表: host の起動の後の 1 回目、`reset`（QMP system_reset）、`restart`（quit → start）、`kill` → start のそれぞれで amdrdna が付くか。host は止まらない。
- 既定の Debian の起動は変わらない（`leave` で既定に戻り amdgpu が付く）。

## 道具

- `plan/ws305/tests/vfio.sh`（build host から）・`vfio-host.py`（host の root）・`41_zedbsd_vfio`（grub.d）・`config.mk`（guest の image）。
- image: `plan/tools/guest/test-image.sh plan/ws305/tests/config.mk build/ws305/vm --file /etc/keiland/autologin=plan/ws291/tests/autologin --file /etc/keiland/session=plan/ws291/tests/session.sh`。

## 記録

- 2026-10-27 11:35 P4: image を build（build/ws305/vm/hdd-image.img）、host は不達（P2 が lock を持ち zedBSD を起動中）。
