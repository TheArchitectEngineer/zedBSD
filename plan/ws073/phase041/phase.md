<!-- awesome-plan project=zedbsd record=ws073-p041 -->

# ws073-p041: BUG-030（起動時の USB mass storage の CSW の時間切れ）の原因と修正

Status: uncleared（2026-09-30、試験の担当の 2 回目の枠。受け入れ条件（main の判断で「usb-storage の error 0」に絞った）が KVM の 40 回中 1 回で満たせなかった。その 1 回は host の flush の待ちではなく BUG-116 と同じ guest の event の取りこぼし。下の「受け入れの試験の結果（2 回目）」）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-030](../../bugs/BUG-030.md)
Queue: main の依頼（2026-09-29「BUG-030 をなるべく短時間で修正」）。Queue の ID は main が記録する
Resume point: BUG-030 の元の原因（SYNCHRONIZE CACHE の flush の待ち）は、修正の後 TCG 75 回・KVM 40 回で一度も出ていない（修正前は TCG 40 回中 7 回）。残る usb-storage の時間切れは [BUG-116](../../bugs/BUG-116.md)（guest の xHCI の event の取りこぼし）の機構で、BUG-116 の修正の後に KVM 2×20 をやり直すか、この 1 回を BUG-116 に移して BUG-030 を resolved にするかは main の判断

## 範囲

[ws073-p040](../phase040/phase.md) の再現（起動の途中の `BOT CSW error=42`）の原因を、xHCI の event の処理を読み、QEMU の debug の機能で切り分けて直す。
受け入れの本数の試験（TCG・KVM で各 20 回以上）は別の Phase・担当が行う。

## 原因

**guest 側の event の取りこぼしではない。** 起動時の `SYNCHRONIZE CACHE(10)`（0x35）に対して、QEMU の usb-storage が host の disk image の
flush（`fdatasync`）を待って CSW を返さず、usb-storage driver の CSW の段の 5 秒の時間切れ（`BOT_TIMEOUT_MS`）に掛かっていた。

切り分けの証拠（QEMU、TCG、`tests/usb-stress.sh` を 2 台並列、起動だけ。再現は 6 回中 6 回（QEMU の trace 付き）、3 回中 1 回（trace なし））:

1. xHCI の driver に診断を入れた（cancel の直前に event ring の未処理の event の数と IMAN・USBSTS・ERDP、cancel 中の request に届いた completion code を log）。
   時間切れの時点で毎回 `pending-events=0 iman=2 usbsts=0 erdp=...b0`（未処理の event なし、IE=1・IP=0・EHB=0）、Stop Endpoint の後の completion は
   **26（Stopped）residual=13**。つまり controller（QEMU）はその TD をまだ実行中で、guest は何も取りこぼしていない。
2. QEMU の trace（`-trace enable=usb_msd_*,usb_xhci_xfer_*,usb_xhci_ep_*,scsi_req_*`、`build/ws073-p041/before-fix/trace*/qemu.log`）で、時間切れの
   command は 6 回とも **tag 0x13f = SYNCHRONIZE CACHE(10)（`scsi_req_parsed ... command 53`、data 無し）**。`usb_msd_cmd_submit` の後に CSW の
   packet が `usb_msd_packet_async`（`s->req` が未完了）になり、`usb_msd_cmd_complete` が来ないまま guest の `usb_xhci_ep_stop` → `scsi_req_cancel` →
   `usb_msd_cmd_cancel`。QEMU の scsi-disk の SYNCHRONIZE CACHE は `blk_aio_flush` = host の image の `fdatasync` で、完了までの時間は host の
   dirty page の量と disk の速さで決まる。
3. 起動時だけ出る理由: `guest.py`（と `boot-test.sh`）は起動の直前に image を `cp --reflink=auto` で複写する。build の directory は ext4（reflink なし）なので
   2.2 GB の実複写になり、host の page cache に 2.2 GB の dirty page が残ったまま guest が起動する。guest の root の journal の commit が出す最初の方の
   SYNCHRONIZE CACHE がその書き戻しを全部待つ（2 台並列なら 4.4 GB、他の agent の QEMU の I/O も重なる）。起動の後の丸読み（READ(10) だけ）で
   出ないのは flush が無いから。TCG・KVM の違いではなく host の書き戻しの遅さで決まる（p017 の KVM 60 回 0 は host が空いていた）。
   BUG-030 の ticket の `BOT data dir=in error=42`（p005）も同じ host の I/O の遅さ（書き戻し中の read）と見られる（未検証）。

xHCI の event ring・IMAN・EHB の扱いは QEMU 10.0 の `hcd-xhci.c` の実装と突き合わせて整合していた（`command_ex` の polling 中の IE=0 と IP を残した
復帰、`event_take` の ERDP の EHB clear、`xhci_irq` の USBSTS.EINT の扱い）。取りこぼしの経路は見つからず、診断もそれを裏付けた。

## 修正

`src/drivers/usb/usb-storage.c`: BOT の段（CBW・data・CSW）ごとの timeout を command で決める `bot_stage_timeout()` を足した。CSW は command が終わる
まで返らないので、段の timeout は command の timeout である。SCSI disk の慣例（Linux の sd と同じ）に合わせ、

- `BOT_TIMEOUT_MS`: 5000 → **30000**（READ/WRITE など）
- `BOT_FLUSH_TIMEOUT_MS`: **60000**（SYNCHRONIZE CACHE(10)。device の cache 全体の書き戻し）
- command 全体の予算（reset と 1 回の再試行を含む `command_deadline`）は従来どおり段の timeout の 3 倍（90 秒、flush は 180 秒）。

これは根の修正である（host の flush が遅いことを transport の failure と誤認していた driver 側の timeout の問題）。安全網（event ring の poll）は要らず、
入れていない。副作用: 本当に応答しない device の failure が 5 秒ではなく 30 秒（flush 60 秒）で出る。disconnect は URB が DISCONNECTED で即時に返るので
待たない。

`src/drivers/pci/pci-xhci.c`: 診断の log を残した（cancel の直前の event ring・interrupter の状態 `xhci: cancel ...` と、cancel 中の request に届いた
completion `xhci: cancelled request ...`）。時間切れの時だけ出る。次に同種の時間切れが出たとき、guest の取りこぼしか controller 側かを 1 行で
切り分けられる。

`plan/ws073/tests/usb-stress.sh`: 毎回の `dmesg` を `GUEST_RUNTIME/dmesg-N.txt` に保存し、xHCI の診断の行も拾う。`QEMU_EXTRA` で QEMU の引数
（`-trace`）を足せる。`PASSES=0`（第 3 引数 0）で丸読みを省く。

## 確かめたこと（QEMU。実機は未実施）

- build: `make -j16 ZEDBSD_CONFIG=/home/awe/zedBSD-rpi4/config.mk BUILD=build/amd64 vmunix`（worktree の `build/amd64`）rc=0、warning 0
  （`build/ws073-p041/kernel.log`）。image は `tests/kernel-image.sh` で `build/ws073-p041/guest.img`。
- clang-format: 未実施（host にも `build/llvm/bin` にも無い）。`git diff --check` は通る。
- 修正前（診断だけの kernel）: TCG 2 台並列・起動だけ、trace なし 6 回中 2 回、trace 付き 6 回中 6 回、全て SYNCHRONIZE CACHE の CSW の時間切れ
  （`build/ws073-p041/before-fix/`）。
- 修正後: 同じ条件（TCG 2 台並列、trace 付き、起動だけ）で 4 回中 4 回 error 0（`dmesg` に `error=`・`xhci: cancel` なし）。QEMU の trace では
  6 回とも時間切れになっていた tag 0x13f の SYNCHRONIZE CACHE が毎回 `usb_msd_cmd_complete status 0, tag 0x13f` まで届き、`usb_msd_cmd_cancel` は 0
  （`build/ws073-p041/trace{1,2}-{1,2}/`）。本数は少ない（受け入れの本数は引き継ぎ）。
- boot test（`plan/tools/boot-test.sh`、`BOOT_MODE=uefi-usb`、`build/ws073-p041/guest.img`、host は stress と並行で負荷あり）: PASS、
  `build/ws073-p041/boot-test/login.png`（画面に usb-storage の error なし、sshd・getty が起動し login prompt）。

## 試験の担当への引き継ぎ（受け入れ）

kernel は `wt/ws073` の最後の commit（この Phase の diff は `src/drivers/usb/usb-storage.c`・`src/drivers/pci/pci-xhci.c`・`plan/ws073/tests/usb-stress.sh`）。
image は `sh plan/ws073/tests/kernel-image.sh build/amd64/vmunix build/<dir>/guest.img`。

1. 再現の条件で 0 回: `MODE=tcg sh plan/ws073/tests/usb-stress.sh IMAGE 20 0` を 2 本並列（`GUEST_RUNTIME` を分ける）→ 各 20 回以上、
   `dmesg` の `error=`・`xhci: cancel` の行が 0。QEMU の trace を足すと再現率が上がるので、`QEMU_EXTRA="-trace enable=usb_msd_*"` 付きでも同じ本数。
   host に書き戻しの負荷がある状態（image の複写の直後）で走らせること。
2. KVM: `sh plan/ws073/tests/usb-stress.sh IMAGE 20 1` を 2 本並列 → 各 20 回以上、error 0（丸読み 1 回を含む）。
3. 回帰: (a) USB の disk の丸読み（上の 2 の `dd` が 2216689664 bytes で終わる）、(b) usb-net（SSH が毎回つながる = 上の試験そのもの）、
   (c) keyboard（`plan/tools/boot-test.sh` `BOOT_MODE=uefi-usb` で login prompt、`plan/tools/guest/serial.py` で login できる）。
4. 受け入れ: 1・2 で error 0、3 が通る。そのうえで BUG-030 を resolved にし、この Phase を cleared にする。
5. 時間切れが出た場合: `dmesg` の `xhci: cancel ... pending-events=N` と `xhci: cancelled request ... completion=C` を見る。N>0 なら guest の取りこぼし
   （この Phase の結論が覆る）、N=0 かつ C=26 なら controller/QEMU 側で、QEMU の trace で command を特定する。

## 受け入れの試験の結果（2026-09-30、試験の担当、QEMU。wrap up で途中）

image は `build/ws073-p041/guest.img`（中の vmunix は `build/amd64/vmunix` と `cmp` で同一。`make ... vmunix` は no-op、warning 0）。
runner は `build/ws073-p041-accept/run.sh`（`usb-stress.sh` を 2 本並列、起動ごとに別の `GUEST_RUNTIME`、各回の `dmesg-1.txt`・`qemu.log`・trace を保存）。
`guest.py` が起動のたびに image を 2.2 GB 複写するので、全ての回が「複写の直後」の条件である。判定は SSH の `dmesg` だけ。

| 組 | kernel | 回数 | SSH | usb-storage の error（BOT/`op=28`） | `xhci: cancel` の起きた回 | keyboard の attach の失敗 | 列挙の再試行 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| TCG、trace なし、2×20（`tcg/`） | 修正後 | 40 | 40/40 | **0** | 7（全て EP0） | 2 | 6（port 7 が 5、port 6 が 1） |
| TCG、`-trace enable=usb_msd_*`、2 本並列（`tcg-trace/`、途中で停止） | 修正後 | 35 | 35/35 | **0**（trace に `usb_msd_cmd_cancel` 0） | 9（全て EP0） | 6 | 3 |
| 基準: TCG、trace なし、2×20（`base-p040-tcg/`） | 修正前（`build/ws073-p040/guest.img`、p041 の diff だけが無い） | 40 | 40/40 | **7 回**（`BOT CSW error=42`） | （診断なし） | 6 | 1 |
| KVM 2×20（丸読み 1 回を含む） | 修正後 | 未実施 | | | | | |

1 回の起動は TCG で 26〜34 秒（SSH の dmesg まで）。

1. **usb-storage（BUG-030 の本体）**: 修正後 75 回で 0（修正前は同じ条件で 40 回中 7 回）。60 秒・30 秒の timeout が足りない回は無かった。
2. **別の問題: EP0 の control の 1 秒の時間切れ（修正の前からある）**: 修正後の 16 回の `xhci: cancel` は全て `endpoint=1`（EP0）で、slot 3（keyboard）が 15、
   slot 2（usb-net）が 1。形は毎回 `pending-events=2 first-type=32 iman=3 usbsts=8 polling=0 command-busy=0 irq-busy=0`、cancel 中の completion は
   **1（Success）residual=0**。つまり transfer event は ring に届き、IMAN.IP・USBSTS.EINT も立っているのに、1 秒（`USB_HID_CONTROL_TIMEOUT_MS`・
   `USB_CONTROL_TIMEOUT_MS`）の間 guest の割り込みの handler が event を取っていない＝**guest 側の取りこぼし（割り込みが届かないか処理されない）**。
   列挙の段なら再試行（BUG-036 の緩和）で回復するが、usb-hid の attach の段だと keyboard が無いまま起動する（修正後 75 回で 8 回、修正前 40 回で 6 回）。
   修正前の kernel でも同じ率で起きており、p041 の修正の退行ではない。BUG-030 の CSW の時間切れ（`pending-events=0`、completion 26）とは形が違う。
   BUG-036（列挙の時間切れ、原因未解明）と同じ根と見られる（未証明）。
3. 丸読み（`dd` 2216689664 bytes）: 未実施（KVM の組で行う予定だった）。usb-net: SSH は 115/115 回つながった。keyboard: 上の 2 のとおり 115 回中 14 回
   attach に失敗（修正前後とも）。boot test の画面と `serial.py` の login は未実施（p041 の修正直後の boot test は `build/ws073-p041/boot-test/login.png` で PASS）。

次の手: (a) EP0 の件を別の bug として扱う（BUG-036 に追記するか新しい ticket、main の判断）。gdbstub で `xhci: cancel` の時点の MSI-X の table・PBA と
CPU の割り込みの状態を見る（IP=1 なのに handler が走っていない理由）。(b) BUG-030 の受け入れ条件を「usb-storage の error 0」に絞るなら、KVM 2×20 と
boot test を足せば clear できる見込み。

## 受け入れの試験の結果（2 回目、2026-09-30、試験の担当、QEMU）

main の判断（2026-09-30）: 受け入れ条件は「usb-storage の error 0」。EP0 の `xhci: cancel` は [BUG-116](../../bugs/BUG-116.md) として別に扱い、対象外。
始める前に `git merge main -m WIP`（5dfe65e0）。merge で変わったのは userland の header だけで、kernel の source は同じなので、image は
`build/ws073-p041/guest.img` のまま（vmunix は通常の config での再 build と `cmp` で同一）。

| 試験 | 結果 |
| --- | --- |
| KVM、`usb-stress.sh IMAGE 1 1` を 2 本並列 × 20（`build/ws073-p041-accept/kvm/`、各回 319〜520 秒） | SSH 40/40、丸読みの `dd` 40/40 が 2216689664 bytes。**usb-storage の error 1 回（g2 の 12 回目）**。ほかに EP0 の `xhci: cancel` が 7 回（usb-net 4、keyboard 3、BUG-116）、keyboard の attach の失敗 3 回、列挙の再試行 4 回 |
| boot test（`BOOT_MODE=uefi-usb`、`build/ws073-p041-accept/boot-test/login.png`） | PASS。画面に usb-hid の attach と login prompt、usb-storage の error なし |
| `serial.py` の login（mirror 付きの kernel の image `build/ws073-p041-accept/guest-serial.img`、USB の起動 disk、KVM） | root で login でき、`serial.py run` で `id` が uid=0、usb-storage の error 0、usb-hid が attach 済み。注: この image の base（2026-09-26）は root の password が空で、現行の `serial.py login` が送る password「root」では `Login incorrect`（道具と image の年代の差。kernel とは無関係）。Password の prompt に空の password で入った |

usb-storage の 1 回（`g2-b12/dmesg-1.txt`）:

```
xhci: cancel slot=1 endpoint=3 pending-events=1 first-type=32 iman=2 usbsts=8 erdp=7fe9a358 dequeue=53 polling=0 command-busy=0 irq-busy=0
xhci: cancelled request slot=1 endpoint=3 completion=1 residual=0
usb-storage: BOT data dir=in error=42 actual=0 expected=8192
```

READ の data の段（8192 bytes）が 30 秒の timeout に掛かったが、transfer event は ring にあり（`pending-events=1`）、completion は 1（Success）
residual=0。つまり controller は転送を終えて event を置いていたのに、guest が 30 秒の間それを取らなかった。BUG-030 の形（`pending-events=0`、completion 26、
QEMU が CSW を保持）ではなく、BUG-116（EP0 で `pending-events=2`）と同じ guest の取りこぼしで、bulk の endpoint でも起きることを示す。IMAN は 2（IP=0、IE=1）で
USBSTS.EINT=1（BUG-116 の EP0 の回は IMAN=3）。直前の同じ起動で keyboard の EP0 の取りこぼしも起きている。その後の I/O error（`op=28 ... error=`・`loop0`）は
無く、reset と再試行で回復した。

判定: 受け入れ条件（usb-storage の error 0）は満たさないので uncleared。ただし BUG-030 の原因（flush の待ち）の再発ではない。

## 残課題

- `src/drivers/usb/usb-uas-disk.c` の UAS の command も 5000 ms の timeout で、同じ host の flush の遅さに掛かりうる（QEMU の harness は BOT なので
  未再現）。UAS を扱う WS で同じ慣例（30 秒・flush 60 秒）に揃える。
- `plan/tools/guest/guest.py`・`boot-test.sh` の image の複写を reflink の効く FS か `dd oflag=direct`＋`sync` にすると、起動の flush の遅さ自体を
  減らせる（試験の道具の改善、任意）。
- 実機（USB stick）での確認は未実施。実機の stick の SYNCHRONIZE CACHE も秒単位のことがあり、この修正はそこにも効くはず（未検証）。

## 試験の道具と成果物

- `plan/ws073/tests/usb-stress.sh`（p040 で足し、この Phase で拡張）。並列の起動は `build/ws073-p041/stress-tcg.sh`・`stress-trace.sh`（git の外）。
- 修正前の証拠: `build/ws073-p041/before-fix/`（`trace*.txt`、各起動の `dmesg-1.txt`・`qemu.log`）。修正後: `build/ws073-p041/trace*`、boot test は
  `build/ws073-p041/boot-test/login.png`。
- QEMU 10.0 の `hcd-xhci.c`・`dev-storage.c` は突き合わせのために読んだだけで、tree には入れていない。

## main の判断（2026-09-30）

- 受け入れの条件を「usb-storage の error 0」に絞る。`xhci: cancel` の行は EP0 の別の症状で、[BUG-116](../../bugs/BUG-116.md) として起票した。
- 修正は main にマージした（TCG で修正の前 40 回中 7 回 → 修正の後 75 回で 0）。cleared にする残りは KVM の 2×20（丸読みを含む）と boot test。
  次の枠で行う。
