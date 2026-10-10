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

## main integration / 2026-10-11

User「/boot/boot0がないようですが、これもマージしてもらえますか？」and standing automatic-integration approval authorize this integration. Source `067ae96d8` and its read-only investigation record `9507b898e` merged into current main source in private worktree as `4cfddb3f9`; no source conflicts or extra source changes. Focused production publication host model passes for RPi4/amd64. `make -j16 build/arm64/vmunix > build/rpi4-boot0-integration.log 2>&1` exits0 with warnings0/errors0 and ELF/raw-image checks PASS. VFS source remains byte-identical to the previously reviewed fix; config.mk matches main. `git diff --check` PASS.

Clean main `40cba8951` fast-forwarded to `4cfddb3f9`; all9 integrated paths read back identical to verified worktree. Main integration complete. Previous approval-pending text is historical. Next user boot with this kernel should expose firmware FAT at /boot/boot0; physical mount/write/kernel-update confirmation is still unverified. No push, deployment, runtime mount or reboot. Shared Master/Queue/history/cache/GitHub projections remain Q1-owned.
