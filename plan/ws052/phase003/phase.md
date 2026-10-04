<!-- awesome-plan project=zedbsd record=ws052p003 -->

# ws052-p003: ACPI の側（LPS0 の `_DSM`、wake の GPE、`_PSx`・`_PRx`・`_DSW`）

Phase ID: `ws052-p003`
Parent: [WS052](../ws.md)
Status: cleared（2026-10-05 Q1: host の試験 PASS（試験の table と 5330 の table）と T1-153 の CI の image の boot PASS（LPS0 の attach が boot の道で走っても起動を壊さない））。以前: in-progress（2026-10-05 P1 generation17。実装と host の試験は済み、vmunix は link まで（warning 0）。T1 の boot test の結果待ち）
Phase disposition: normal
Queue: q727 の続き（Q1 の 2026-10-05 の指示「p002 の残りを進め、その後 p003（ACPI の側、HAL に依らない）へ」）

## 範囲

- LPS0 の `_DSM` の通知（display off/on、entry/exit。Intel と Microsoft の 2 つの UUID）。
- wake の GPE だけを有効にする口と、元に戻す口。
- device の電源の helper: `_PS0`〜`_PS3`、power resource（`_PR0`〜`_PR3` の `_ON`・`_OFF`、参照の数）、wake の `_PRW`・`_DSW`（無ければ `_PSW`）、`_S0W`。
- 5330 の table での host の試験。
- Q1 の追加（2026-10-05）: 今の `_OSI` の答えと、5330 で電源ボタンの事象が届く経路を確かめ、HIDD（`INTC1070`）の扱いが要るなら範囲に入れる。
- HAL に依らない。HAL の API（`include/hal/hal.h`）は変えない。S0i3 の入口・出口そのもの（user の停止、CPU の idle）は p006。

## `_OSI` と電源ボタンの経路（5330 の DSDT の読み、2026-10-05）

- zedBSD の `_OSI`（`src/drivers/acpi/aml-osi.c`）は "Windows 2000"〜"Windows 2022" に真を返す。5330 の `_INI` は "Windows 2015" で `OSYS = 0x07DF` に
  する（host の試験で `\OSYS` は 0x7df）。
- `\_SB.PBTN._STA` が 0 になるのは `CondRefOf (\_SB.HIDD.BTLD) && S0ID == 1` かつ `OSYS >= 0x07DF && \_SB.HIDD.BTLD` の時だけ。`BTLD` を 1 にするのは
  `\_SB.HIDD.BTNL`（OS が HID event filter の driver を載せた時に呼ぶ method）だけで、zedBSD は呼ばない。だから PBTN は present のまま（host の試験で
  `\_SB_.PBTN._STA` は 0xf）。
- S0 で電源ボタンを押すと: EC の query 0x66（`_Q66`）→ `NEVT` → `ECG1` の bit 0 → `EV3 (1, 2)` → `BTNV` → `Notify (PBTN, 0x80)`。WS132 p002 の
  `acpi-power.c` は PNP0C0C の Notify 0x80 を `power-button` の事象にする。FADT の PWR_BUTTON の flag は 1（control method の電源ボタン、固定の
  電源ボタンではない）。
- wake では `_L18`（`ECG7`）から `EV3 (1, 1)` → `Notify (PBTN, 0x02)`。`acpi-power.c` は 0x02 を押下にしない（正しい: wake を押下と数えない）。
  PBTN と LID0 の `_PRW` は `PPRW`（GPE は Dell の SMI `EEAC (3, 0)` で実行時に決まる）。
- **結論: HIDD の扱いは要らない**（zedBSD が `HIDD.BTNL` を呼ばない限り、電源ボタンは PBTN の Notify 0x80 で来る）。将来 HIDD の driver を足して
  `BTNL` を呼ぶなら、電源ボタンは `Notify (HIDD, 0xCE/0xCF)` に変わるので、その時に扱う。UAT の項目（電源ボタンで systemevents に POWER が出る）は
  ws159-p005 にある。

## 実装（2026-10-05）

- `src/drivers/acpi/acpi-sleep.c`（新規）:
  - `drv_acpi_lps0_attach()`: `_HID INT33A1` か `PNP0D80`（文字列・EISA、`_CID` の package も）の present な device を探し、両 family の function 0 で
    function の mask を得る（bit 0 が無い family は無い扱い）。boot の時に `acpi-kern.c` が呼ぶ（無い platform は log だけ）。
  - `drv_acpi_lps0_enter()`: Intel 3 → Microsoft 3 → Microsoft 7 → Intel 5 → Microsoft 5。`drv_acpi_lps0_exit()`: Intel 6 → Microsoft 6 →
    Microsoft 8 → Intel 4 → Microsoft 4。mask に無い function は呼ばない。失敗しても残りを呼び、最初の失敗を返す。revision は両方 0
    （Microsoft の `_DSM` は 0 だけに答える。5330 の Intel の `_DSM` は revision を見ない）。
  - `drv_acpi_device_power_set(node, D0..D3cold)`: power を下げる時は `_PSx` → 新しい state の resource を取る → 古いのを放す。上げる時は resource を
    取る → `_PSx` → 古いのを放す。D3cold は resource を持たない D3hot（D3hot↔D3cold は resource だけ）。D3 から D1/D2 は EINVAL、`_PSx` も `_PRx` も
    無い D1/D2 は ENOTSUP。失敗したら元の state と resource のまま。device は最初の変更の時に D0（`_PR0` の resource を持つ）とみなす。
  - power resource は参照の数で共有（0→1 で `_ON`、1→0 で `_OFF`）。device ごとに持っている resource の list を保つ（`_PRx` が動的でも放す物を誤らない）。
  - `drv_acpi_device_wake_enable(node, D)`: `_PRW` の resource を取り、`_DSW (1, 0 = S0, D)`（無ければ `_PSW (1)`）、`_PRW` の GPE を arm。
    `drv_acpi_device_wake_disable(node)` はその逆。GPE が GPE block device の中の `_PRW` は ENOTSUP。`drv_acpi_device_wake_state()` は `_S0W`。
  - どの public の関数も interpreter を持って動く（`drv_acpi_enter`）。S0i3 の経路（p006）は 1 つの thread から呼ぶ前提。
- `src/drivers/acpi/acpi-event.c`:
  - GPE ごとの `armed`（wake の源の数）と `drv_acpi_gpe_wake_set(gpe, arm)`。
  - `drv_acpi_events_sleep_begin()`: arm された GPE だけを unmask、他は mask（runtime で mask していた wake 専用の GPE は古い status を clear して
    から）。PM1 の固定の event はそのまま。`drv_acpi_events_sleep_end(&woken)`: runtime の enable に戻し、sleep 中に最初に SCI を起こした GPE を返す
    （p006 の wake の理由）。
  - interrupt と thread の間で処理中の GPE は thread が `gpe_wanted()` で unmask する（sleep 中は arm されていて handler か method がある時、runtime は
    `enabled` の時）。handler も method も無い arm された GPE は 1 度だけ起こして mask のまま（level の嵐を避ける）。
- `src/drivers/acpi/acpi-ec.c`: EC の GPE を wake の源として arm したままにする（多くの機種で蓋・電源ボタン・AC は EC の query で来る。5330 の電源
  ボタンは query 0x66）。
- `include/drivers/acpi/acpi.h`: `enum drv_acpi_device_state`、`DRV_ACPI_GPE_NONE` と上の関数の宣言（HAL ではない）。

## 確認

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| host の試験（ASan/UBSan） | `plan/ws052/tests/run-host-sleep.sh` | PASS（2026-10-05）。`sleep.asl` の 61 行が `sleep.expected` と一致: LPS0 の順（`0x0313170515` / `0x0616180414`）、resource の参照の数（DEV0 D3: `_ON` A・B → `_PS3` → `_OFF` A、共有の PRB は最後の放しで `_OFF`）、D3hot↔D3cold、拒否（D1 の ENOTSUP、D3 から D1 の EINVAL、`_PRW` 無し ENOENT、GPE block device の ENOTSUP）、`_DSW`/`_PSW`、sleep 中は arm した GPE だけ enable、runtime の GPE 0x14 は sleep 中に起きず sleep の後に処理、woken 0x12、二重の begin は EBUSY・begin 無しの end は EINVAL |
| 5330 の table | 同上（2 つ目） | PASS。`ACPI: LPS0 \_SB_.PEPD, Intel functions 0x7f, Microsoft functions 0x1ff`、entry・exit の AML が失敗無し、TXHC・XHCI の `_PS3`/`_PS0`、TXHC の `_S0W` 3、LID0・PBTN の wake（`PPRW`、`_PSW`）の on/off、`\OSYS` 0x7df、`\_SB_.PBTN._STA` 0xf。模擬の GPE block は 0〜63 なので XHCI の GPE 0x6d は EINVAL（想定どおり、巻き戻しも AML の失敗無し）。模擬の memory は 0 を読むので値は実機のものではない |
| WS049 の回帰（触った acpi-event.c・acpi-ec.c） | `make -C plan/ws049/tests`、`plan/ws049/tests/check-latitude5330.sh`、`run-asl.py ecdt events glock` | PASS（4 項目、3 passed） |
| kernel の compile（warning 0） | `make -C plan/ws049/tests OUT=build/ws052/host kernel-check` | PASS（`src/drivers/acpi/*.c` を kernel の flag と `-Werror` で） |
| vmunix の link | `make vmunix` | PASS、warning 0、`amd64 vmunix check: PASS` |
| 規約 | `plan/tools/style-check.py`（触った 5 file）、全文の規則の目視（条件の中の呼び出し、式の Boolean、3 項以上の条件、最後の成功の return） | 指摘 0 |
| QEMU の boot test | T1 に依頼（Q1 経由） | 未実施（結果待ち）。LPS0 の walk と EC の GPE の arm が boot の経路に入ったので、login prompt まで行くことを確かめる |
| 実機（5330） | — | 未実施。実機の boot の log で `ACPI: LPS0 \_SB_.PEPD, Intel functions 0x7f, Microsoft functions 0x1ff` が出ることは、次の実機の UAT で見る |

試験の file: `plan/ws052/tests/Makefile`、`host-sleep.c`（WS049 の harness に `--wrap=drv_acpi_reset` で入り、`WS052_SLEEP` の手順を流す）、
`sleep.asl`、`sleep.expected`、`latitude5330.expected`、`run-host-sleep.sh`。

## 制限と残り

- 呼び出し元はまだ無い（LPS0 の attach だけが boot で動く）。device の suspend（p004）と S0i3 の入口・出口（p006）が使う。
- GPE block device（`_PRW` の GPE が package）は未対応（5330 には無い）。
- 2 つ目の GPE block（GPE1）は今の event の code と同じく扱う（5330 は GPE0 だけ、128 個）。
- power resource の `_STA` を最初に読んで状態を合わせることはしない（最初の変更で device を D0 とみなし `_PR0` を取る）。
- LPS0 の function 1（device の制約の一覧）は使っていない。S0i3 に入れない時の診断（design §8）で使う候補。

## Q1 の判定（2026-10-05）

host の試験 PASS（試験の table と 5330 の table）と T1-153 の CI の image の boot PASS（LPS0 の attach が boot の道で走っても起動を壊さない）。**cleared**。5330 の boot の log の LPS0 の行は UAT。
