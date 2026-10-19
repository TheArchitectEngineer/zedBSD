<!-- awesome-plan project=zedbsd record=ws270-p003 -->

# ws270-p003 QEMU riscv64-virt での画面なしの確かめ

- 親: [WS270](../ws.md)、Status: in-progress（2026-10-20 P2、q1342 / q1342-i01。準備は済み、T1 の結果待ち）。
- 設計: [design.md](../design.md) 改訂 2.1 の §2.1 B・C・E・F、§3 の p003。

## 範囲

QEMU riscv64-virt（U-Boot、NVMe の native.img、PCI の xhci・usb-kbd・usb-mouse）で、desktop の組の image の service・USB keyboard の text login・mouse の event・graphical login の「display が無い時の道」・`keiland-preview` の static の実行を、T1 の `boot-test.sh` の画面で確かめる。描画はしない。HAL・toolchain は変えない（noct・emacs は config に無い）。

## 準備（P2、2026-10-20）

1. 試験の config: [tests/config-riscv64-virt-desktop.mk](../tests/config-riscv64-virt-desktop.mk)。`plan/ws256/tests/config-riscv64-virt-pci.mk`（xhci・usb-hid・NVMe）に、[tests/config-visionfive2-desktop.mk](../tests/config-visionfive2-desktop.mk) の program の組（noct・emacs を除く）と `mount sh evdev-probe` を足し、`ZEDBSD_GRAPHICAL_LOGIN := y`（`ZEDBSD_GRAPHICAL_BOOT := n` のままなので boot の行の `login=graphical` は 1 つだけ）。
2. 大きさ（自分の worktree の build、`BUILD=build/ws270-p003`）: rootfs の `du -sm` は **73 MiB**（usr 59、bin 8、lib 5、sbin 2）。root.ufs は既定の `RISCV64_NATIVE_ROOT_MIB` 256 MiB に入り、native.img は 404,750,336 B（386 MiB）。WS270-D5 の判断の材料: desktop の組は既定の 256 MiB の root に入る（描画の後の package・data で増える分は未測定）。
3. riscv64 の native の image は `ZEDBSD_GRAPHICAL_LOGIN` を見ていなかった（boot の設定は `platform/riscv64/zedbsd-native.cfg` をそのまま入れていた）。`platform/riscv64/disk-image.mk` で `$(BUILD)/zedbsd-native.cfg` を作り、y の時に `login=graphical` を足す（arm64 の `cmdline.txt` と同じく毎回比べ、同じなら mtime を保つ）。loader（`bootloader/uefi/zedbsd-config.c`）は kernel= 以外の行を kernel の引数に渡し、kernel は `login=graphical` を受ける（`src/kern/boot.c`）。build の確認: 生成の cfg が `kernel=vmunix`・`rootpart=PARTLABEL=zedBSD-root`・`login=graphical`、2 度目の make は何もしない。
4. mouse の event の読み手: 既存の `userland/tests/evdev-probe`（ws143-p005、arch `*`）を使う。`-b usb -n Mouse` で node を探し、event を 1 行ずつ `EVDEV event type=T code=C value=V` で出す。新しい program は作らない。
5. `keiland-preview` は `/usr/libexec/keiland-preview`、riscv64 の static の ELF（`file`: statically linked）。`--info` は text（`keiland-preview-info 1`・`kind=png`・`width=`・`height=`）を fd 1 に書く。入力は image の中の `/usr/share/keiland/wallpapers/Lakeside.png`。直接の実行は sandbox_spawn の道（`preview/zedbsd/confine.c`）を通らない（zedBSD では confine は何もしない）。spawn の道は Keiland の app が呼ぶ時（p006 以降）。
6. 「display が無い時の道」: sessiond は gpu0 が無く `hw.gpu.attaching` が 0 なら待たずに console に戻り、console に `sessiond: there is no greeter or no display; the console login takes over` を 1 行書く（`userland/desktop/sessiond/main.c` の `main_console_back`）。design §6 の「理由の 1 行が画面に出るか」はこの行で見る。

build: `make ZEDBSD_CONFIG=plan/ws270/tests/config-riscv64-virt-desktop.mk BUILD=build/ws270-p003 -j16 build/ws270-p003/native.img` rc=0。C の warning は 15 件で全部 openssl（外部 package、`packages-riscv64`）、自前の code は 0。disk-image.mk を変えた後の再 build の warning は 0。host 試験: 生成の cfg の中身と、2 度目の make が何もしない事（上の 3）。QEMU は起動していない。

## T1 への依頼（Q1 が投げる）

> ws270-p003（P2、q1342）。QEMU riscv64-virt の desktop の組の image で、graphical login の「display が無い時の道」・USB keyboard の text login・service・mouse の event・`keiland-preview` の static の実行を 1 回の boot で見る。worktree は P2 の commit を merge した main。判定は画面（PNG）だけ、serial・console の log は読まない。
>
> 1. build: `make ZEDBSD_CONFIG=plan/ws270/tests/config-riscv64-virt-desktop.mk BUILD=build/t1-ws270-p003 -j16 build/t1-ws270-p003/native.img`、`cp build/t1-ws270-p003/native.img build/t1-ws270-p003/native-nvme.img`（U-Boot の virtio-blk と kernel の NVMe が同じ file を開かないため。片付けは Q1）。
> 2. 走り（1 回、時間を測る: QEMU の開始から login の prompt まで）:
>
> ```
> BOOT_MODE=riscv64-uboot BOOT_TIMEOUT=900 \
> QEMU_EXTRA="-drive if=none,id=nv,file=$PWD/build/t1-ws270-p003/native-nvme.img,format=raw -device nvme,drive=nv,serial=p003 -device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 -device usb-mouse,bus=xhci.0" \
> BOOT_PATTERN=$'sessiond: there is no greeter or no display; the console login takes over\n^(?:\\S+ )?login: ?' \
> BOOT_THEN=$'keys:root\nexpect:[Pp]assword\nkeys:root\nexpect:^root@\\S+:~\\$\nkeys:mount | grep nvme\nexpect:nvme\\S* on / type ufs\nkeys:service status\nexpect:syslogd\nkeys:evdev-probe -b usb -n Mouse -t 5000 -r 20000 > /tmp/m.txt &\nkeys:sleep 3\nmouse:20,10\nmouse:-5,8,left\nkeys:sleep 25; head -1 /tmp/m.txt | cut -c1-40; grep -c type=2 /tmp/m.txt\nexpect:EVDEV node=/dev/input/event\nexpect:^[1-9][0-9]*\\s*$\nkeys:/usr/libexec/keiland-preview --info < /usr/share/keiland/wallpapers/Lakeside.png > /tmp/i; echo rc=$?; cat /tmp/i\nexpect:^rc=0\\s*$\nexpect:keiland-preview-info 1\nexpect:kind=png\nkeys:/usr/libexec/keiland-preview --width=64 --height=64 < /usr/share/keiland/wallpapers/Lakeside.png > /tmp/p; echo prc=$?; wc -c < /tmp/p\nexpect:^prc=0\\s*$' \
> plan/tools/boot-test.sh build/t1-ws270-p003/native.img
> ```
>
> 3. 合格: 画面に sessiond の 1 行と login の prompt（E: display が無い時の道で console に戻る）、USB keyboard（`keys:`）で root の login、`/ type ufs` が nvme、`service status` が出る、`/tmp/m.txt` の 1 行目が `EVDEV node=/dev/input/event…` で REL（type=2）の event が 1 件以上（C: mouse）、`keiland-preview --info` が rc=0 と `keiland-preview-info 1`・`kind=png`、画像の preview が prc=0（F: static の実行）。
> 4. 返す物: PNG（login の後と最後の画面）、各 expect の PASS・FAIL、login の prompt までの秒数（WS270-D2 の見直しの材料）、`service status` の画面（要約の 1 行の有無、design §6）、`wc -c` の値。**見込みの外れ**: sessiond の行が出ない（login の prompt だけ出る）→ greeter の service が起動していない、FAIL として PNG を添える。login の prompt が出ずに止まる → 画面の PNG と時間を添えて FAIL（解析しない）。`evdev-probe` が `EVDEV none` → usb-mouse の名前が違う可能性、`ls /dev/input` を 1 行足して 1 回だけ流し直してよい。

## 結果

T1 待ち（2026-10-20）。cleared は T1 の結果を Q1 が判定してから。

## 残り

- WS270-D2（Venus で先に描くか）の見直しは T1 の起動の時間を見てから（design §4）。
- greeter の失敗の道（READY の時間切れ・連続の失敗）は VF2 の実機だけ（p004）。
