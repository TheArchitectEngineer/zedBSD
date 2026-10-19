<!-- awesome-plan project=zedbsd record=ws270-p004 -->

# ws270-p004 VF2 の実機での画面なしの確かめ

- 親: [WS270](../ws.md)、Status: in-progress（2026-10-20 P1、q1355。image と手順の準備は済み、実機の確かめは未実施。実機待ち。ユーザーには今は頼まず、Q1 の「ユーザーが選べる試験の一覧」に載せる）
- 設計: [design.md](../design.md) 改訂 2.1 の §2.1 C・D・E、§2.2 の 2a、§3 の p004。

## 範囲

VF2 の実機で、desktop の組の image（graphical login が y）の boot、graphical login の「greeter の失敗の道」（E）、その後の `gpu-bars`・`display-inventory`（D）、USB keyboard の login と mouse の event（C）を、ユーザーの目視（画面の写真）で確かめる。描画（Keiland）はしない。HAL・toolchain・sessiond は変えない。

## 準備（P1、2026-10-20）

1. 試験の config: [tests/config-visionfive2-desktop-p004.mk](../tests/config-visionfive2-desktop-p004.mk)。[tests/config-visionfive2-desktop.mk](../tests/config-visionfive2-desktop.mk) から `ZEDBSD_GRAPHICAL_LOGIN := y`（`ZEDBSD_GRAPHICAL_BOOT := n` のままなので boot の行の `login=graphical` は 1 つだけ。生成の `build/ws270-p004/zedbsd-native.cfg` は `kernel=vmunix`・`rootpart=PARTLABEL=zedBSD-root`・`login=graphical`）、program に `mount sh evdev-probe gpu-bars display-inventory` を足した。noct・emacs は無い。
2. `powervr-firmware` は入れない（license の同意を担当・Q1 が代わりにしないため）。この image では powervr の firmware の読み込みが失敗し、render の node（gpu1）は出ない。**firmware 入りの版の作り方**（ユーザーが自分で同意する時だけ）: config の `ZEDBSD_USER_PROGRAMS` の末尾に `powervr-firmware` を足した写しを作り、`make ZEDBSD_CONFIG=<写し> BUILD=<dir> firmware-licenses`（`LICENSE.powervr` を読んで同意）、続けて `make ZEDBSD_CONFIG=<写し> BUILD=<dir> -j16 <dir>/native.img`（WS269 p007 の `config-p007.mk` と同じ流儀）。
3. `display-inventory` の platform が `amd64` だけで riscv64 の選択から落ちていた。`userland/tests/display-inventory/Makefile` を `amd64 visionfive2` にした（`gpu-bars` と同じ。main.c に arch の依存は無い）。riscv64 の PIE で入る事を `file` で確かめた。
4. build: `make ZEDBSD_CONFIG=plan/ws270/tests/config-visionfive2-desktop-p004.mk BUILD=build/ws270-p004 -j16 build/ws270-p004/native.img` rc=0（worktree `/home/awe/zedBSD-worktrees/p1`）。warning 0（外部 package は cache 済みで再 build されなかった）。大きさ: native.img 404,750,336 B（386 MiB）、rootfs の `du -sm` 73 MiB（既定の 256 MiB の root に入る）。sha256 の頭 `64aecfb2838b7abc`。`/bin/gpu-bars`・`/bin/display-inventory`・`/bin/evdev-probe` が入る。`/lib/firmware` は無い。QEMU は起動していない。

## E: 今の sessiond の振る舞い（code を読んだ見込み、未確認）

VF2 では display の node（gpu0、`src/drivers/gpu/powervr/` の dc8200）が出て、render の node（gpu1）は firmware が無いので出ない。

1. `main_wait_display`（`userland/desktop/sessiond/main.c:513-567`）は `/dev/gpu0` だけを見る。gpu0 があれば `hw.gpu.attaching` を見ずに直ちに抜ける。`hw.gpu.attaching` を数える driver は今 i915・nvrtx だけで、powervr は数えない（`grep attaching src/drivers/gpu/powervr` は空。WS269 p005 の必須項目）。**gpu1 は待たない**（design §2.2 の 2a は未実装。p006a の必須項目）。
2. sessiond は greeter（compositor の `--greeter`）を起動する。greeter は libvulkan の初期化（render の node が要る）で失敗する見込み。
   - 速く終わる場合: `SESSIOND GREETER failed`、retry の待ちは 1・2・4・8・16 s（`MAIN_RETRY_FIRST`・`MAIN_RETRY_LONGEST`、`main_retry_delay`）。6 回連続（`SESSIOND_GREETER_FAILURES`）か、2 回以上で 90 s（`SESSIOND_FAILURE_STREAK_MS`）で console に戻る。boot から約 30〜40 s。
   - READY を言わずに止まる場合: 1 回 30 s（`GREETER_READY_MS`、`greeter.c:66`）で殺され、2 回目以降で 90 s を越えた所（3 回目の後、約 100 s）で戻る。
3. 戻る時、`sessiond_seat_restore()` の後、console に **`sessiond: the graphical login failed to start; the console login takes over`** を 1 行書き（`main_console_back`、`main.c:243`・`:791`）、init が console の getty を起こす（`login:`）。
4. 見込みの外れ: gpu0 が出ない時は「display が無い時の道」の行 `sessiond: there is no greeter or no display; …` になる（それも記録する）。greeter が成功して画面が変わる・止まったまま 3 分以上 `login:` が出ない時はその写真で返す。

## D: E の後

console の login の後、sessiond は display を返しているので、`gpu-bars` が claim できる（graphical の間に流すと EBUSY）。`display-inventory` は claim しないので順はどちらでもよいが、E → D の順にする。

## 実機の試験の文面（ユーザーが選べる試験の一覧に載せる。今は頼まない）

> **U?? VF2 の desktop の image（graphical login の失敗の道、gpu-bars、display-inventory、USB の入力）**（WS270 p004）
>
> image: `build/ws270-p004/native.img`（P1 の worktree の build。Q1 が main で作り直すなら `make ZEDBSD_CONFIG=plan/ws270/tests/config-visionfive2-desktop-p004.mk BUILD=build/uat/vf2-desktop -j16 build/uat/vf2-desktop/native.img`）、404,750,336 byte。powervr の firmware は入っていない。
>
> 1. SD に `sudo dd if=<image> of=/dev/sdX bs=4M conv=fsync status=progress`（device を取り違えない）。HDMI、USB keyboard と USB mouse をつなぐ。boot の switch は 00 のまま（QSPI の U-Boot）。
> 2. U-Boot の prompt（自動起動は SD の最初の認識に失敗する）で:
>    `mmc rescan` → `load mmc 1:1 ${kernel_addr_r} EFI/BOOT/BOOTRISCV64.EFI` → `bootefi ${kernel_addr_r} ${fdtcontroladdr}`
> 3. 見る行（E）: framebuffer の console に `vfs: rootpart … /dev/mmcblk0p2`、`powervr: firmware … failed (6); powered down`（firmware が無いので正しい）。その後 30 s〜2 分ほど待って **`sessiond: the graphical login failed to start; the console login takes over`** の 1 行と `login:`。代わりに `sessiond: there is no greeter or no display; …` が出たらそれを写す。3 分待っても `login:` が出なければ、その画面を写す。
> 4. USB keyboard で root / root で login（C の keyboard）。
> 5. `display-inventory`（D）: `DISPLAY-INVENTORY sequence=… count=N`、`output … connected=1 … mode=WxH@…`、`DISPLAY-INVENTORY done` を写す。
> 6. `gpu-bars`（D）: 画面が左から赤・緑・青・白の 4 本の縦の帯になり（3 秒、色を変えずにもう 3 秒）、console に戻って `GPU-BARS PASS rgba=1 bgra=1 WxH`。帯の写真と最後の行を写す。FAIL なら `GPU-BARS FAIL step=… error=…` を写す。
> 7. mouse（C）: `evdev-probe -b usb -n Mouse -t 5000 -r 20000 > /tmp/m.txt &`、mouse を動かしてボタンを押し、`sleep 25; head -1 /tmp/m.txt; grep -c type=2 /tmp/m.txt`。1 行目が `EVDEV node=/dev/input/event…`、数が 1 以上。
> 8. 返す物: 3・5・6・7 の画面の写真（3 は sessiond の行と login の prompt が見える物、6 は帯の写真も）。

## 残り

- 実機の確かめ（上の文面）。結果で C・D・E を判定し cleared にする。
- p006a（sessiond の gpu1 の待ち）は本 Phase の結果と WS269 p005 の attaching の数えを待つ。
