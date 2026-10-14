# 案: submodule `vendor/raspberrypi-firmware` を外す（Q1 の手順）

ws233-p001 の後、build は `vendor/raspberrypi-firmware` を読まない（`platform/arm64/firmware.mk` が同じ commit `3d301dd9…` の 5 file を cache に取る）。CI の `git submodule update` の行は外した。`plan/tools/boot-test.sh`・`plan/tools/guest/rpi4-serial.sh` は image の横の `bcm2711-rpi-4-b.dtb` を先に使い、無ければ submodule の copy を使う（どちらの状態でも動く）。

担当は削除の command を打たないので、外す作業は Q1 が main で行う:

```sh
git rm vendor/raspberrypi-firmware      # gitlink と .gitmodules の節を消す
git status --short .gitmodules          # 中身が空なら: git rm .gitmodules
git commit -m WIP -- .gitmodules vendor/raspberrypi-firmware
```

main の checkout の `vendor/raspberrypi-firmware/` の working tree（populated）は `git rm` で消える。次の物が既定の DTB の path としてそこを指している（DTB は引数で渡せる）。既定を直すのは各 WS の変更の時（AGENTS.md の試験の整理の基準）:

- `plan/ws048/tests/host-test.mk`（`DTB ?=`）
- `plan/ws203/tests/host-test.mk`（`DTB ?=`）
- `plan/ws206/tests/sdio-host-test.sh`・`firmware-smp-host-test.sh`・`gpio-host-test.sh`（第 2 引数の既定）
- `plan/ws048/proposed/scratch-qemu-probe.sh`（別の checkout の絶対 path、もう動かない）

代わりの DTB は `build/sources/firmware/rpi4-boot/3d301dd924bcd758a4c8cb19fe8531031f033f43/bcm2711-rpi-4-b.dtb`（arm64 の build の後）か、image の横の `build/arm64/bcm2711-rpi-4-b.dtb`。
