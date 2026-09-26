<!-- awesome-plan project=zedbsd record=ws036p027 -->

# ws036-p027: rpi4 の起動 parameter（DTB の `/chosen/bootargs`）

Phase ID: `ws036-p027`
Parent: [WS036](../ws.md)
Status: planning（人間の判断待ち）
Phase disposition: normal
Queue: なし

## 目的

rpi4 の kernel は起動 parameter を受け取らず、`kern_boot_parameters_initialize(NULL, 0)` の既定（legacy autoroot:
FAT の boot partition の隣の UFS root）で起動している。firmware が `cmdline.txt` から作る DTB の `/chosen/bootargs` を
`hal_get_arch_handoff("boot.command-line")` で kernel に渡し、amd64・pcat・pc98 と同じ parameter（`root=`・`init=`・
`overlay-*`・`swap0=` 等）で起動を選べるようにする。

## 調査（2026-09-27、WS036 の subagent）

### HAL の差分の扱い

- `include/hal/hal.h` は変わらない。`hal_get_arch_handoff()` の契約（名前で handoff の object を返す。無いものは NULL）の中で、
  `src/hal/arm64/bsp-rpi4/boot.c` が `"boot.command-line"` を返すようにする実装の補完である。`"boot.command-line"` は
  amd64・pcat・pc98 の HAL が既に返し、`src/kern/main.c` が全 platform で読む名前である。
- 2026-09-25 の規則（Guardrail「hal.h を変えない `src/hal/` の実装の変更は承認を要しない」）により、差分そのものは承認なしで
  適用できると読める。「HAL の差分が要る（承認待ち）」という p012 の記録は規則の変更の前（2026-09-24）に書かれた。

### 止める理由: 実機の bootargs を kernel の parser が受け付けない

kernel の parser（`src/kern/boot.c` の `kern_boot_parameters_parse()`）は厳格で、次のどれかで **parse の失敗 → idle**
（`boot: parameter parsing failed; entering idle.`）になり、起動しない:

- `=` の無い token（`rootwait`・`quiet`・`splash` 等）。
- 空白以外の制御文字（`cmdline.txt` の末尾の改行が firmware の版によって残る）。
- 既知の名前の重複。

Raspberry Pi の firmware は `/chosen/bootargs` に自分の parameter（`coherent_pool=1M 8250.nr_uarts=0 snd_bcm2835.* video=...
vc_mem.* smsc95xx.macaddr=...`）を前置し、`cmdline.txt` が無いときは Linux 向けの既定の行（`console=... root=/dev/mmcblk0p2
rootfstype=ext4 rootwait` 等）を使う。したがって bootargs をそのまま渡すと、**実機では cmdline.txt の中身に関わらず起動しなくなる
見込みが高い**。QEMU（`-dtb` で `-append` 無し）は bootargs を作らないので、QEMU の試験ではこの失敗が見えない。

### 選択肢（人間の判断が要る）

| 案 | 内容 | 影響 |
| --- | --- | --- |
| A | kernel の parser を緩める: `=` の無い token と未知の名前は無視して数えるだけにする（全 platform 共通の振る舞いの変更） | 他の platform の boot の契約（「誤りがあれば記録を空にして idle」）が変わる |
| B | rpi4 の HAL が区切り（例: `--` の後、または `zedbsd.` の接頭辞の token だけ）を切り出して渡す | HAL が parameter の方針を持つ。cmdline.txt の書き方が rpi4 だけ特殊になる |
| C | rpi4 は bootargs を使わず、FAT の boot partition の file（例 `zedbsd.txt`）を kernel が読む | firmware に依存しないが、kernel の起動の流れに rpi4 だけの経路が増える |
| D | 今のまま（legacy autoroot）。parameter は image の既定で固定 | 変更なし。root・init を起動時に選べない |

既定（可逆）: **D のまま据え置き**、この Phase は planning に置く。A〜C のどれにするかをユーザーに尋ねる。
どの案でも、実機での確認（firmware の実際の bootargs を読んだ起動）はユーザーが行う。

## 完了の条件（案が決まった後に確定する）

- rpi4 の kernel が `/chosen/bootargs`（または決めた経路）から parameter を受け取り、`root=`・`init=` で起動を選べる。
- parameter が無い・壊れているときに、今の legacy autoroot で起動できることが変わらない（QEMU）。
- 実機: 未実施（ユーザー）。
