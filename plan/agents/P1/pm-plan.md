# 電源と性能の WS の全体の依存（q1615、P1、2026-10-27）

ユーザー（2026-10-27）「新規WSはまず計画を広く進めて、全体の依存関係を見ましょう。」に答える計画の一覧。指示ではなく案で、Queue の権限は与えない。
各 WS の本体は ws.md。ここは依存と順と判断の写し（食い違えば ws.md が正）。

## 対象

| WS | 到達目標（要約） | 状態 | 見積もり（LW、±50%） |
| --- | --- | --- | --- |
| [WS299](../../ws299/ws.md) | 5330 の NVMe の連続の読み書きを量に依らず一定に、Debian に対し決めた割合へ（512 MiB の崖を無くす） | planning（新） | 約 32（HMB を外せば 28） |
| [WS300](../../ws300/ws.md) | NVMe と VMD の低電力: S0 idle の時の VMD・root port・NVMe の suspend・resume と状態、動作中の APST・ASPM L1.2・LTR | planning（新） | 約 27 |
| [WS052](../../ws052/ws.md) | S0ix を初めて動かす: 5330 で SLP_S0 の residency を出し、起きて session と network が続く | incomplete（保留を解く案、p014〜p018 を追加） | 約 31（新しい Phase） |
| [WS301](../../ws301/ws.md) | CPU の DVFS と P/E core の相対の性能の枠組み（5330 の HWP・hybrid から、他の arch は後の Phase） | planning（新） | 約 40（5330 だけなら 31） |
| [WS302](../../ws302/ws.md) | デスクトップスレッドバランサ（DTB、`CONFIG_DTB`、sched への hint） | planning（新） | 約 32 |
| 熱と fan（番号は Q1、案） | 熱の policy（passive = WS301 の上限、active = fan）と fan の口（ACPI `_FAN`、Dell の EC・SMM・thermal mode、Pi 5 の PWM、AMD GPU の SMU の fan との関係） | 未作成（ユーザーの補足「ファン制御もあるといいですね。」、WS301 に入れず別にする案） | 約 18〜24（未精査） |

合計 約 160 LW（fan の WS を入れて約 180 LW）。8 LW ≈ 実時間 2〜3 時間の換算で、担当 1 人で約 40〜65 時間。

## 関係する既存の WS・bug

| 記録 | 関係 |
| --- | --- |
| [WS298](../../ws298/ws.md) VMD の driver（completed 2026-10-27） | WS299・WS300 の土台。`vmd_suspend` の EBUSY と 512 MiB の崖はここの残り |
| [WS132](../../ws132/ws.md) `/dev/system` の電源と PnP（completed） | 事象の購読・`KERN_SYSTEM_GET_POWER`。WS052 の UAPI、WS301 の UAPI の推し（A）の置き場所 |
| [WS049](../../ws049/ws.md) ACPI AML（completed） | LPS0 の `_DSM`・`_PSx`・`_CPC`・`_PSS`・`_FAN` を評価する前提 |
| [WS253](../../ws253/ws.md) 起動の時間（completed） | 直接の依存は無い。networkd などを遅れて起こす順は WS052 の SLEEP_PREPARE の相手。WS299 の速さは起動の時間にも効く |
| [WS182](../../ws182/ws.md) 電源ボタンの menu（completed） | WS052 p012 の「電源ボタンは眠らせない」 |
| [WS039](../../ws039/ws.md) AMD RDNA（completed） | p012 の SMU の fan（GPU の board に閉じた物）。熱と fan の WS と口を揃えるかの判断 |
| [WS291](../../ws291/ws.md) GPU の描画の速さ（P2、進行中） | WS302 の物差し（frame の時間）と GPU の統計の場所（`gpu.c`）を共有。file の衝突に注意 |
| [WS245](../../ws245/ws.md)・BUG-334（i915 が idle で DC6 に入らない） | WS052 p018 の候補（S0ix の阻害） |
| BUG-289（xHCI の suspend の doorbell）・BUG-159（電池の時の fps） | WS052 の実機の時に再確認 |
| F-097（Pi 4 の idle の context switch の多さ） | WS301 の arm64 の provider・電力の評価の時に関係 |

## 依存の図（矢印は 前提 → 後）

```
WS298(済) ──> WS299 p001 ──> p002(buffer cache) ──> p005 ──┐
   │                 └──> p003(大きな転送) ──> p004(queue・vector) ──> p006(HMB) ──> p007 ──> p008
   │                                              │
   └──> WS300 p001 ──> p002(VMD suspend) ──────────┼──────────────> WS052 p014(5330 の基準の UAT)
                         │                         v                     ^         │
                         ├──> p004(ASPM・LTR)    p003(NVMe の S0 の状態)  │         v
                         │                         └──> p005(APST)        │     p018(阻害の解消) ──> p008(20 回・規約)
                         └────────────── p006(5330 受け入れ) <── WS052 p014│         ^
WS052 p006(実装済み) ──> p015(無人の wake) ─────────────────────────────────┘         │
                    └──> p016(residency の読み) ──> p017(PMC の診断) ──────────────────┘

WS301 p001 ──> (HAL・UAPI の決定) ──> p002(枠組み) ──> p004(x86 provider) ──> p006(5330) ──> p009
       │                    └──> p003(HAL) ──┘          └──> p005(UAPI)
       │                                     p002 ──> p007(arm64)・p008(riscv64)
       ├──> WS302 p001(検討) ──> p002(sched の口) ──> p004(DTB) ──> p006(評価) ──> p007
       │              └──> p003(統計) ──────────┘   ↑ WS301 p002・p004
       └──> 熱と fan の WS（WS301 p002 の熱の上限の口の後）
```

- WS052 と WS300 の循環を避けた形: VMD の suspend・resume（ws300-p002）は WS052 に依らず先にでき、WS052 p014 がそれに依る。WS300 の残り（p006 の 5330 の受け入れ）は WS052 p014 に依る。
- WS299 と WS300 は `pci-nvme.c`・`pci-vmd.c` を共有する。WS299 p003・p004 → WS300 p003・p005 の順（同じ担当が続けて持つのを推す）。
- 5330 は 1 台（`/tmp/i915-hw.lock`）で、WS052・WS299・WS300・WS301・WS302 の全部が使う。遠隔の電源が無く、S0ix の試みで止まるとユーザーの電源ボタンが要る。USB の stick は 2026-10-27 に挿さっていない。

## 着手の順の案

1. **WS299 p001**（測定と崖の確かめ）→ **p002**（buffer cache の確保を O(1)）: 崖の最有力の仮説は全機種の kernel に効き、小さく効果が大きい。
2. **WS300 p001・p002**（VMD の suspend・resume）: WS052 の 5330 の前提を外す。
3. 並べて（別の担当、5330 を使わない）: **WS052 p015・p016**（無人の wake、residency の読み）と **WS301 p001**（調べと設計、HAL・UAPI の案）。
4. **WS052 p014**（5330 の基準の UAT、ユーザーが居る時に）→ p017 → p018。
5. **WS299 p003〜p008**（NVMe の転送・queue・readahead）→ **WS300 p003〜p007**。
6. HAL・UAPI の決定の後に **WS301 p002〜p006**、並べて **WS302 p001**（検討、ユーザーの review）。
7. **WS302 p002〜p007**、**WS301 p007〜p009**、熱と fan の WS。
8. **WS052 p008**（20 回と規約の全文）は p018 と WS300 の後。

推しの担当の分け方: 担当 A が WS299 → WS300（NVMe・VMD の file を一人で）、担当 B が WS052（S0ix）、担当 C が WS301 → WS302。5330 は Q1 が順に割り当てる。

## ユーザーに聞く事

1. WS299 の受け入れの数（Debian に対する割合など）は p001 の測定の後に決めてよいか。HMB（DRAM-less の BG5）をやるか。
2. WS300 で「動作中の APST・ASPM」まで入れるか。入れると `docs/architecture/power-management.md` の「idle の device の runtime の省電力は対応外」を変える。
3. WS052: residency を返す `KERN_SYSTEM_SLEEP_INFO` の UAPI（2026-10-05 の承認の外と P1 は読む）、MSR の counter を読む HAL の口、S0ix の試み（5330 が起きない時に電源ボタンを頼む）をいつやるか。
4. WS301: UAPI は `/dev/system`（推し A）でよいか。設定を誰に許すか。HAL の差分（topology、x86 の HWP の機構を HAL に置く案 (a)）を案として作ってよいか。他の arch（Pi・VF2）まで WS301 に入れるか、5330 で閉じるか。
5. fan は別の WS（熱と fan）にしてよいか（推し）。5330 は EC が fan を回すため、OS からは回転数の読みと Dell の thermal mode の選択になる見込み。
6. WS302: DTB の既定（on・off）、規則の強さの順（nice・GPU・tty・長い計算）、nice を sched の priority にも効かせるか、compositor から前面の app を kernel に知らせる UAPI を作るか。
7. 「（上記WSでやってほしいです）」の読み: Q1 は S0ix を既存の WS052 に割り当てた。NVMe・VMD の WS300 の中で S0ix までやる意味だったなら、WS052 の p014〜p018 を WS300 に移す（どちらでも依存の形は同じ）。
