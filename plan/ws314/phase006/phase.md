<!-- awesome-plan project=zedbsd record=ws314-p006 -->

# ws314-p006 `kl_instance` の event と Settings の移行

- 親: [WS314](../ws.md)。設計: [design.md](../design.md) §6。Queue: q1690 / q1690-i01（担当 P2、2026-10-30）。
- Status: cleared（2026-10-30。zedBSD（QEMU）で instance-test が PASS。Settings そのものの 2 回目の起動の QEMU は未実施）
- Disposition: normal

## 目的・受け入れ

- `kl_instance_bind(instance, app)`（KL_VERSION 99）: 後の起動の要求を app の `KL_APP_INSTANCE` の event で届ける。`kl_instance_fd` を
  使わずに済む形にし（外すのは p008）、Settings を移す。close の順（app が先でも copy が先でも）で安全。

## 実装

- `libkeiland/instance.c`: `struct kl_instance` に app・app の list・queued・thread（join する）・止める pipe・lock・要求の ring（8）。
  `kl_instance_bind` は thread `kl-instance` を起こす（listen の socket と止める pipe を `poll`、来た起動を今の `instance_receive`
  （accept・500 ms 以内の 2 行・検査）で受け、ring に積み、1 つ目で app の wake を鳴らす。9 つ目からは捨てる）。`kl_instance_take` は
  bind の時は ring から、無い時は今どおり直に受ける。`kl_instance_close` は bind の時に thread を止めて join し、app の list と queue から除く。
- `ui/app.c`: dispatch の `keiui_instances_queue`（要求のある copy で queue に未取得の event が無い物に 1 つ）、`kl_app_take` の
  `keiui_instance_taken`、`kl_app_close` の `keiui_instances_unbind`、`keiui_app_forget_instance`。
- `platform/`: join する thread（`keiui_thread_start_joined`・`keiui_thread_join`）。
- header: `keiland_instance.h`（`kl_instance_bind`）、`keiland_app.h`（`KL_APP_INSTANCE`）、`keiland_version.h`（99）、exports.map。
- Settings: `kl_instance_bind(main_instance, app)`、`se_window_dispatch` が `KL_APP_INSTANCE` で `handed_over` を立て、main loop は立った時だけ
  `main_handed_over()`（毎回の呼び出しを止めた）。`extra_fd`・`extra_watched` と `kl_app_watch_fd` を外した。close の順（app、copy）は今のまま。

## 確かめ（2026-10-30）

- Linux（all、`build/ws314-p006-linux`）: make 0・warning 0。
- zedBSD（`build-zedbsd.sh`）: amd64（desktop 19＋tests 9）・arm64・riscv64（19）・installer の image の installer: make 0・warning 0。
- host の試験 `plan/ws314/tests/instance-test.c`（`sh plan/ws314/tests/host-test.sh instance BUILD`）: one copy になり bind、2 度目の bind は
  EINVAL、別の thread の 3 つの起動（`kl_instance_open` の同じ名前）が `KL_APP_INSTANCE` で届き順に take、残り 0、app を先に閉じて copy を閉じる。
  PASS。TSan の libkeiland でも PASS（post・process も）。
- zedBSD（QEMU）: p005 と同じ run（`build/p2-q/run4`）で instance-test が PASS。
- 未実施: Settings を 2 回起動して page が渡ること（QEMU）。
