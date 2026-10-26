<!-- awesome-plan project=zedbsd record=ws294 -->

# WS294 i915 の GuC による描画の submission（Gen12 以降、kernel の option で有効）

- Master: [plan/master.md](../master.md)（2026-10-27 00:11 作成）。Primary Milestone: MG006。Related: [WS245](../ws245/ws.md)、[WS252](../ws252/ws.md)（D3「Meteor Lake対応のときに入れます。」はこの WS の指示で改める）、[WS293](../ws293/ws.md)（render/ → vk/）。
- Status: canceled（2026-10-27 00:18 ユーザー「了解です。Meteor Lakeを持っておらず、Lunar Lakeは持っています。Meteor Lakeは入手したら後日やります。i915のGuCは実装せず、xeドライバを開発するときにGuC対応を実装することにします。xeドライバはMeteor Lake以降のiGPUと、DiscreteのArcのためのものとします。既存i915の利用できるところはコピーして、重複させて作ります。調査のphaseから始めて、全体のphaseをあとで追加します。WSだけ立てておいてください。」 → i915 の GuC は実装しない、GuC は xe の driver（[WS295](../ws295/ws.md)）で）。前: planning（2026-10-27 00:11 新設。計画と見積もりをユーザーに提示、承認待ち）
- 出典: 2026-10-27 00:11 ユーザー「新しいWSを立てて、GuCによる描画に対応します。既存のexecutorも残して、カーネルのオプションをつけると、Gen12以降でだけGuCを有効にします。LinuxのドライバやMesaを見て、値だけ引用を明示して著作権は影響を受けない、既存のガードレールをそのまま適用します。計画を立てて工数を見積もってください。」

## 単一の到達目標

i915 の Gen12 以降（TGL・RKL・ADL-S/RPL-S・ADL-P）で、kernel の option を付けた時だけ GuC の firmware を load し、render・copy・compute の engine の submission を GuC 経由で行う。option が無い時・Gen12 より前・GuC の load の失敗の時は、今の execlists の executor のまま動く。

## 制約

- 今の execlists の submission と Vulkan の executor（render/、WS293 で vk/）は残し、既定は execlists。
- Linux の i915（GPL）・Mesa（MIT）は値・表・手順の参照だけ。code は写さず、block ごとに「Value(s) referenced: …」「Procedure referenced: …」（[Guardrail](../guardrail.md)「MIT・GPL の出典のデータの引用」）。GuC の interface の値（H2G・G2H の action の番号、ADS・CTB の layout、WOPCM の値）は linux の固定の版（6.8.12 の tarball、WS252 で検証済み）から。
- GuC の firmware は zedBSD-firmware の i915/ と i915-firmware の package（WS252 q1601 と同じ形、push はユーザーの指示）。HuC は使わない。
- HAL の API は変えない。

## Phase と見積もり（LW、±50%。1 LW ≈ 実時間 15〜25 分）

| Phase | 内容 | 依存 | LW |
| --- | --- | --- | --- |
| p001 | 設計（GuC の版の固定、WOPCM・ADS・CTB の layout、submission の抽象の切り方、reset、option の名前と既定、試験）と design-reviewer | — | 4 |
| p002 | firmware: GuC の bin（tgl_guc_70・adlp_guc_70 など、版は p001）を zedBSD-firmware と i915-firmware に | p001、WS252 q1601 | 2 |
| p003 | GuC の load: WOPCM の分割、DMA の upload、RSA の検証の待ち、init の param、log の buffer、状態の読み | p002 | 6 |
| p004 | ADS（golden context、WA と reg の save/restore の表、capture の list） | p003 | 5 |
| p005 | CT の channel（H2G・G2H の CTB、doorbell・割り込み、G2H の handler の枠） | p003 | 6 |
| p006 | submission の backend: context の登録・sched の enable/disable・deregister、request・fence・割り込みを execlists と共通の口に（submission の抽象） | p004、p005 | 12 |
| p007 | reset と回復（GuC の engine reset の通知、全体 reset への後退、再登録、GuC の再 load） | p006 | 6 |
| p008 | kernel の option と後退（Gen12 以降だけ、load の失敗・未対応で execlists） | p006 | 1 |
| p009 | host 試験（GuC の model・CTB の stub、layout の照合を Linux から機械で読む） | p003〜p008 と並べて | 5 |
| p010 | 実機（5330、ADL-P）: option の on/off で desktop・mview・WS291 の bench を比べる | p006〜p009 | 4 |
| p011 | 出典の監査と規約の全文の見直し | 全部 | 3 |
| （任意）p012 | SLPC（GuC の電力・周波数の管理）。Linux は GuC submission と組で使う | p006 | 5 |

- 合計: 54 LW（SLPC を入れると 59 LW）、実時間で約 14〜23 時間（担当 1 人）。p004・p005 と p009 は並べられ、2 人なら約 2/3。
- 実機は 5330 だけ（ADL-P）。TGL・RKL・ADL-S は host 試験まで（WS244・WS252 の決定と同じ）。

## 判断待ち（ユーザー）

1. SLPC を入れるか（p012）。
2. option の形: boot の引数（例 `i915.guc=1`）か build の config か、両方か。
3. GuC の firmware の版（Linux 6.8.12 が要る 70.x の版に固定する案）。
