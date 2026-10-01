<!-- awesome-plan project=zedbsd record=ws105-p001 -->

# ws105-p001: Linux の試験の guest（QEMU の Debian 13）と操作の道具

Status: planned
Disposition: normal
Parent: [WS105](../ws.md)
Queue: なし
実行者: phase-runner（high）か phase-runner-mid。道具は `plan/tools/keiland-linux/`（main の範囲。subagent は自分の worktree で作り、main が merge して Tools 節に登録する）

## 目的

Keiland の Linux の compositor・app・gdm・WiFi・音を試す場所を作る（決定 D23、[design.md](../design.md) §7）。host（この開発機）の画面と入力は使わない。
この Phase は Keiland の code を書かない。

## 作る物（`plan/tools/keiland-linux/`）

| file | 役割 |
| --- | --- |
| `README.md` | この directory の道具の一覧と使い方（下の command をそのまま載せる） |
| `build-guest.sh` | Debian 13 の guest の disk image を作る |
| `guest.sh` | guest の起動・停止・SSH・file の転送・screenshot・入力の注入 |
| `install-guest.sh` | `DESTDIR` の tree（`build/keiland-linux/stage/opt/keiland`）を guest の `/opt/keiland` に写す |
| `png-probe.py` | PNG の画素の色を出す（判定に使う） |

### `build-guest.sh [OUT] [VARIANT]`

- `OUT` の既定は `build/keiland-linux/guest`。作る物: `$OUT/guest.img`（raw、ext4、8 GiB）、`$OUT/vmlinuz`・`$OUT/initrd.img`（guest の `/boot` から複写）、
  `$OUT/id_ed25519`（試験用の SSH の key、無ければ作る）。`VARIANT` は `base`（既定）か `gdm`（p009 で使う。base に gdm3 を足す）。
- 手順（sudo を使ってよい。AGENTS.md）:
  1. `mmdebstrap --variant=important --include=<package の一覧> trixie $OUT/rootfs.tar http://deb.debian.org/debian`
     （network の方針で取れないときは `http://ftp.jp.debian.org/debian` などを試し、取れなければ Phase を uncleared で止めて main に報告）。
  2. package（base）: `linux-image-amd64,systemd-sysv,udev,openssh-server,sudo,kmod,iproute2,mesa-vulkan-drivers,libvulkan1,vulkan-tools,weston,wpasupplicant,hostapd,iw,alsa-utils,kbd,rsync`。
     `gdm` の variant ではさらに `gdm3`（GNOME の一部が入る。重い）。
  3. `truncate -s 8G guest.img`、`mkfs.ext4 -F -d rootfs/ guest.img`（rootfs.tar を展開した directory から。`-d` が使えなければ loop の mount で写す）。
  4. image の中の設定（`debugfs` か loop の mount で書く、または mmdebstrap の `--customize-hook`）:
     - root の password を消し、`/root/.ssh/authorized_keys` に `id_ed25519.pub`。`/etc/ssh/sshd_config.d/keiland.conf` に `PermitRootLogin prohibit-password`。
     - 利用者 `kei`（uid 1000、group `video,input,audio,render,netdev`、password `kei`、同じ authorized_keys）。
     - hostname `keiland-guest`、`/etc/fstab` に `/dev/vda / ext4 defaults 0 1`、network は systemd-networkd の DHCP（`/etc/systemd/network/20-wired.network`、`Name=en*`）。
     - `/etc/modules-load.d/keiland.conf` は作らない（`mac80211_hwsim` は試験が必要な時に `modprobe` する）。
  5. `vmlinuz`・`initrd.img` を rootfs から `$OUT/` に写す。
- 冪等: `$OUT/guest.img` があれば何もしない（`--force` で作り直す）。

### `guest.sh COMMAND ...`

環境変数: `GUEST_DIR`（既定 `build/keiland-linux/guest`）、`GUEST_RUN`（既定 `build/keiland-linux/run`、pid・QMP の socket・log）、`SSH_PORT`（既定 2225）、
`GUEST_IMAGE`（既定 `$GUEST_DIR/guest.img`）、`GUEST_VENUS`（`1` で Venus、既定 0）。**他の agent と同時に使えるように、`GUEST_RUN` と `SSH_PORT` を変えれば別の guest が走る形にする。**

| COMMAND | 動き |
| --- | --- |
| `start` | QEMU を background で起動し、SSH が通るまで待つ（上限 180 秒）。既に走っていれば何もしない |
| `stop` | SSH で `poweroff`、30 秒で消えなければ QMP の `quit` |
| `ssh CMD...` | `ssh -i $GUEST_DIR/id_ed25519 -p $SSH_PORT -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@127.0.0.1 CMD...`。`SSH_USER=kei` で利用者 |
| `put SRC DST`・`get SRC DST` | `scp` で転送 |
| `screenshot PNG` | QMP の `screendump`（`format: png`）。`plan/tools/qmp.py` を使う |
| `key KEY...` | QMP の `input-send-event` で key を押して離す（名前は QEMU の `QKeyCode`、例 `ctrl alt f2`、`a`、`ret`） |
| `type TEXT` | 英数字の文字列を key の列にして送る |
| `move X Y`・`click X Y [BUTTON]` | QMP の `input-send-event` の `abs`（virtio-tablet。座標は 0〜32767 に写す。画面の大きさは `screenshot` の PNG から） |
| `status` | 走っているか、SSH が通るか |

QEMU の command（design.md §7.2）:

```
qemu-system-x86_64 -enable-kvm -m 4G -smp 4 -display none \
  -kernel $GUEST_DIR/vmlinuz -initrd $GUEST_DIR/initrd.img -append "root=/dev/vda rw console=ttyS0 quiet" \
  -drive file=$GUEST_IMAGE,format=raw,if=virtio \
  -device virtio-vga \
  -device virtio-keyboard-pci -device virtio-tablet-pci -device virtio-mouse-pci \
  -audiodev none,id=snd0 -device intel-hda -device hda-duplex,audiodev=snd0 \
  -netdev user,id=n0,hostfwd=tcp:127.0.0.1:$SSH_PORT-:22 -device virtio-net-pci,netdev=n0 \
  -qmp unix:$GUEST_RUN/qmp.sock,server,nowait -serial file:$GUEST_RUN/serial.log \
  -pidfile $GUEST_RUN/qemu.pid -daemonize
```

- `GUEST_VENUS=1` のとき: `-device virtio-vga` を `-device virtio-vga-gl,hostmem=4G,blob=true,venus=true` にし、`-display egl-headless` を足す（host の Vulkan は lavapipe）。
  既定は Venus 無し（guest の Vulkan は guest の lavapipe）。
- serial の log は残すが、**判定に使わない**（AGENTS.md の zedBSD の規則に揃える）。判定は SSH の command の結果と screenshot。

### `install-guest.sh [STAGE]`

`STAGE` の既定は `build/keiland-linux/stage`。`tar -C $STAGE/opt -cf - keiland | guest.sh ssh 'rm -rf /opt/keiland && tar -C /opt -xf -'`。
`$STAGE/usr/share/wayland-sessions/keiland.desktop` があれば同じく写す（p009）。

### `png-probe.py PNG X Y [X Y ...]`

Python の標準 library（`zlib`・`struct`）だけで PNG（8 bit の RGB・RGBA、interlace 無し）を読み、点ごとに `X Y #RRGGBB` を出す。`--size` で `WIDTH HEIGHT`。

## 確かめ（完了の条件）

1. `plan/tools/keiland-linux/build-guest.sh` が通り、`guest.sh start` で起動して SSH が通る。
2. guest の中で（`guest.sh ssh ...`）:
   - `uname -r` が 6.x、`ls /dev/dri/card0 /dev/input/event* /dev/snd/controlC0` が全てある。
   - `vulkaninfo --summary` に `llvmpipe` が出る。`/usr/lib/x86_64-linux-gnu/libvulkan.so.1` がある。
   - `modprobe mac80211_hwsim radios=2 && iw dev` に 2 つの interface が出る。
   - `amixer -c 0 scontrols` に `Master` か `PCM` が出る。
3. `guest.sh screenshot build/keiland-linux/p001-console.png` の PNG に console の login の画面（文字）が出る（PNG をユーザーに見せる）。
   `png-probe.py --size` が 0 でない大きさを出す。
4. `guest.sh key ...`・`move`・`click` が QMP の error なしで返る。
5. `GUEST_RUN=build/keiland-linux/run2 SSH_PORT=2226 guest.sh start` で 2 つ目の guest が同時に起動する（並行の確かめ）。両方を `stop`。
6. `GUEST_VENUS=1` での起動と `vulkaninfo --summary` に `Virtio-GPU Venus` が出るか（出なければ「Venus は使えない」と記録して PASS にしてよい。既定は Venus 無し）。

## 記録

- 作った image の大きさ、mmdebstrap の時間、入れた package の版（`guest.sh ssh dpkg -l mesa-vulkan-drivers libvulkan1 weston gdm3 linux-image-amd64`）。
- master の Tools 節への登録（main）: 「Linux の試験の guest と道具（WS105）: `plan/tools/keiland-linux/`」。

## 結果

（実行の後に書く）
