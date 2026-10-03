<!-- awesome-plan project=zedbsd record=ws073-p042 -->

# ws073-p042: BUG-116（xHCI の event の取りこぼし: EP0 の control が 1 秒、bulk の READ が 30 秒で時間切れ）の原因と修正

Status: cleared（2026-09-30、試験の担当の受け入れの試験。下の「受け入れの試験の結果」）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-116](../../bugs/BUG-116.md)（関係: [BUG-036](../../bugs/BUG-036.md) の列挙の時間切れも同じ機構と見られる）
Queue: main の依頼（2026-09-30「BUG-116 をなるべく短時間で修正」）。Queue の ID は main が記録する
Resume point: なし（cleared）。残課題は下の「残課題」

## 範囲

[ws073-p041](../phase041/phase.md) の試験で見つかった 2 つの形（EP0 の control の 1 秒の時間切れ `pending-events=2 iman=3`、bulk の READ の 30 秒の時間切れ
`pending-events=1 iman=2`）の原因を、xHCI の driver の event の処理と QEMU 10.0 の `hcd-xhci.c`・`msix.c` の突き合わせで特定し、直す。
受け入れの本数の試験（TCG・KVM で各 2×20）は別の担当が行う。

## 原因

driver は command（Address Device・Configure Endpoint など）を出す間、`command_ex()` が event ring を自分で poll するので、interrupter 0 の
IMAN.IE を 0 にし（`command_polling`）、command が終わったら IE を 1 に戻していた。この IE の上げ下げが、QEMU の xHCI + MSI-X で割込みを失う。

QEMU 10.0 の動き（`hw/usb/hcd-xhci.c`・`hcd-xhci-pci.c`・`hw/pci/msix.c`）:

1. event を置くとき `xhci_intr_raise()`: EHB=1・IP=1・USBSTS.EINT=1 を立て、**EHB が既に立っていれば何もしない**。IE=0 でも何もしない（IP は残る）。
   MSI-X なら `msix_notify()` を呼び、その返り値に関わらず IP を 0 に戻す（edge）。
2. IMAN の書き込み → `xhci_intr_update()`: **先に**「IP=1 かつ IE=1 なら `msix_notify()`（IP を 0 に）」、**その後に** `intr_update(IE)` で
   `msix_vector_use()` / `msix_vector_unuse()`（IE=0 で vector を unuse、IE=1 で use）。
3. `msix_notify()` は **vector が use されていなければ黙って捨てる**（`if (!dev->msix_entry_used[vector]) return;`）。`msix_vector_unuse()` は
   PBA の pending も消す。

したがって driver が IE=0 で poll している間に event が来ると（EHB=1・IP=1、割込みなし）、command の完了を取った後に IE=1 を書いた瞬間に
QEMU は IP の再送を試みるが vector はまだ unuse のままなので捨てられ、IP だけ 0 になる。ring に残った event と EHB=1 はそのまま、
以後の event は「EHB が立っている」ので割込みを起こさない。driver の handler は割込みでしか ring を読まないので、次の command が poll するまで
ring が止まる。

- **形 2（`iman=2`、IP=0、pending 1、bulk）**: command の完了 event の後ろに usb-storage の transfer event が既に置かれていた。poll が完了 event を取る
  ERDP の書き込みで QEMU が再 raise（IE=0 なので IP=1 だけ）→ IE=1 の復帰で捨てられて IP=0。
- **形 1（`iman=3`、IP=1、pending 2、EP0）**: 上の状態のあと、EP0 の control 転送の event が来て「EHB 立ち」で IP=1 のまま止まる。
  port reset 直後（Address Device の command の直後の GET_DESCRIPTOR）や usb-hid の attach（Configure Endpoint の直後の control）で出る。
- 診断の `erdp=...c8`・`...b8`・`...58` は全て bit 3（EHB）が 1 で、この機構と一致する。

p041 の「driver と QEMU の IMAN・EHB の扱いは整合」という判断は、`msix_notify()` の use の検査と `xhci_intr_update()` の順（notify → use）を
見ていなかった。実機の xHC でも IE の上げ下げで IP の再送があるかは実装依存で、Linux の xhci は command の間も IE を落とさない。

## 修正（`src/drivers/pci/pci-xhci.c`）

command の poll の間も IE を落とさない（`command_polling` と、`command_ex()` の前後の IMAN の 2 回の書き込みを削除）。poll と `xhci_irq()` は
もともと `event_lock` で event ring の consumer を共有し、handler は command の完了 event を `command_event` で poll に渡すので、この設計に
mask は要らなかった。poll している CPU は割込み禁止なので、その CPU 宛ての MSI-X は command の後に届き、handler が ring の残りを取る。
別の CPU なら handler が並行して取り、transfer の完了の callback は `command_busy` が 0 になってから `xhci_completion_drain()` で流す（従来どおり）。

診断の行 `xhci: cancel ...` から `polling=` を外した（field が無くなったため）。安全網（待ちの中で ring を定期的に poll する等）は入れていない。
これは根の修正である。

## 確かめたこと（QEMU。実機は未実施）

- build: `make -j16 ZEDBSD_CONFIG=/home/awe/zedBSD-rpi4/config.mk BUILD=build/amd64 vmunix`（worktree）rc=0、warning 0（`build/ws073-p042/kernel.log`）。
  image は `sh plan/ws073/tests/kernel-image.sh build/amd64/vmunix build/ws073-p042/guest.img`。
- clang-format: 未実施（host に無い）。`git diff --check` は通る。
- 再現の試験（`build/ws073-p042/run.sh`、`usb-stress.sh` を 2 本並列、各回の dmesg は `build/ws073-p042/<tcg|kvm>/gK-bN/dmesg-1.txt`）: 下の表。
- boot test: 下の表。

| 試験（修正後の kernel、`build/ws073-p042/guest.img`） | 結果 |
| --- | --- |
| TCG、`run.sh tcg tcg 10 0`（2 本並列 × 10 = 20 回、起動のみ、起動ごとに image を複写、`build/ws073-p042/tcg/`） | SSH 20/20。`xhci: cancel` **0**、keyboard の attach の失敗 **0**（`driver=usb-hid` 20/20）、usb-storage の `error=` 0、列挙の再試行（`enumeration failed (42); retrying`）**0**。修正前（p041 の受け入れ、同じ条件）は 40 回で cancel 7・attach の失敗 2・再試行 6。1 回 19〜27 秒 |
| KVM、`run.sh kvm kvm 5 1`（2 本並列 × 5 = 10 回、丸読み 1 回、`build/ws073-p042/kvm/`） | SSH 10/10。`xhci: cancel` 0、attach の失敗 0、`error=` 0、再試行 0、`dd` 10/10 が 2216689664 bytes。修正前は 40 回で cancel 7・attach の失敗 3・READ の 30 秒の時間切れ 1。1 回 331〜479 秒 |
| boot test（`bash plan/tools/boot-test.sh`、`BOOT_MODE=uefi-usb`、TCG の stress と並行） | PASS、`build/ws073-p042/boot-test/login.png`（画面に `usb-hid: event device ... report-bytes=8`・`driver=usb-hid`、sshd・getty の起動、login prompt。usb-storage の error なし） |
| `serial.py` の login | 未実施 |

本数は少ない（TCG 20・KVM 10）。修正前の率（TCG で 5〜6 回に 1 回 cancel）からは、TCG 20 回で 0 は有意だが、受け入れの本数は引き継ぎ。
注: `plan/tools/boot-test.sh` は bash の script（`sh` で呼ぶと 45 行目で syntax error）。


## 試験の担当への引き継ぎ（受け入れ）

kernel は `wt/ws073` の最後の commit（この Phase の diff は `src/drivers/pci/pci-xhci.c` だけ）。image は
`sh plan/ws073/tests/kernel-image.sh build/amd64/vmunix build/<dir>/guest.img`（`build/ws073-p042/guest.img` がそのまま使える）。
runner は `build/ws073-p042/run.sh LABEL MODE BOOTS PASSES`（2 本並列、各回の dmesg を保存、行ごとに `cancel lines`・`error= lines`・`attach-failed`・
`usb-hid` を出す）。判定は SSH の `dmesg` だけ（console・serial の log は読まない）。

1. TCG: `sh build/ws073-p042/run.sh tcg20 tcg 20 0`（2×20、起動のみ）→ `xhci: cancel` 0、`attach-failed` 0、usb-storage の `error=` 0、
   `driver=usb-hid` が毎回 1 以上。修正前の率は TCG 75 回中 cancel 16・keyboard の失敗 8（p041 の受け入れの試験）。
2. KVM: `sh build/ws073-p042/run.sh kvm20 kvm 20 1`（2×20、丸読み 1 回）→ 同じ条件で 0、`dd` が毎回 2216689664 bytes。修正前は KVM 40 回中
   cancel 7・keyboard の失敗 3・usb-storage の READ の 30 秒の時間切れ 1。
3. 回帰: (a) 丸読み（2 の `dd`）、(b) usb-net（SSH が毎回つながる = 上の試験そのもの）、(c) keyboard（`plan/tools/boot-test.sh` `BOOT_MODE=uefi-usb` で
   login prompt、`plan/tools/guest/serial.py` で login できる）。
4. 受け入れ: 1・2 で 0、3 が通る。そのうえで BUG-116 を resolved にし、この Phase を cleared にする。BUG-036 の列挙の時間切れ（`enumeration failed (42)`・
   再試行の行）も同じ試験で数え、0 なら BUG-036 の「時間切れの原因」の欄にこの Phase を書く（resolved にするかは main の判断）。
5. 時間切れが出た場合: `dmesg` の `xhci: cancel ... pending-events=N iman=I erdp=E` を見る。N>0 かつ E の bit 3（EHB）=1 なら別の取りこぼしの経路で、
   その dmesg を保存して報告する。N=0 なら controller/QEMU 側（BUG-030 の形）。

## 受け入れの試験の結果（2026-09-30、試験の担当、QEMU。実機は未実施）

始める前に `git merge main -m WIP`（5d0c5ba8。`plan/known-bugs.md` の conflict は BUG-116 の行を worktree の側、BUG-115・117〜119 を main の側で解決）。
merge で USB・PCI の source は変わっていない。image は `build/ws073-p042/guest.img`（中の vmunix は 04:07 の `build/amd64/vmunix` と `cmp` で同一）。
runner は `build/ws073-p042/run.sh`、集計は `build/ws073-p042/summ.sh LABEL`。判定は SSH の `dmesg` だけ。

| 試験 | 回数 | SSH | `xhci: cancel` | attach-failed | `error=`（usb-storage を含む） | usb-hid の無い回 | 列挙の再試行（BUG-036） | `dd` 2216689664 bytes | 1 回の時間 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| TCG 2×20、起動のみ（`build/ws073-p042/tcg20/`） | 40 | 40/40 | 0 | 0 | 0 | 0 | 0 | （丸読みなし） | 19〜30 秒 |
| KVM 2×20、丸読み 1 回（`build/ws073-p042/kvm20/`） | 40 | 40/40 | 0 | 0 | 0 | 0 | 0 | 40/40 | 311〜497 秒 |

修正前の同じ条件（p041 の受け入れの試験）: TCG 75 回で cancel 16・keyboard の失敗 8・列挙の再試行 9、KVM 40 回で cancel 8（usb-storage の READ の
30 秒の時間切れ 1 を含む）・keyboard の失敗 3・列挙の再試行 4。

回帰:
- boot test（`BOOT_MODE=uefi-usb bash plan/tools/boot-test.sh build/ws073-p042/guest.img`）: PASS、`build/ws073-p042/boot-test-accept/login.png`
  （usb-hid の attach と login prompt、usb-storage の error なし）。
- `serial.py`: mirror を on にした kernel（config.mk の複写で `CONFIG_PCAT_SERIAL_MIRROR := y`、HEAD の source、warning 0）の image
  `build/ws073-p042/serial-accept/guest.img` を USB から KVM で起動し、serial で root の login（この image の base は root の password が空なので、Password の prompt に
  空で入る）→ `serial.py run` で `id` が uid=0、`xhci: cancel|attach-failed|error=` が 0、usb-hid あり。worktree の `build/amd64` は通常の config に戻した。

判定: 受け入れ（1・2 で 0、3 が通る）を満たす。cleared。BUG-116 を resolved にした。BUG-036 の列挙の時間切れも 80 回で 0（この Phase の修正の後）。

## 残課題

- `xhci: cancel` の診断と `xhci_cancel_diagnose()` は残してある（時間切れの時だけ出る）。落ち着いたら削るかは WS073 の判断。
