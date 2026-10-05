<!-- awesome-plan project=zedbsd record=q779 -->
# q779: BUG-202 の起動中の idle sleep

Date: 2026-10-06
Status: finished
Executor: Codex
Phase: [ws073-p055](../ws073/phase055/phase.md)
Bug: [BUG-202](../bugs/BUG-202.md)
Lane: [Codex BUG-202](../agents/codex-bug202/queue.md)

## 承認と範囲

現在のuserの指示:「実機で起動中（カーネル起動直後）にカーネルがフリーズします。ACPIやモダンスリープのサポートを追加したのがデグレードの直接の原因です。セキュリティはチェックせず、機能性だけチェックして直してください。テストは実機で私が行います。あなたはビルドが通るところまででいいです。」

統合の承認: 同じ会話でuser「マージしてください。」

q779-i01: 写真のidle thread sleepsの呼出しを既存ELFとsourceで確定し、起動時のsleep可能なdevice初期化を通常threadへ移す。機能reviewとamd64 kernel build warning/error 0まで。実行試験はuserの実機UATに委ねる。原因確定から修正・buildの1 attempt、最大3h。依存は既存idle guardとuat-0506c ELFで、両方を確認した。依存関係はこの既存成果→q779-i01だけで、循環はない。

## 結果

q779-i01 / ws073-p055: **cleared**（上記の限定範囲）。LPSSのD0復帰10ms待ちがidle/bootstrap上で実行されていた。PCI・ACPI・device refreshを既存の通常boot_workerへ移し、その後のVFS・initの順を維持した。変更は `src/kern/entry.c`・`src/kern/main.c`・`include/kern/kernel.h`。機能reviewでplatform固有handoffの拡張部の寿命も確認した。HAL APIの変更はない。

元のsource commit `801393ad`・記録 `0ca9de75`（base `acbb7e4a`）。mainの履歴整理後、今回の2コミットだけをmain `010ae1c0`上へ載せ直した（source `e093bebe`・記録 `c313e3ef`、branch `codex/merge-bug202`）。mainへの統合差分は元修正と一致し、無関係なsource変更はない。共有Queue・Master・Past Log・WS・Phase・Bugへ同時に投影した。

## mainでのビルド

Command: `make -j8 ZEDBSD_CONFIG=plan/uat/config-uat.mk BUILD=build/bug202-merge build/bug202-merge/vmunix`

- exit 0、Clang23.1.0、compiler warning/error 0。
- kernel include check PASS（341 objects / 8903 dependencies）、amd64 vmunix check PASS。
- 元修正は全文C規約の変更範囲review、clang-format19、style-diffの変更行findings 0、diff check PASS。統合時のsourceは同一。
- ELF: `/home/awe/zedBSD-claude1/build/bug202-merge/vmunix`。
- SHA256: `6ea6eca215149dbb69d37880d065dbe0e90bfc02c9c7a40315a78e42f44a8757`（元worktreeの最終buildとも一致）。
- Log: `/tmp/bug202-merge-build.log`。既存toolchainを使用し、変更・再buildはしていない。

## 残りと同期

user指定によりhost/QEMU/実機の実行試験、セキュリティreviewは未実施。今回の成果物はkernelで、USB用disk imageは作成していない。userが同じ5330の起動でfatalを越えてlogin/desktopに到達するか確認する。ACPI/modern standbyの起床自体も今回のbuildで合格とはしない。

BUG-202は実機確認までtracking、WS073はincomplete。再現した場合はuserの写真と今回のELFを照合する。次Queueは開始しない。記録はlocalのみ、GitHub公開はconfigどおり保留。pushは未実施。
