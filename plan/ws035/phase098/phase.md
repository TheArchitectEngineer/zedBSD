<!-- awesome-plan project=zedbsd record=ws035p098 -->

# ws035-p098: 完全なグラフィカル起動を既定に（と kernel の開発用の切り替え）

Phase ID: `ws035-p098`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て。ユーザー「…グラフィカルログインをデフォルトにします。…これはデフォルトではありますが、
カーネル自体の開発のときは無効にして、コンソールにメッセージを表示させ、コンソールログインにします。」）

## 実装（2026-09-28）

- init（`userland/base/init/main.c`）: service 定義の `replaces=NAME`。それを持つ service が enabled なら NAME を起こさない
  （skipped、`init: getty_console replaced by greeter`）。それが終わり再起動しないとき NAME を起こす（`init: greeter ended;
  starting getty_console`）。system の停止中はしない。`docs/reference/init-services.md` の表。
- `/etc/service.d/greeter`（p094）が `replaces=getty_console`。`userland/base/etc/rc.conf` に `greeter: enabled: true,
  optional: true`（sessiond の無い platform・image では optional で無視され、getty_console が従来どおり）。
- kernel: sysctl `kern.boot.login`（`login=` の値、無ければ ""、`include/uapi/sysctl.h` の `KERN_BOOT_LOGIN`）。`sysctl` の
  道具が文字列として出す。sessiond はこれが `graphical` のときだけ graphical（p094 の実装のまま）。
- build: 選択肢 `ZEDBSD_GRAPHICAL_BOOT`（`config/rootfs-options.list`、amd64、既定 y。Makefile の `?= y`）。y なら amd64 native の
  zedbsd.cfg（`$(BUILD)/zedbsd-native-uefi-graphical-y.cfg`、値が名前にあるので切り替えで image を作り直す）に
  `logo=logo.ppm kmsg=quiet login=graphical`。n は kernel の開発用（message を console に、console の login）。
- 試験の構成: `plan/ws035/tests/config-amd64-userland.mk`（lean の Venus image・guest image・i915 の実機 image の元）に
  `ZEDBSD_GRAPHICAL_BOOT := n`（zdesktop を自分で起こす試験が表示を空きで見つける）。`config-amd64-graphical.mk`（新、y）と
  `build-login-image.sh BUILD graphical`。CI の構成（`config/ci/config-amd64.mk`）は変えない（既定 y）: boot test の QEMU には
  GPU が無いので sessiond がすぐ終わり、getty_console が login prompt を出す（下の検証）。
- 文書: `docs/reference/kernel-boot-parameters.md` §7b（`login=`、既定と kernel の開発）、名前の一覧に `kmsg=`・`login=`。
  `login-manager-design.md` §10（決定と実装）。

## 検証（amd64、QEMU、2026-09-28）

- `plan/ws035/tests/zdesktop-p098.sh`（Venus の guest、graphical の image）PASS: `kern.boot.login: graphical`、init が getty を
  起こさず sessiond が greeter（uid 78、users=1 root）を起こす、Enter（root の空の password）で session（`/run/user/0`）、
  App Home の Log Out で greeter に戻る。画面 `p098-20260928-greeter.png`・`-session.png`・`-again.png`、文字 console
  （標準 VGA）`-console.png`（HAL の提案を当てていないので HAL の早期の行が残る。kernel の message は無い）。
- boot test（graphical の image、GPU の無い QEMU）PASS: `init: getty_console replaced by greeter` → `greeter ended; starting
  getty_console` → login prompt（`p098-20260928-boot-test-no-gpu.png`）。
- boot test（試験の構成 n の image）PASS。zdesktop-p094（sessiond の手動の試験）PASS。
- `make menuconfig-host-test`: `MAC-T001: expat is filed under packages/libs, which the menu omits` で失敗（package の menu の
  分類で、この Phase の rootfs の選択肢とは関係しない。main の tree で既にあるかは未確認）。
- 規約: init の追加は増やしていない（149 → 149 に戻した）、sysctl.c は増えていない。build warning 0（第三者の package の既存の
  warning を除く）。
- 実機: 未実施。

## 残り

- HAL の早期 console の提案（p097）の承認と適用: 当てるまで、静かな起動で HAL の早期の行が logo の上に残る。
- demo の image（`plan/ws035/demo`、構成 n）を graphical login に移すか（demo の rc.conf は root の zdesktop を直接起こす）。
