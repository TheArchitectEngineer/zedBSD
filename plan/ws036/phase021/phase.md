<!-- awesome-plan project=zedbsd record=ws036p021 -->

# ws036-p021: 全 platform の回帰と規約の確認

Phase ID: `ws036-p021`
Parent: [WS036](../ws.md)
Status: **cleared**（2026-09-27、WS036 の subagent。main の 3231b05f から）
Phase disposition: normal

## 範囲

WS036 の最終確認。amd64・pcat・pc98・rpi4 の build（warning 0）と QEMU の起動、WS036 が足した・変えた C の source の全文の規約の確認。

## 見つけて直したこと

| 所在 | 問題 | 変更 |
| --- | --- | --- |
| `src/kern/sched.c` の `idle_mask_clear()` | 2026-09-26 の変更で 64-bit の `__atomic_fetch_and` を直接使い、i386（pcat・pc98）が `-Werror,-Watomic-alignment` で build できなかった（i386 の HAL は 64-bit の atomic を割り込みの禁止で真似る） | kernel の atomic の interface（`atomic_u64_load_acquire`・`atomic_u64_compare_exchange`）の compare and exchange の loop で bit を消す。HAL は変えない |
| `src/drivers/platform/rpi4/rpi4-console.c`（p012 の新規 file） | style-check 6 件と全文の規約の違反（file scope の変数の説明、Boolean の式、`if` の説明、呼び出しの直接の return） | 全文の規約に合わせた（style-check 0 件）。振る舞いは変えない |

## 検証

外部 package を外した config（amd64 は `config-amd64.mk` から clang・libcxx・openssh・openssl・remacs・noct・firmware、pcat・pc98 は firmware を外し serial mirror を足さない。
rpi4 は `config/ci/config-rpi4.mk` のまま）。package は WS036 の変更と関係しない。

| platform | build | 起動（QEMU） |
| --- | --- | --- |
| amd64 | `disk-image` warning 0 | `boot-test.sh`（UEFI・NVMe）PASS |
| pcat | `disk-image` warning 0 | `boot-test.sh`（`BOOT_MODE=bios-ide`）PASS |
| pc98 | `disk-image`（warning は外部の Noct の source 1 件と make の jobserver の通知だけ） | `pc98-boot.py`（PC-98 fork）で login と `uname -a`（i386） |
| rpi4 | `disk-image` warning 0 | `boot-test.sh`（`BOOT_MODE=raspi4b`）PASS。シリアルの login と `uname`・`dyntest`（6 回中 5 回。1 回は login の後の prompt を harness が待ち損ねた。ws044-p005 の記録と同じ現象で、続く 5 回は通った） |

規約（style-check、全文の規約で読んだもの）:

| file | 結果 |
| --- | --- |
| `src/drivers/platform/rpi4/rpi4-console.c`（WS036 の新規） | 0 件 |
| `src/rtld/elf.h` | 0 件 |
| `src/kern/sched.c` | main と同じ 86 件（増えていない。旧来の違反） |
| `rpi4-sdhci.c`・`src/rtld/rtld.c`・`src/hal/i386/lib.c` | WS036 の変更は lock・関数の範囲の囲み・mmio の 2 関数だけ。旧来の違反は WS036 の範囲外 |
| WS036 の makefile・python（p026・p028・p029） | C でないので style-check の対象外。変更は周りの書き方に合わせた |

実機（amd64 の機械・PC/AT・PC-98・Raspberry Pi 4）: 未実施（ユーザー）。

ws036-p027（起動 parameter）は kernel の parser を変えるので、その Phase でも 4 platform の起動を確かめ直す。
