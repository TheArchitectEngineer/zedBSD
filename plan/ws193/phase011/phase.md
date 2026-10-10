<!-- awesome-plan project=zedbsd record=ws193-p011 -->
# WS193 p011: RPi4 boot FAT automatic publication

Status: cleared
Disposition: normal
Parent: [WS193](../ws.md)
Queue: [rpi4-boot0-20261011](../codex-rpi4-boot0-queue.md)

Current user authorized automatic /boot/boot0 mount for SSH kernel updates. [Read-only evidence](../tests/rpi4-boot0-readout-20261011.md) confirms native UFS root and absent boot mount.

Design: retain the loader-origin FAT through the existing common boot-source context after legacy native root selection, avoiding duplicate FAT ownership for legacy overlay/root-on-FAT. Use the existing directory creation and bind-publication helper; RPi4 slot0 is published regardless of overlay/swap references. FAT mount failure is logged and does not invalidate a usable UFS root. Normal explicit-root RPi4 also publishes its configured slot0. Do not change swap, command line, HAL API or other platforms.

Criteria: focused publication checks use actual changed production function, RPi4/other-platform compile paths, warning0 current arm64 kernel build, full coding-style.md review of all final changes and diff-check. Build success does not prove physical FAT writes or next boot. Deployment/reboot and main integration are separate from these criteria. Constraints: AGENTS.md, Guardrail, full C standard, .clang-format; private worktree/build, read-only toolchain; Master and shared boards unchanged, Q1 projection pending.

## Source/build outcome / 2026-10-11

Limited criteria cleared. [Evidence](../tests/rpi4-boot0-20261011.md) records actual production publication host models, amd64 VFS compilation, current arm64 kernel warning0/image checks, final full-standard review and limitations. Existing native UFS root is preserved; retained FAT is read-write and RPi4 slot0 is automatically published. The existing legacy overlay retains its separate FAT without a duplicate mount. Physical FAT/kernel-update acceptance is unverified; main approval and shared projections/GitHub remain pending. No push/deployment/reboot.

## main integration / 2026-10-11

User「/boot/boot0がないようですが、これもマージしてもらえますか？」and standing automatic-integration approval authorize this integration. Source `067ae96d8` and its read-only investigation record `9507b898e` merged into current main source in private worktree as `4cfddb3f9`; no source conflicts or extra source changes. Focused production publication host model passes for RPi4/amd64. `make -j16 build/arm64/vmunix > build/rpi4-boot0-integration.log 2>&1` exits0 with warnings0/errors0 and ELF/raw-image checks PASS. VFS source remains byte-identical to the previously reviewed fix; config.mk matches main. `git diff --check` PASS.

Clean main `40cba8951` fast-forwarded to `4cfddb3f9`; all9 integrated paths read back identical to verified worktree. Main integration complete. Previous approval-pending text is historical. Next user boot with this kernel should expose firmware FAT at /boot/boot0; physical mount/write/kernel-update confirmation is still unverified. No push, deployment, runtime mount or reboot. Shared Master/Queue/history/cache/GitHub projections remain Q1-owned.
