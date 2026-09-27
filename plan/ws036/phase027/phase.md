<!-- awesome-plan project=zedbsd record=ws036p027 -->

# ws036-p027: rpi4 の起動 parameter（DTB の `/chosen/bootargs`）

Phase ID: `ws036-p027`
Parent: [WS036](../ws.md)
Status: **cleared**（2026-09-27、WS036 の subagent）
Phase disposition: normal

## 目的

rpi4 の kernel は起動 parameter を受け取らず、legacy autoroot（FAT の boot partition の隣の UFS root）で起動していた。firmware が
`cmdline.txt` から作る DTB の `/chosen/bootargs` を `hal_get_arch_handoff("boot.command-line")` で kernel に渡し、amd64・pcat・pc98 と同じ
parameter（`rootpart=`・`init=`・`overlay-*`・`swap0=` 等）で起動を選べるようにする。

## 判断（2026-09-27 ユーザー）

調査（下の旧記録の要約）で、firmware の bootargs は Linux 向けの token（`rootwait`・`quiet` 等の `=` の無いもの）を含み、kernel の厳格な
parser が拒んで idle になると分かり、方針を尋ねた。ユーザーは **案 A（kernel の parser を全 platform で緩める: `=` の無い token と未知の名前は
無視して数える）** を選んだ（main session 経由、2026-09-27）。hal.h は変えない。

## 変更

| 所在 | 変更 |
| --- | --- |
| `src/kern/boot.c` の `kern_boot_parameters_parse()` | 区切りは空白・tab・CR・LF。`=` の無い token と未知の名前は数えて無視（最初の名前を診断用に残す）。既知の名前は従来どおり 1 回だけ、値が要る（空は `EINVAL`、重複は `EEXIST`）。非 ASCII と他の制御文字は従来どおり拒む。区切りの判定（`parameter_separator`・`skip_separators`） |
| `src/kern/vfs.c` | legacy autoroot（x86 以外）は、parameter が無いときに加え、**root を名指さない**（`rootpart`・`overlay-root`・`overlay-data` のどれも無い）parameter のときにも使う。firmware の行だけの起動は従来どおり |
| `src/hal/arm64/bsp-rpi4/fdt.c`・`fdt.h` | `/chosen` の `bootargs` の blob の中の位置と長さを記録 |
| `src/hal/arm64/bsp-rpi4/boot.c` | 起動の早い段階（DTB が確かに残っている間）に bootargs を HAL の buffer（`KERN_BOOT_PARAMETERS_STORAGE_SIZE`）へ写し、`"boot.command-line"` で返す。長すぎる行は収まる最後の空白で切る。無いときは NULL（従来どおり） |
| `plan/ws044/tests/rpi4-serial.sh` | `APPEND` で QEMU の `-append`（DTB の `/chosen/bootargs` になる） |

## 検証

| 試験 | 結果 |
| --- | --- |
| rpi4・amd64・pc98・pcat の `disk-image` | warning 0（pc98 は外部の Noct の既存の 1 件だけ） |
| `plan/ws036/tests/bootargs-rpi4.sh`（QEMU raspi4b） | firmware 風の Linux の行（9 個を数えて無視、legacy autoroot で UFS root）、tab と改行の区切り（2 個を無視）、`rootpart=/dev/mmcblk0p2`（native root で mount）、`-append` 無し（vendor の DTB 自身の bootargs `coherent_pool=1M 8250.nr_uarts=1 …` を受け取り legacy autoroot）。すべて login して guest で確かめた |
| `boot-test.sh` rpi4（raspi4b）・amd64（UEFI・NVMe） | PASS |
| pc98（`pc98-boot.py`） | login と `uname -a` |
| pcat（`BOOT_MODE=bios-ide`） | **login に届かない**。parameter の行は正しく解釈される（画面に `boot: parameters: boot0=UUID=… overlay-root=… overlay-data=… swap0=…`）が、VFS の overlay-data の mount が `loop1: write … error=17` で止まる。**この Phase の変更の無い tree（f6735de4）でも同じ**で、main の 3231b05f と e2750963 の間の他の WS の変更による回帰。main session が BUG-066 として WS073 に割り当てた（2026-09-27） |
| style-check | `src/kern/boot.c` 39 → 38、`fdt.c` 30 → 30、`vfs.c` 変わらず、bsp の `boot.c` 0 |
| 実機（Raspberry Pi 4 の firmware の実際の bootargs） | 未実施（ユーザー）。QEMU の vendor DTB の bootargs で同じ形の行を確かめた |

## 旧記録（2026-09-27 の調査の要約）

firmware の bootargs をそのまま渡すと、`=` の無い token・行末の制御文字で旧 parser が失敗し idle になる見込みだったため、
案 A（parser を緩める）・B（HAL で切り出す）・C（別 file）・D（据え置き）をユーザーに尋ねた。ユーザーは A を選んだ。
