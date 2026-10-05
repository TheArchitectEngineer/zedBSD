<!-- awesome-plan project=zedbsd record=ws073-p055 -->
# ws073-p055: BUG-202 の起動中の idle sleep

Status: cleared（user指定の機能修正・buildまで。実機UATは未実施）
Disposition: normal
Parent: [WS073](../ws.md)
Queue: [q779-i01](../../agents/codex-bug202/queue.md)（[共有履歴](../../history/queue-q779.md)）
Bug: [BUG-202](../../bugs/BUG-202.md)

2026-10-06のuserの直接の修正指示。機能修正とamd64 buildまでを受け入れとし、実行試験はuserの実機UATへ。写真のstack候補とbuild/uat-0506c/vmunixの命令を照合した。lpss_attach+0x214がkern_usleep_range(10000)をcallし、return PC 0xffffffff8032dabcが一致。kern_usleep_range→waitq_sleep→sched_sleep_locked→idle guardを確認。PCI probeはkernel_entryのCPU0 idle/bootstrap上であり、後続のACPI AML Sleepにも同じ制約がある。

手順: kern_platform_initとrefreshを既存boot_worker冒頭へ移す。PCI→ACPI→USB/WLAN→i915 runtime→VFSの順は保ち、割込みはworkerが走る前に有効。kernel_mainの唯一の呼び手とkernel内headerを合わせ、device tableはstatic寿命を維持する。idle guardとHAL APIを変えない。Guardrailと全文coding-styleを適用し、変更範囲を機能と規約について確認。現存のtoolchainでUAT configのamd64 vmunixをlinkしwarning/error 0を確認する。QEMU/host実行/実機試験は今回実行しない。

調査上限: 写真とELF/sourceの対応、既存workerへの移動に必要な起動順・所有・割込みの確認。機能のscopeが変わる判断が必要ならこのattemptを止めて理由を記録する。

## 結果（2026-10-06）

q779-i01 cleared。製品修正WIP `801393ad`。3 source/headerだけで、platform初期化を既存boot_workerへ移した。通常threadでsleepでき、IRQ有効化・PCI/ACPI/refresh/VFSの順を維持。device tableはstatic、platform固有handoffはdiscoveryまで元の拡張部を保持、VFSは共通snapshot。機能reviewでhandoffの拡張部を失う問題を修正してから最終buildした。HAL API変更なし。

確認: `make -j8 ZEDBSD_CONFIG=plan/uat/config-uat.mk BUILD=build/bug202 build/bug202/vmunix` exit0、compiler warning/error 0、kernel include/amd64 vmunix check PASS。Clang23.1.0。全文規約の変更範囲review、clang-format19、`style-diff.py --base acbb7e4a`は変更行findings 0、diff check PASS。最終ELF SHA256 `6ea6eca215149dbb69d37880d065dbe0e90bfc02c9c7a40315a78e42f44a8757`。root worktree `/tmp/zedbsd-bug202-codex`、log `/tmp/bug202-final-build.log`。詳細と写真のPC照合はBUG-202末尾。

userが実機で試験する方針のためhost/QEMU/実機の実行試験は省略。BUGはtracking、WS073全体はincomplete。起動がfatalを越えてlogin/desktopに到達するかをuserが確認する。2026-10-06 user「マージしてください。」により、source e093bebe・記録 c313e3efをmain 010ae1c0へ統合し共有計画へ投影。統合時のbuild結果はq779の共有履歴を参照。GitHub公開は保留。次Queueは開始しない。
