# Codex Queue: rpi4-boot0-20261011

Status: finished
Owner: Codex / codex/rpi4-sshd
Approval: current user「/boot/boot0 というマウントポイントを自動で作って、RPIの起動パーティションをマウントするようにできますか？カーネルをSSHで更新できるのでうれしいです。」
Finite scope: current RPi4 native UFS autoroot retains the loader-origin FAT in the common boot-source context and publishes it read-write at /boot/boot0, even with no image/swap reference. Preserve legacy overlay and other platforms. Review final changed source against full C rules, focused host publication checks, warning0 arm64 kernel build. No swap activation, bootloader change, GPU/USB change, toolchain, push, deployment/reboot or shared planning edits.

| Attempt | Phase | Status | Dependencies |
| --- | --- | --- | --- |
| rpi4-boot0-20261011-i01 | [ws193-p011](phase011/phase.md) | cleared | integrated common VFS; read-only native UFS evidence |

Graph: existing native UFS boot (context) → i01 → user physical mount/kernel-update acceptance (context).
Shared projections/GitHub are Q1 pending. No automatic next Queue.

## Outcome

Source/build item cleared. [Evidence](tests/rpi4-boot0-20261011.md): common boot context retains the native-root boot FAT and RPi4 slot0 publishes read-write at /boot/boot0 without image/swap references. Production publication models PASS, amd64 VFS compile and arm64 kernel/image build PASS warning0, final full C review/diff-check PASS. Main concrete-commit approval is pending; physical FAT write/kernel update is unverified. No swap activation, push/deployment/reboot or next Queue.
