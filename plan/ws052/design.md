# WS052 設計: 電源管理（S0i3、modern standby）

Status: 案の第 1 版（2026-10-04、ws052-p001、P1 generation16）。design-reviewer の敵対的レビューは未実施。人間の判断が要る点は §10。

## 1. 目的と範囲

Dell Latitude 5330（Alder Lake-P）で ACPI の S0 low power idle（modern standby、S0ix）の最も深い S0i3 に入り、wake の事象（電源ボタン、蓋、
キーボード、USB、AC）で戻る。操作は `/dev/system`（WS132 の事象と同じ node）から行う。S3（suspend to RAM）と S4（hibernate）は対応しない
（2026-09-24 ユーザー）。S0i1・S0i2 は必要になったら検討する（S0i3 に入れない時の中間の段として、状態の確認にだけ使う）。

範囲外: S3・S4、runtime の device の自動の省電力（D3cold の runtime PM、APST の調整などの日常の省電力）は S0i3 の入口と出口に要る分だけ、
CPU の周波数の制御（HWP）、熱の管理（DPTF）。

## 2. S0i3 の仕組み（Alder Lake-P、Linux の suspend-to-idle と同じ考え方）

S0i3 は OS から見ると「全ての device を低電力にし、全 CPU を最も深い C-state で止めると、PMC（Power Management Controller）が platform を
SLP_S0 にする」状態である。S3 と違い、OS は platform に「眠れ」と命じない（`_PTS`・`\_S3`・SLP_EN を使わない）。

| 段 | 内容 |
| --- | --- |
| 1. 利用者の要求 | Keiland（蓋を閉じた・電源ボタン・idle の方針）が `/dev/system` の ioctl で S0i3 を求める |
| 2. user の停止 | user の process を止める（freeze）。timer の事象・network の受信で process が走らないようにする |
| 3. device の suspend | device ごとの suspend（下の §5 の順）: I/O を止め、状態を保存し、D3hot（必要なら `_PS3`・`_PR3` で D3cold）にし、wake の要る device は `_DSW`・PME を有効に |
| 4. LPS0 の通知 | LPS0 device（`INT33A1`/`PNP0D80`、5330 は `\_SB.PEPD`）の `_DSM`: display off（function 3）→ low power entry（function 5）。Microsoft の UUID の版も（5330 は空の実装） |
| 5. wake の準備 | wake の GPE（`_PRW` の GPE）だけを有効にし、他の GPE を mask。SCI は IRQ 9 のまま（WS049 p007） |
| 6. CPU の idle | 全 CPU を最も深い C-state（MWAIT の C10。`_CST` か ACPI の LPIT の値）で止める。periodic の tick を止める（または次の timer まで延ばす） |
| 7. SLP_S0 | PMC が全 IP の低電力を確かめ、platform を SLP_S0 に（OS は何もしない。residency の counter が増える） |
| 8. wake | wake の GPE の SCI か device の割り込みで CPU が起き、逆の順: tick の再開、LPS0 の exit（function 6）・display on（function 4）、device の resume、user の再開、`/dev/system` の事象（wake の理由） |

## 3. 5330 の事実（DSDT の逆アセンブル、2026-10-04）

- LPS0: `\_SB.PEPD`（`_HID INT33A1`、`_CID PNP0D80`）。`_STA` は `OSYS >= 0x07DF`（`_OSI ("Windows 2015")`）か `OSYS >= 0x07DC && S0ID == 1` で
  0x0F（`PSOP ()` を呼ぶ）。
- `_DSM`（Intel の UUID `c4eb40a0-6cd2-11e2-bcfd-0800200c9a66`）: function 0 は `Buffer {0x7F}`（1〜6）。1 = device の制約の一覧（`DEVY`、
  `S0ID == 0` なら空）、2 = `BCCD`、3 = display off（`SGOV (0x090C000D, 0)`）、4 = display on（EC の `EISC (0x81, 0xB9, 0)`・GPIO）、5 = low power
  entry（EC の `EISC (0x81, 0xB9, 1)`、`GUAM (1)`、xHCI の `PSLI (5)`、`GPRV`、GPIO）、6 = exit。
- `_DSM`（Microsoft の UUID `11e00d56-ce64-47ce-837b-1f898f9aa461`）: function 0 は `_OSI ("Windows 2020")` で `{0xFF, 0x01}`、他は空。
- UUID `57a6512e-…` の function 1 は `LBUF`（用途は p002 で確かめる）。
- `S0ID`（GNVS、BIOS の設定の S0 idle の有効）が 1 であることが前提。FADT の `LOW_POWER_S0_IDLE_CAPABLE`（flags の bit 21）は FACP を
  取り出していないので未確認（UAT か、Linux の 5330 から FACP を取り出す。§9）。
- EC: 蓋・ボタン・AC は EC の `_Qxx` から `Notify`（WS049 p007 の実機の UAT で確かめる）。EC の GPE は `_PRW` で wake の GPE（p002 で一覧を作る）。

## 4. 今の zedBSD の状態

- ACPI: AML の interpreter、SCI・GPE・固定の event・EC（WS049、p007 は QEMU で PASS、実機待ち）。`_PRW` の GPE を wake として runtime から外す
  処理はある（`acpi-event.c` の `wake_visitor`）。suspend の時に wake の GPE だけを有効にする口は無い。
- HAL: CPU の idle は `hal_cpu_idle()`（`sti; hlt`）だけ。MWAIT・C-state の選択は無い。timer は periodic の tick（`HAL_TIMER_FREQUENCY`）で、
  止める・延ばす口は無い（`include/hal/hal.h` の RTC の節）。
- device の driver に suspend・resume の口は無い（PCI の層にも無い）。
- `/dev/system`: halt・reboot・poweroff の ioctl はある（`include/uapi/system.h`）。事象の配信は WS132 が設計中（subscriber が種類を登録、
  受け取り手は Keiland）。

## 5. device の suspend・resume

- **口**: driver の登録に `suspend(device)`・`resume(device)` を足す（PCI の driver の ops と、PCI でない driver（ACPI・platform）の登録の表）。
  順は device の木の子から親へ suspend、親から子へ resume（PCI の bridge の先を先に）。失敗した driver があれば、それまでに suspend した device を
  resume して S0i3 を中止し、理由を `/dev/system` の事象で返す。
- **5330 の device と要るもの**（p002 で一つずつ確かめる）:

| device | suspend | 備考 |
| --- | --- | --- |
| i915（display・GPU） | scanout を止め、display の power well を DC6/DC9 に、GT を RC6 に、D3hot | Guardrail の scanout の規則: resume では GOP の出力先でなく、suspend の前の出力先（Keiland の指示の分を含む）に戻す。Keiland には resume の事象で知らせる |
| NVMe | 書き込みを flush、APST か power state の最深、D3hot（Intel の platform は D3cold を要する機種がある） | S0i3 に入れない最も多い原因（Linux の経験） |
| xHCI（USB） | controller の停止、port の状態の保存、PME の有効（USB の wake） | `_DSM`・`PSLI` は LPS0 の function 5 が呼ぶ |
| HDA（音） | codec を D3、controller を D3hot | |
| Wi-Fi（RTL8822B など） | 接続を切るか WoWLAN（範囲外）、D3hot | `.inc` の license の分離は保つ |
| Thunderbolt・TCSS | IOM と PMC の handshake（TC cold） | WS051 の TC cold の扱いと揃える |
| SATA・AHCI（無ければ不要） | LPM | 5330 は NVMe のみの見込み（確かめる） |
| EC | query の停止はしない（wake の源）。`_REG` の状態は保つ | |
| UCSI（WS050） | resume の後に通知の再有効化か PPM_RESET からの列挙（WS050 design §6 の C1） | |

- **ACPI の device の電源**: `_PS0`・`_PS3`、`_PR0`・`_PR3`（power resource の `_ON`・`_OFF`）、wake の device の `_DSW`（または `_PSW`）を
  WS049 の評価で呼ぶ（共通の helper を WS049 か WS052 に置く）。

## 6. CPU の idle と timer（HAL の差分が要る、§10-1）

- **最も深い C-state**: ADL-P の C10 は MWAIT の hint（`_CST` の FFixedHW の値、または Intel の公開の表）で入る。HAL に「この CPU を MWAIT の hint で
  idle にする」口が要る（今の `hal_cpu_idle()` は `hlt` だけで、`hlt` は C1 にしかならず S0i3 に届かない）。
- **tick の停止**: periodic の tick が止まらないと CPU は C10 から毎 tick 起き、SLP_S0 に入れない。S0i3 の間だけ tick を止め（LAPIC timer を止める
  か deadline を遠くに）、wake の後に再開する HAL の口が要る。kernel の時刻は wake の後に TSC か RTC で進める。
- **AP**: 全 AP も同じ idle に入る（AP を止める IPI と、wake で再開する経路）。
- **wake の割り込み**: SCI（IRQ 9、level）と、wake の要る device の MSI。S0i3 の間に他の割り込みを mask する口。
- 上の 4 つは `include/hal/hal.h` の API の追加になるので、**差分ごとにユーザーの事前承認が要る**（AGENTS.md）。p002 で差分の案を
  `plan/ws052/proposed/` に置き、承認の前は適用しない。

## 7. `/dev/system` の口（WS132 と同じ node）

| ioctl | 内容 |
| --- | --- |
| `KERN_SYSTEM_SLEEP`（案） | S0i3 に入る。戻ったら返る（返り値は wake の理由と、入れなかった時の理由: device の suspend の失敗、S0ix に未対応の platform、SLP_S0 に届かなかった） |
| `KERN_SYSTEM_SLEEP_INFO`（案） | 対応の有無（FADT の S0 idle、LPS0 の有無、`S0ID`）、最後の S0i3 の時間と SLP_S0 の residency、入れなかった原因の device |

- 事象（WS132 の配信）: 入る前（`power.sleep.begin`）、戻った後（`power.sleep.end reason=power-button|lid|keyboard|usb|ac|timer`）、入れなかった
  （`power.sleep.failed device=…`）。Keiland は蓋の事象（WS049 の EC の Notify → WS132 の電源の事象）を受けて自分の方針で `KERN_SYSTEM_SLEEP` を呼ぶ。
- 権限: S0i3 に入れるのは graphical session の利用者（Keiland）か root（WS132 の権限の設計に合わせる）。
- UAPI の追加は `include/uapi/system.h`（HAL ではない）。名前と struct は WS132 の事象の形式と一緒に p002 で決める。

## 8. 確かめ方

- **SLP_S0 の residency**: PMC の MMIO（PWRM の base、ADL では PCI の 0:1f.2 の BAR か ACPI の LPIT が示す counter）の SLP_S0 の residency の
  counter が S0i3 の前後で増えることを確かめる（Linux の `intel_pmc_core` の `slp_s0_residency_usec` と同じ値を読む。code は写さず register の
  意味だけ）。LPIT（Low Power Idle Table）を firmware が持てば、その counter の address を使う。
- **入れない時の調べ方**: PMC の LTR の無視の設定、IP の電源の状態（PMC の register）、device ごとの D-state を `/dev/system` の情報と log に出す
  （Linux の `substate_status`・`ltr_show` に当たる診断）。
- QEMU: S0ix は無い。QEMU の試験は boot test と、`KERN_SYSTEM_SLEEP` が「未対応の platform」で安全に失敗すること（T1）。
- 実機（UAT）: S0i3 に入り residency が増える、電源ボタン・蓋で戻る、login した session と network が続く、wake の理由が正しい、20 回の繰り返しで
  失敗が無い（耐久はユーザーに確かめて夜間）。

## 9. Phase の案

| Phase | 内容 | 依存 | 受け入れ |
| --- | --- | --- | --- |
| p001 | 調査と設計（この文書） | WS049 の namespace | この文書、レビュー、§10 の判断 |
| p002 | 詳細の調査と HAL の差分の案: 5330 の FACP・LPIT（取り出しの許可）、`_PRW` の wake の GPE の一覧、`_CST`、PMC の register、HAL の差分（MWAIT の idle、tick の停止、AP、wake の割り込み）を `proposed/` に | p001、FACP・LPIT の取り出し | 差分の案と承認の依頼、wake の一覧 |
| p003 | ACPI の側: LPS0 の `_DSM`（display off/on、entry/exit）、wake の GPE だけを有効にする口、`_PS0`/`_PS3`・`_PRx`・`_DSW` の helper、host の試験（5330 の DSDT で `_DSM` の呼び出しの順） | p001、ws049-p007（実機）、ws049-p017 | host の試験、build |
| p004 | device の suspend・resume の口と各 driver（i915・NVMe・xHCI・HDA・Wi-Fi） | p003 | build、QEMU の boot test、実機で device ごとの suspend・resume（S0i3 の前の段として、各 device の D3 と戻り） |
| p005 | CPU の idle と tick（承認された HAL の差分）と S0i3 の入口・出口、`/dev/system` の ioctl と事象（WS132） | p002 の承認、p004、WS132 の事象 | 実機で SLP_S0 の residency が増え、wake で戻る |
| p006 | 実機の確認（繰り返し、session と network の継続）と規約の全文の確認 | p005 | 受け入れの全項目、規約、build、boot test |

## 10. 人間の判断が要る点

1. **HAL の API の追加**（§6: MWAIT の idle、tick の停止と再開、AP の停止、wake の割り込みの mask）。p002 で差分の案を作り、差分ごとに承認を求める。
2. **S0i3 に入る契機**: Keiland の方針（蓋を閉じた時、電源ボタン、idle の時間）で `KERN_SYSTEM_SLEEP` を呼ぶ案。kernel は自分からは入らない。
3. **入れない device がある時**: S0i3 を中止して理由を返す案（Linux の s2idle は入れなくても浅い idle で待つ）。中止せず浅い idle（S0i1/i2 相当）で
   待つ方を選ぶか。
4. **5330 の FACP・LPIT の取り出し**: DSDT・SSDT と同じく 5330 の Linux から読み取り専用で `/sys/firmware/acpi/tables/FACP`・`LPIT` を取り出す許可。
5. **beta1 の範囲**（WS132 は beta1 に入る。WS052 の device の範囲（i915・NVMe・xHCI は必須、HDA・Wi-Fi は後でよいか）。
