<!-- awesome-plan project=zedbsd record=ws300-design -->

# WS300 設計: NVMe と VMD の低電力（S0 idle の VMD の suspend・NVMe の状態、APST・ASPM・LTR）

- 版: **第 1 版の下書き**（2026-10-29 P5、q1666-i04、書きかけ。利用の上限の前に途中で commit した。§4.3 以降は骨組みで、design-reviewer は未実施）。
- 基点: worktree `p5-design-d`（main 68ee65381）。code は書いていない。**HAL は変えない**（§3、AGENTS.md §7）。
- 入力: [survey-5330.md](survey-5330.md)（5330 の Debian の読みと DSDT）、[survey-fable.md](survey-fable.md)（公開の資料、GPL の source は読まない条件）、zedBSD の source（§1）。web の取得はしていない。GPL の source は読んでいない。

## 0. ユーザーの決定（原文）と解釈

| 日付 | 原文 | 解釈（P5） |
| --- | --- | --- |
| 2026-10-27 | 「NVMeやVMDがS0i3などに対応するようにWSを立ててやってほしいです（まず計画から。あとで順番にやってほしいです）。」 | この WS の発端。VMD・NVMe が S0 idle（S0ix）を妨げないようにする |
| 2026-10-27 | 「そもそもS0ixが動作した実績がないので、これに対応するWSを作成してやってほしいです。（上記WSでやってほしいです）」 | S0ix の全体は WS052（Q1 の割当）。この WS は storage の側 |
| 2026-10-27 09:39（クリック、ws.md） | 動作中の省電力（NVMe の APST、PCIe の ASPM L1.2・LTR）も入れる（docs/architecture/power-management.md の「runtime の省電力は対応外」を直す） | 範囲に APST・ASPM・LTR を含む。文書の範囲も変える（ユーザーの review） |

## 1. 今の振る舞い（code の事実）

- VMD: `vmd_suspend` は常に EBUSY（`src/drivers/pci/pci-vmd.c:422〜435`、comment「The root ports behind the VMD have no driver to save and restore their bus numbers and windows across D3」）。`.resume` は無い（`vmd_driver` 225〜232）。detach も EBUSY（409〜419）。
  attach は BAR の decode と bus master を入れ（306〜320）、窓・bus を読み、vector を取り、`vmd_plan` で隠れた root port の bus の番号と窓を置き（`pci-vmd-plan.c`）、`drv_pci_bus_create_root` で root bus を作り scan する（378〜402）。config の読み書きは `config_lock`、子の MSI は `message_lock`（150〜171）。
- PCI の共通の S0 idle（`src/drivers/pci/pci-power.c`、file の先頭 8〜31）: **driver の付いた function だけ**を集め（`count_visitor`・`collect_visitor` 726〜769）、深い順に suspend（`suspend_entry` 821〜900: driver の suspend → `drv_pci_device_save_state` → wake → D3hot → platform）、resume は逆順（`resume_entry` 902〜977）。1 つ失敗すれば全部を戻して中止（650〜672）。
  save は header 64 byte・MSI・PCIe の 4 つの control・LTR・L1SS の 2 つの control・MSI-X の表（159〜293）。`drv_pci_device_keep_d0`（525〜547）で D0 のまま眠らせられる。
- 結果: 5330 を NVMe（VMD の下、深さ 2）から起動すると、NVMe → VMD の順に suspend し VMD が EBUSY → NVMe を resume → S0 idle は中止（ws.md の「今の事実」、`plan/ws298/design.md:170〜172`）。隠れた root port（深さ 1）は driver が無いので集めに入らない。
- NVMe: `nvme_suspend`（今の main では `pci-nvme.c:2265〜`、`nvme_resume` 2303〜。ws.md の 2073〜2102 は古い行）は CC.SHN → disable → D3hot（ws.md の 2026-10-27 の読み、本文は読み直していない §10）。APST（0x0C）・Feature 02h の定義は無い。**HMB（0x0D）は今は有る**（`include/drivers/pci/pci-nvme-protocol.h:55`、Set Features の組み立て 462〜463。ws.md の「HMB は無い」は古い）。suspend・resume での HMB の扱いは未読（§10）。
- ASPM・L1SS・LTR を設定する code は無い（save と restore だけ）。

## 2. 調べた事実（要約、詳細は survey の 2 つ）

- BG5: PS0〜PS2 操作、PS3 0.05 W（ENLAT 800 µs・EXLAT 1000 µs）、PS4 0.003 W（3000 µs・44900 µs）、APSTA 1、RTD3R 100 ms・RTD3E 500 ms、HMB を求める（DRAM 無し）。
- 隠れた root port 10000:e0:06.0 と NVMe: ASPM L1 と L1SS（L1.2）の能力が有り、Debian でも全部無効（firmware が隠れた port を設定しない）。LTR は両端で有効。
- PMC: Debian でも動作中は Package C2 まで。S0ix の residency は 0（他の妨げもありうる、未確認）。
- DSDT: `\_SB.PC00.VMD0` に `StorageD3Enable`（`VDSD == 1` の時）。`VMD0.PEG0` に D3cold の method `VPOF`・`VPON`（呼び手は DSDT に無い、OS の VMD の driver が呼ぶ前提と推定）。

## 3. 設計の方針（下書き）

- **HAL は変えない**: 必要な物は全部 PCI の driver（`pci-vmd.c`・`pci-power.c`・`pci-nvme.c`）と ACPI の driver の中で足りる。config 空間の読み書き・D-state・save と restore の口は既にある（§1）。
- **VMD の suspend（p002）の推し**: VMD の driver が、自分が `vmd_plan` で設定した隠れた root port を自分の suspend の中で扱う。
  suspend: 各 root port の `drv_pci_device` に `drv_pci_device_save_state`（type 1 の header 64 byte に bus の番号・窓・bridge control が入る、PCIe の control・LTR・L1SS も）→ D3hot。VMCONFIG（0x44、64 byte の外）を保存。core が VMD 自身の header・MSI-X の表を保存し D3hot と ACPI の `_PS3`（VMD0）。
  resume: core が VMD を D0・復元 → VMD の resume が VMCONFIG を書き戻し、各 root port を D0・`drv_pci_device_restore_state`（10 ms の待ち）→ core が NVMe を resume（逆順なので VMD の後）。
  選ばなかった案: 汎用の PCIe port の driver を bridge に付ける（全部の機械の bridge に driver が付き、影響が広い。VMD の下だけなら推しと同じ）。
- **S0 idle の NVMe の状態（p003）**: 今の SHN＋D3hot を保つ（推し、まず VMD の往復を通す）。Microsoft の形（最深の非操作の PS＋link L1.2、shutdown なし）と D3cold（`VPOF`）は p006 の測りの後の選択肢（ユーザーの問い Q300-1 の候補）。
- **ASPM・L1SS・LTR（p004）**: 隠れた root port と NVMe の両端に、両端の能力の共通の部分だけを設定（ASPM L1、L1.2、LTR は既に有効）。手順の出典が課題（PCIe Base の仕様は手元に無い、§8・Q300-2 の候補）。
- **APST（p005）**: Identify の PSD から表を作り Set Features 0x0C。idle の時間は state の ENLAT+EXLAT の 50 倍と 100 ms の大きい方（5330 の BG5 で PS3 100 ms・PS4 約 2.4 s の見込み）、ENLAT+EXLAT が許容（既定 100 ms）を超える state は使わない。reset・resume のたびに設定し直す（FreeBSD の考え方、BSD）。

## 4〜9. （未着手。次の版で: 状態遷移、lock、失敗と回復、UAPI（状態を見る口、WS303 の `/dev/thermal` に揃えるか）、試験、Phase と受け入れ、出典、問い）

## 10. 未確認

1. VMD の D3hot で隠れた root port の config が失われるか（p002 で zedBSD の上で D3hot の前後に読む）。
2. `pci-nvme.c` の suspend・resume の本文と、HMB を shutdown の前に返させ resume で渡し直しているか（NVMe Base 2.0e §8.9）。
3. zedBSD の起動の時の隠れた root port の L1SS・LTR の値（Debian の値は Linux の後かもしれない）。
4. GNVS の `VDSD`・`STD3`・`VMS0` の値。
