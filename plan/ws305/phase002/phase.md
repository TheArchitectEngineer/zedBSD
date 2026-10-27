<!-- awesome-plan project=zedbsd record=ws305-p002 -->

# ws305-p002 passthrough の環境の構築と RDNA の guest

- 親: [WS305](../ws.md)。Queue q1619 / q1619-i01（P4、p001 の後）。
- 状態: planned（未着手。2026-10-27 11:57 ラップアップの時点で script と image は用意済み、host の作業は一つも未実施。再開点は末尾）

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
- 2026-10-27 11:57 P4: ラップアップ。host に何も入れていない（grub・apt・/etc/default/grub は未変更）。

## 状態と再開点（2026-10-27 11:57）

用意済み（host で未試験）: `plan/ws305/tests/vfio.sh`・`vfio-host.py`・`41_zedbsd_vfio`（build host で dry-run だけ）・`config.mk`。
image は P4 の worktree の `build/ws305/vm/hdd-image.img`（2026-10-27 11:25、sha256 の先頭 b2e68dec57f4adb6、基点 0e8132fbd の source、sysroot は P2 の複写）。
guest の鍵は worktree の `plan/tmp/guest/id_ed25519`（image に入っているのはこの鍵の公開鍵。別の worktree で再開するなら image を作り直す）。

手順（各段の前に lock が空いている事と Q1 の OK、build host の tree の上で）:

1. `sh plan/ws305/tests/vfio.sh mode` → `debian`。
2. `sh plan/ws305/tests/vfio.sh setup`: qemu-system-x86・ovmf を apt（無ければ）、`/etc/default/grub.ws305-orig` を残して `GRUB_RECORDFAIL_TIMEOUT=5` を追記（Q1 2026-10-27 11:20 の依頼）、
   `/etc/grub.d/41_zedbsd_vfio` を入れて update-grub。出力の grub.cfg の menuentry と diff を記録。既定の項目が先頭のままである事を確かめる。
3. `sh plan/ws305/tests/vfio.sh push build/ws305/vm/hdd-image.img`。
4. `sh plan/ws305/tests/vfio.sh enter`（grub-reboot zedbsd-vfio ＋ reboot、300 s 待つ）→ `status`（GPU の function が driver 無しか snd_hda_intel、group）。
5. `start` → `gpu`（amdrdna の段）→ `shot build/ws305/shot-1`（PNG を Q1 へ）。GOP が走らず post されない時は `rom vbios.rom` → `start --rom /home/awe/ws305/vbios.rom`。
6. R1 の表: `reset`、`restart`、`kill` ＋ `start` のそれぞれの後に `gpu`。戻らない形が出たら `reset-method ""`（host の reset を止める）で同じ事、だめなら R2（vendor-reset）の案を Q1 に。
7. `stop` → `leave`（既定の Debian に戻り amdgpu が付く事を `ssh awe@10.0.10.3 lspci -k -s <slot>` で）。

lock: 全命令が build host の `/tmp/zedbsd-desktop.lock` を flock -w 60（P2 の measure.sh と同じ lock、Q1 2026-10-27 11:10）。電源の入れ直しは Q1（SwitchBot）。
