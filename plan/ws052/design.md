# WS052 設計: 電源管理（S0i3、modern standby）

Status: 案の第 1 版（2026-10-04、ws052-p001、P1 generation16）。§10 は 2026-10-05 にユーザーが決定（末尾）、§9 の Phase と §6 の HAL の差分の案は 2026-10-05 q727 に改訂、§6 の差分は 2026-10-05 に専門家のレビューを受けて第 2 版（[proposed/README-v2.md](proposed/README-v2.md)、P4）に。design-reviewer の敵対的レビューは未実施。

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
- `S0ID`（GNVS、BIOS の設定の S0 idle の有効）が 1 であることが前提。FADT の `LOW_POWER_S0_IDLE_CAPABLE`（flags の bit 21）は **1**（2026-10-05 に
  FACP を取り出して確かめた）。LPIT: S0ix の entry は MWAIT の hint 0x60（C10）、SLP_S0 の residency の counter は PMC の MMIO 0xFE00193C（8197 Hz）、
  package C10 の residency は MSR 0x632。wake の GPE・`_CST`・電源ボタン（`HIDD`）の読みは [phase002](phase002/phase.md)。
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
- **差分の案（第 2 版、2026-10-05 P4、[proposed/README-v2.md](proposed/README-v2.md)。第 1 版 H1〜H4 は [proposed/README.md](proposed/README.md) に履歴として残す）**:
  専門家のレビュー（[proposed/expert-review-2026-10-05.md](proposed/expert-review-2026-10-05.md)）に沿って「per-CPU の suspend-idle の mechanism」と
  「system-wide の IRQ の policy」の 2 層に整理した。
  - H1v2 `hal_cpu_idle_suspend_supported()`・`hal_cpu_idle_suspend()`: 今の CPU を system の suspend-to-idle に適した最も深い idle に入れ、1 つの割り込みで戻る。
    state は HAL が選ぶ（amd64 は firmware の LPIT の FFixedHW の entry = MWAIT 0x60 と CPUID leaf 5 の確認。kernel は state の名前を渡さない）。HAL が内部で
    今の CPU の tick を止めて戻し、CPU-local の private な源（LAPIC の LVT）を静かにして戻す（第 1 版の H2 `hal_timer_stop/resume` を吸収）。probe で kernel は
    device に触る前に未対応を知る。
  - H3v2 `hal_cpu_notify()`・`hal_cpu_notify_mask()` の契約の明記（API の追加なし）: HAL_OK を返した notify は online の CPU に必ず届き、`hal_cpu_idle()`・
    `hal_cpu_idle_suspend()` の待ちを終え、`hal_irq_suspend()` の間も届く。
  - H4v2 `hal_irq_set_wake(irq, enable)`・`hal_irq_suspend()`・`hal_irq_resume()`: wake の源を arm し、system-wide の routing（I/O APIC の pin・MSI、登録の無い pin）
    を wake 以外全部 mask して戻す。**suspend 中に来た wake の IRQ は handler を runtime どおり呼ぶ（latch しない）**: SCI の handler（p003）が記録と mask の層で、
    spurious な wake の判定は kernel の policy（README-v2 §4-4）。
  - H5 `hal_rtc_read_counter()` の契約の強化（API の追加なし）: counter は HAL が入る全ての idle state をまたいで進む。約束できない state には HAL が入らない。
  - 他の architecture は probe が `HAL_ERR_UNSUPPORTED` を返し、kernel は device に触る前に「未対応の platform」で中止して理由を返す（§10-3）。
  - kernel の p006 の流れ（SMP の調停、各 CPU の suspend idle、wake の判定と spurious の再突入、時刻の進め方）は README-v2 §7。

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

## 9. Phase の案（2026-10-05 §10 の決定に合わせて改訂）

§10 の決定: 入れない device があれば**中止して理由を返す**。device は **i915・NVMe・xHCI が必須**、**HDA・Wi-Fi は後**（それまでは止めれば入れる）。
契機は**蓋を閉じた時・電源ボタンの短押し・一定時間の無操作**（どれも Keiland が判断して `KERN_SYSTEM_SLEEP` を呼ぶ。kernel は自分から入らない）。

| Phase | 内容 | 依存 | 受け入れ |
| --- | --- | --- | --- |
| p001 | 調査と設計（この文書） | WS049 の namespace | この文書、§10 の判断（済み） |
| p002 | 詳細の調査と HAL の差分の案: HAL の差分 H1〜H4 を `proposed/` に（**済み、承認待ち**）。残り: 5330 の FACP・LPIT（取り出しの許可、§10-4）、`_PRW` の wake の GPE の一覧、`_CST` の C10 の hint、PMC の SLP_S0 の residency の register | p001、FACP・LPIT の取り出しの許可 | 差分の案と承認の依頼、wake の一覧、`_CST` の hint |
| p003 | ACPI の側: LPS0 の `_DSM`（display off/on、entry/exit）、wake の GPE だけを有効にする口、`_PS0`/`_PS3`・`_PRx`・`_DSW` の helper、host の試験（5330 の DSDT で `_DSM` の呼び出しの順） | p001、ws049-p007（実機）、ws049-p017 | host の試験、build |
| p004 | device の suspend・resume の口（PCI と platform の driver の ops、子から親への suspend・親から子への resume、**失敗したら既に suspend した device を resume して中止し、原因の device を理由として返す**）と必須の NVMe（flush、shutdown、D3hot）・xHCI（Save/Restore State、port の U3、Restore できなければ attach し直す）、`/dev/system` の `KERN_SYSTEM_SLEEP` の「devices だけ」の mode（root だけ、device の suspend→resume の往復、試験の口）。**i915 は p009 に移した**（2026-10-05 Q1: QEMU で試せず規模が大きい） | p003 | build、host の試験、QEMU で NVMe・xHCI の往復と suspend の無い driver での中止と理由（T1） |
| p005 | **後の device の「止めて入る」経路**: suspend・resume を持たない device（HDA・Wi-Fi・他の任意の device）は、sleep の前に止め（Wi-Fi は networkd が接続を切り interface を down・radio off、HDA は audiod が stream を閉じ controller を reset）、wake の後に初期化し直す。止められない device があれば中止して理由を返す。HDA・Wi-Fi の本当の suspend・resume は後の Phase（WS052 の範囲で別に立てる） | p004 | build、実機で Wi-Fi・HDA があっても中止せずに入る（または止められない時に理由が返る） |
| p006 | CPU の idle と tick と割り込み（**承認された H1v2・H3v2・H4v2・H5**: amd64 の実装と他の architecture の stub）、S0i3 の入口・出口（[README-v2 §7](proposed/README-v2.md) の流れ: 対応の確認（probe・FADT・LPS0）→ user の停止 → device → LPS0 → wake の GPE → `hal_irq_suspend` → 全 CPU が idle loop で `hal_cpu_idle_suspend` → 起きた CPU が coordinator を起こす → wake の理由の判定（spurious なら再突入）→ `hal_irq_resume` → 逆順）、CPU 0 の時刻の補正（counter の差を `kernel_ticks` に）、`/dev/system` の ioctl（`KERN_SYSTEM_SLEEP` の S0 idle の mode・`KERN_SYSTEM_SLEEP_INFO`）と事象（WS132: `power.sleep.begin`・`end reason=…`・`failed device=…`）。user の process の停止（freeze）の機構は kernel に無いので p006 で作るか別に立てる（Q1） | p002 の承認、p004、p005、p009、WS132 の事象 | 実機で SLP_S0 の residency が増え、wake で戻る、sleep の前後の counter と RTC の秒が合う（TSC が S0i3 で進む確認）。QEMU では probe が未対応で device に触らずに安全に失敗（T1） |
| p007 | Keiland の契機: 蓋を閉じた時（WS132 p008 の蓋の分の「画面を消して lock」を sleep に置き換える）、電源ボタンの短押し（dialog 無し、ws132-p001 D1）、一定時間の無操作（Settings の Power で時間を決める、WS089 の頁）。中止の理由を利用者に示す（通知、WS156） | p006、WS132 p008、WS089 | 実機で 3 つの契機で入り、電源ボタン・蓋で戻る |
| p008 | 実機の確認（繰り返し 20 回、session と network の継続）と規約の全文の確認 | p007 | 受け入れの全項目、規約、build、boot test |
| p009 | i915 の suspend・resume（設計: [design-p009-i915.md](design-p009-i915.md)）: display の suspend（窓を出る）、DC9 と PCH の SBCLK の workaround、GT の idle と RC6、GGTT の復元、resume での display の core の再初期化（CDCLK・DBUF・DMC）と保った出力の設定の再適用（Keiland の指示の分を含む）。設計から（2026-10-05 Q1 が p004 から分けた: QEMU で試せず規模が大きい） | p004 | 設計、build、5330 の UAT（suspend→resume で画面が同じ出力先に戻る） |

## 10. 人間の判断が要る点

1. **HAL の API の追加**（§6: MWAIT の idle、tick の停止と再開、AP の停止、wake の割り込みの mask）。p002 で差分の案を作り、差分ごとに承認を求める。→ 第 2 版 H1v2・H3v2・H4v2・H5（[proposed/README-v2.md](proposed/README-v2.md) §12 に残る判断: `hal_irq_resume` の返り値、HAL が LPIT を読む責務、freeze の機構の置き場）。
2. **S0i3 に入る契機**: Keiland の方針（蓋を閉じた時、電源ボタン、idle の時間）で `KERN_SYSTEM_SLEEP` を呼ぶ案。kernel は自分からは入らない。
3. **入れない device がある時**: S0i3 を中止して理由を返す案（Linux の s2idle は入れなくても浅い idle で待つ）。中止せず浅い idle（S0i1/i2 相当）で
   待つ方を選ぶか。
4. **5330 の FACP・LPIT の取り出し**: DSDT・SSDT と同じく 5330 の Linux から読み取り専用で `/sys/firmware/acpi/tables/FACP`・`LPIT` を取り出す許可。
5. **beta1 の範囲**（WS132 は beta1 に入る。WS052 の device の範囲（i915・NVMe・xHCI は必須、HDA・Wi-Fi は後でよいか）。

## §10 の決定（2026-10-05 ユーザー、クリックの回答、複数選択）

S0i3（modern standby）に入る契機: **蓋を閉じた時**（ACPI の LID の event）、**電源ボタンを短く押した時**（電源ボタンの event を Keiland が /dev/system で受けて判断）、**一定時間操作が無い時**（idle の timeout、Settings の Power で時間を決める）。menu だけに限る案は採らない。

## §10 の決定（続き、2026-10-05 未明ユーザー、Q1 が 1 問ずつ聞いた）

- §10-3 入れない device がある時: **中止して理由を返す**（浅い idle で待つ案は採らない）。Keiland が利用者に理由を示す。
- §10-5 device の範囲: i915・NVMe・xHCI は必須、**HDA・Wi-Fi は後**。それまでは、その device を止めれば S0i3 に入れる形（Wi-Fi を切って入る等）で中止を避ける。
- 電源ボタンの短押しは dialog 無しで S0i3（ws132-p001 の D1 の訂正）。蓋は S0i3 ができるまで画面を消して lock、15 分以内の解除は password 無しで自動の unlock（ws132-p001 の D2）。
