<!-- awesome-plan project=zedbsd record=ws073p017 -->

# ws073-p017: 起動時の USB の列挙の再試行（BUG-036）、kernel の console の record 単位の排他（BUG-031）、間欠の bug の再現の試み

Status: cleared（2026-09-27）
Disposition: normal
Parent: [WS073](../ws.md)
Bugs: [BUG-036](../../bugs/BUG-036.md)（緩和、解決とは言わない）、[BUG-031](../../bugs/BUG-031.md)（修正、確認は部分的）、
[BUG-030](../../bugs/BUG-030.md)・[BUG-041](../../bugs/BUG-041.md)（再現せず）

## 道具

- [tests/boot-loop.sh](../tests/boot-loop.sh): guest を N 回起動し、毎回 SSH で `dmesg` の timeout・列挙の失敗の行を探し、command を 1 つ走らせる
  （`DISK=usb|nvme`、`EXTRA` で 2 台目の disk）。SSH が上がらない起動を数える。
- [tests/console-mix.sh](../tests/console-mix.sh): USB から N 回起動し、画面（QEMU の screendump の文字）の kernel の行を `dmesg` の record と比べる。

## BUG-036: 起動時の USB の列挙の一時的な失敗

- 再現（修正の前の kernel、main の測定用 image に差し替え）: 起動 disk を usb-storage にした 20 回で 2 回、2 台目の usb-storage を
  つないだ 20 回で 1 回、SSH が上がらなかった。1 回は serial の console で `usb0: port 6 enumeration failed (42)`（usb-net の port）と
  `ifconfig -a` が lo0 だけを確認（guest の操作で確かめた。QEMU の log では判定していない）。
- 原因の範囲: root port の列挙（`src/drivers/usb/usb.c`、reset・GET_DESCRIPTOR・SET_ADDRESS）の 1 段が ETIMEDOUT を返すと、その port は
  次の接続の変化まで使われなかった（再試行が無い）。1 段が時間切れになる理由（xHCI の event を取りこぼすのか QEMU の側か）は**未解明**。
- 緩和: `root_port_enumerate()` を分け、ETIMEDOUT・EIO なら reset から最大 3 回（`USB_ENUMERATE_TRIES`）繰り返す（失敗した列挙は device を
  解放してから戻るので再試行できる）。再試行のたびに `usb0: port N enumeration failed (42); retrying` を log に残す。hub の下の port は変えていない。
- 確認: 修正の kernel で、起動 disk を usb-storage、2 台目の usb-storage もつないだ 20 回: **20 回全て SSH が上がり、ue0 がある**。そのうち 5 回で
  最初の列挙が ETIMEDOUT になり（全て port 7、keyboard）、再試行で成功した（`dmesg`）。一時的な失敗は修正の前も同じ頻度で起きていたと見られ、
  修正の前はそれが network の port に当たると SSH が上がらなかった。

## BUG-031: 起動時の console の行の混ざり

- 原因（コードで確認）と修正: `kern_log_write()`（`src/kern/klog.c`）の console への写しが lock の外だった。record を写す間、CPU が
  `klog_mirror_owner` を持つ（上限つきの待ち、同じ CPU の入れ子は待たない、lock の順位には入らない）。
- 確認: USB の起動 15 回で、画面の末尾の kernel の行が `dmesg` の record と全て一致（混ざり 0）。画面に残るのは最後の約 30 行だけで、修正の前の
  発生率も同じ試験では測っていないので、**確認は部分的**（ticket は tracking のまま）。

## 間欠の bug の再現の試み（修正なし）

- BUG-030（usb-storage の I/O の `BOT CSW error=42`）: USB の起動 60 回（上の 3 つの loop）で I/O の timeout の行は 0。列挙の段の timeout は
  上のとおり起きる（同じ error 42 の系統）。tracking のまま。
- BUG-041（2 台目の NVMe の mount の ETIMEDOUT）: 2 台の NVMe の起動 20 回で全て mount でき、timeout 0。tracking のまま。

## 検証の範囲

- build: guest の vmunix（`-Werror`）。規約: `tests/style-diff.py`（usb.c・klog.c）0。
- boot test（lean native、この Phase の最後の kernel）: NVMe PASS（`build/ws073-p017/boot-native/login.png`）、USB（`BOOT_MODE=uefi-usb`）PASS
  （`build/ws073-p017/boot-native-usb/login.png`）。
- QEMU（KVM）だけ、amd64 だけ。実機は未実施。
