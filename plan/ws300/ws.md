<!-- awesome-plan project=zedbsd record=ws300 -->

# WS300 NVMe と VMD の低電力（S0i3 の時の D3 と、動作中の APST・ASPM）

- Master: [plan/master.md](../master.md)（2026-10-27 作成、q1615 P1 の計画）。Primary Milestone: MG003。Related: MG004、[WS052](../ws052/ws.md)（S0ix）、[WS298](../ws298/ws.md)（VMD）、[WS299](../ws299/ws.md)（NVMe の速さ）。
- Status: planning（2026-10-27 新設。計画だけ）
- 出典: 2026-10-27 ユーザー「NVMeやVMDがS0i3などに対応するようにWSを立ててやってほしいです（まず計画から。あとで順番にやってほしいです）。」
  「そもそもS0ixが動作した実績がないので、これに対応するWSを作成してやってほしいです。（上記WSでやってほしいです）」（S0ix 自体は既存の WS052 で、Q1 の割り当て）
- 全体の依存と着手の順: [plan/agents/P1/pm-plan.md](../agents/P1/pm-plan.md)。

- 2026-10-27 09:39 ユーザー（クリック）: 動作中の省電力（NVMe の APST、PCIe の ASPM L1.2・LTR）も入れる（docs/architecture/power-management.md の「runtime の省電力は対応外」を直す）。

## 単一の到達目標

5330 で NVMe から起動した zedBSD が、(1) S0 idle（WS052）の時に VMD とその下の root port・NVMe を suspend して resume でき（今は `vmd_suspend` が常に EBUSY）、
S0i3 の妨げにならない状態（NVMe は D3、link は L1.2 か L2 相当）に置き、(2) 動作中の idle では NVMe が APST で自ら低い power state に移り、link が ASPM L1・L1 substates に入る。

## 今の事実（2026-10-27 の調べ）

- VMD: `vmd_suspend` は常に EBUSY（`src/drivers/pci/pci-vmd.c:422-435`、理由の comment は「VMD の後ろの root port に driver が無く、bus の番号と窓を D3 の間に保てない」）。
  `.resume` は無い（:225-233）。VMCONFIG（0x44、bypass の bit）は 64 byte の header の保存の外。WS298 の design は BIOS_PM_QUIRK（LTR・ASPM）と secondary bus reset を意図して外した（`plan/ws298/design.md:238` U5、U3）。
  結果: NVMe（深さ 2）が先に suspend され、VMD が拒み、NVMe が resume される → **5330 を NVMe から起動すると S0 idle は必ず中止**（`plan/ws298/design.md:170-172`）。
- NVMe: `nvme_suspend`（`pci-nvme.c:2073-2102`）は CC.SHN の正規の shutdown → disable → D3hot（PCI の共通）。resume は全部の初期化（:2111-2134、2288-2333）。
  APST（Set Features 0x0C）・Power Management（0x02）・NPSS/PSD/APSTA の読み・HMB は無い（`include/drivers/pci/pci-nvme-protocol.h:43-45` は 0x09/0x07 だけ）。
- PCI の共通（`src/drivers/pci/pci-power.c`）: D0 と D3hot だけ（`include/drivers/pci/pci.h:718-719`）。LTR・L1SS・LnkCtl は保存と復元だけで、ASPM・L1SS・LTR を設定する code は無い。
  D3cold は ACPI の `_PS3`・電源の資源の側だけ（`src/drivers/acpi/acpi-sleep.c:362-409`）で、VMD の下の device には ACPI の node が無い（platform の hook は ENOENT）。
  `drv_pci_device_set_wake` を呼ぶ所は無い。runtime PM の汎用の枠組みは無い（i915 だけ自前の `runtime-pm.c`）。
- docs: `docs/architecture/power-management.md:89` は NVMe を「deepest power state」と書くが、実際は SHN＋D3hot。「idle の device の runtime の省電力」は対応外と書いてある（:22-24）。範囲を変えるなら文書も（ユーザーの review）。

## 範囲

- 含む: VMD の suspend・resume（root port の bus の番号・窓・bridge の control、VMCONFIG、MSI-X の付け替えの復元、子の順序）、NVMe の S0 idle の時の状態（D3hot か
  NVMe の最低の power state、`StorageD3Enable` に当たる platform の要求の扱い）、ASPM L1・L1.1/L1.2・LTR の設定（VMD の下の link、BIOS の値の扱い）、
  NVMe の APST（動作中）、NVMe の電源の状態を見る口（今の PS、APST の表、link の状態）。
- 含まない: S0ix の全体の流れと residency の読み（WS052）、NVMe の速さ（WS299）、他の PCIe device の ASPM の一般化（必要なら別の WS）、D3cold の電源を切る制御（5330 の VMD の下に ACPI の口が無い。p001 で要否を確かめ、要れば別に）。

## 制約

- 値と手順: NVMe 仕様（APST・Power State Descriptor）、PCIe 仕様（ASPM・L1 PM Substates・LTR）、Intel の VMD・ADL の資料、Microsoft の modern standby の storage の要件（公開文書）を優先。
  Linux（GPL）の vmd.c・nvme の値と手順は参照だけ、code は写さず block ごとに Value(s)/Procedure referenced。
- HAL の API を変えるなら差分ごとの事前承認。UAPI（状態を見る口を `/dev/system` か sysctl に足す）は別の明示の決定。
- 試験: host 試験（VMD の保存・復元の計画、APST の表の組み立て）、5330 の実機（`sleepctl devices` の往復、`sleepctl s0idle`、APST の遷移の数・時間、
  `/tmp/i915-hw.lock`、遠隔の電源は無いので止まったら Q1 → ユーザー）。QEMU の nvme で APST の Set Features の往復（QEMU が対応する範囲）。
- WS299 と `pci-nvme.c`・`pci-vmd.c` を共有する。WS299 の p003・p004（転送と queue・vector）の後に WS300 の p003 以降を当てる（順は pm-plan）。

## 受け入れ（案）

1. 5330（NVMe から起動）で `sleepctl devices` の往復が 20 回続けて成功し、root の UFS の読み書きが続く（VMD・root port・NVMe の suspend・resume）。
2. WS052 の `sleepctl s0idle` が VMD で中止されない（S0ix の residency の受け入れ自体は WS052）。S0 idle の間の NVMe の状態（D3 か最低の PS）と link の状態を記録で示す。
3. 動作中に idle の NVMe が APST で非操作の power state に入る事を数で示し（遷移の数か滞在、または PMC の LTR・link の状態）、読み書きの速さが WS299 の値から落ちない（落ちるなら閾値の調整）。
4. 規約の全文、build warning 0、5330 の素の起動の回帰（`5330-run.sh` PASS）。

## Phase の案と見積もり（LW、±50%）

| Phase | 内容 | 依存 | LW |
| --- | --- | --- | --- |
| p001 | 調査と設計: 5330 の VMD の下の root port・NVMe の PM capability・L1SS・LTR・BIOS の初期値（Debian で lspci -vvv を読む）、BG5 の NPSS・PSD・APSTA、Windows/Linux の modern standby の storage の要件、S0 idle の時の NVMe の状態の選び方、VMD の保存と復元の範囲、design-reviewer | WS298（済） | 5 |
| p002 | VMD の suspend・resume: root port の config の保存と復元（bus の番号・窓・bridge control・PCIe の control）、VMCONFIG、MSI-X の付け替え、子の順序。`sleepctl devices` の往復 | p001 | 6 |
| p003 | NVMe の S0 idle の状態: SHN＋D3hot を保つか、最低の PS＋D3 にするか（p001 の決定）、resume の時間の短縮 | p002、WS299 p003・p004 | 3 |
| p004 | ASPM L1・L1 substates・LTR の設定（VMD の下の link、BIOS の値を読む・必要なら設定する） | p002 | 5 |
| p005 | APST（Set Features 0x0C、表の組み立て、閾値、無効にする option）と状態を見る口（UAPI は決定の後） | p003 | 4 |
| p006 | 5330 の受け入れ（WS052 の s0idle と一緒に 1 本の script、APST の数、速さの回帰） | p004、p005、WS052 p014 | 2 |
| p007 | 規約の全文の見直し | p006 | 2 |

合計 約 27 LW。

## 判断が要る点

- 範囲: 「動作中の APST・ASPM」を入れると docs/architecture/power-management.md の「runtime の省電力は対応外」を変える（ユーザーの review）。
- S0 idle の時の NVMe を SHN＋D3hot（今）のままにするか、Microsoft の要件に合わせた形にするか（p001 の調べの後）。
- 状態を見る口（UAPI）の置き場所（`/dev/system` か sysctl）。WS301 の決定と揃える。
