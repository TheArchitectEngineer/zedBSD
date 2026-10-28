<!-- awesome-plan project=zedbsd record=ws004 -->

# WS004: hardware expansion

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」: 主な USB・WLAN・NVMe の経路は完了、残りは実機の確かめと任意の後続だけ）
<!-- retained-after-completion --> tests/ の 4 項目 はその場に残す: src/・userland/・platform/・Makefile・config/ の source か build の file が path を名指す（AGENTS.md §10 の source の comment の掃除の後に消す。2026-10-28 Q1 の方針、q1660）
Completed: 2026-10-27（q1608 の精査）
Primary Milestone: MG003
Related Milestones: MG004, MG008
Objectives: O1, O2, O4
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

対象の laptop（Dell Latitude 5320、後に 5330）に要る再利用できる PCIe・DMA・割り込み・reset・firmware の土台と、xHCI・USB root・USB Ethernet（CDC NCM・ECM）・NVMe・WLAN（RTL8822BU、Intel AX211/CNVio2）の native driver を作る。model・QEMU の結果と実機の結果を分けて記録する。

完了の条件（元の定義）: 共通の土台が focused な回帰試験を通り、xHCI と NVMe が実機で通り、RTL8822BU が lifecycle・回復の gate を通り、AX211 が通常の経路を通った後に共通化の見直しをする。driver の完了は、発見・通常動作・並行・timeout・reset・detach/shutdown・診断の全てを覆うこと（probe だけ、stub で成功を返す物は部分）。i915 は別の WS。

## ユーザーの決定

- 2026-10-07: NVMe などをベータ3 へ（原文の抜粋「…NVMe…」）。
- 2026-10-13: 「WS004は、5330のNVMeに書き込んでいいので、ベータ2に移します。」
- 2026-10-27（q1608）: 「…masterとWSを修正して、completedやcanceledにしてください。」「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」
- p037: ユーザーが Intel の対象を観測した AX211/CNVio2（`8086:51f0`）に直した（AX201 の仮説の訂正）。
- p045・p046: Archer T3U Plus はユーザーの依頼による調査と拡張。

## 結果

- PCIe・ECAM/MCFG・MSI/MSI-X の HAL の契約と lifecycle、shared INTx、DMA の土台（p001〜p003、p009）。
- xHCI（MSI-X、USB storage、切断・再接続、endpoint ごとの並行 URB、cancel・drain・reclaim）、UHCI/EHCI の検査付きの退避と並行の schedule、USB の function model（複数の configuration・alternate・IAD）、binding の transaction、STALL・clear-halt・direct-root の回復、USB storage の有限の回復（p004、p006、p010〜p012、p015、p016、p031、p032、p049）。
- USB root の連続性・overlay の書き込み（500 回の起動の gate）、PC/AT の warm reset、SMP の kernel heap の lock の統一、framebuffer console の直列化（p005〜p008、p033）。
- CDC NCM（wire codec、driver、回復と計数、RTL8156 の association）と CDC ECM（QEMU の基準の 4 cell）（p013、p014、p017〜p020）。RTL8156 の物理の carrier・DHCP は WS005 p001。
- NVMe（admin・Identify・I/O queue・flush・timeout/reset・GPT、QEMU の受け入れ、複数の controller。BUG-016 は直った）（p022〜p024、p050）。
- WLAN: 版付きの pointer を持たない WLAN ioctl と共通の core・fake radio（p027）、RTL8822BU の firmware・scan・WPA2/CCMP・lifecycle・W52・接続の状態機械（p026、p028〜p030、p036、p040〜p044）、Archer T3U Plus（p045・p046）、Intel AX211 の独立の driver（p037・p038、VFIO の正確な device で firmware/PNVM・scan・WPA2・DHCP・ping・HTTP の取得まで）、lifecycle の見直しと stop（p047・p048）。物理の RTL8822BU の経路はユーザーが確かめた（p043、q071）。AX211 の実機の接続は WS248・U9 の記録による。
- 共通化の見直し（p039、2026-10-13 P2 q963）: 抽出は無し（CCMP の header の検査だけが重複するが、offset・bit・error・lock が違う）。source の変更なし。

## 制限・移管

- 実機の確認は未実施: p005・p006 の手動の受け入れ、p021 の Latitude の carrier/DHCP の checkpoint、p025 の SN740 の read-only の受け入れ、p038 の AX211 の direct boot の最終の結果、p051（BUG-134、passthrough で再現せず。BUG-134 は 2026-10-02 ユーザーの実機の確認で resolved）。
- p034（CDC ECM の非同期の TX error の計数）: code と build まで。QEMU の自動の gate の再実行は未実施（p019 の ECM の fixture が tree に無い）。
- p035（同じ endpoint の複数の URB の ring）: 計測で要る時だけの任意の後続、未着手。
- RTL8822CE は別の後の対象。i915 は WS014・WS029 以降。
- MSI の後続（p003 で見送り）: 複数 message の conventional MSI（消費者が要る時）、MSI の affinity（CPU 0 固定）、arm64 の IORT/GIC ITS（未対応を返す）、PCI 以外の message の源。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws004-p001 | PCIe・DMA・割り込み・xHCI の土台の監査 | complete（software の監査と共通の修正 2 件） |
| ws004-p002 | PCIe と xHCI の前提 | complete（software） |
| ws004-p003 | ECAM と MSI の割り込みの所有 | complete（software、QEMU の配送） |
| ws004-p004 | xHCI host controller | complete（QEMU の storage） |
| ws004-p005 | USB の boot/root の連続性 | 自動の QEMU は complete、手動の受け入れは未実施（2026-10-27 ユーザー「実機確認だけが…」で閉じた） |
| ws004-p006 | USB の overlay の書き込みの信頼性 | 自動の QEMU は complete（500 回の gate）、手動の受け入れは未実施 |
| ws004-p007 | PC/AT の warm reset の再初期化 | complete |
| ws004-p008 | SMP の起動中の kernel heap の整合 | complete（q010） |
| ws004-p009 | PCI HCD の IRQ の検査付きの teardown | complete（q016） |
| ws004-p010 | USB の function と alternate setting の model | complete（q027） |
| ws004-p011 | xHCI の endpoint ごとの並行 URB | complete（q027） |
| ws004-p012 | network device の hotplug と shutdown の寿命 | complete（q027） |
| ws004-p013 | CDC NCM の wire codec と交渉 | complete（q027） |
| ws004-p014 | CDC NCM の network driver | complete（q027） |
| ws004-p015 | USB の binding と interface の transaction | complete（q027） |
| ws004-p016 | legacy HCD の検査付きの request の退避 | complete（q041） |
| ws004-p017 | CDC NCM の回復と完了の計数 | complete（q054） |
| ws004-p018 | RTL8156 の CDC NCM の association | complete（q028） |
| ws004-p019 | 独立の CDC ECM と QEMU の基準 | complete（q049） |
| ws004-p020 | CDC NCM の受信と open の強化 | complete（q029） |
| ws004-p021 | xHCI SuperSpeed の interrupt endpoint context | uncleared のまま WS を閉じた（自動・QEMU は通過、Latitude の checkpoint は未実施） |
| ws004-p022 | NVMe の controller と namespace の発見 | complete（q030） |
| ws004-p023 | NVMe の I/O と lifecycle | complete（q030） |
| ws004-p024 | QEMU の NVMe の block の受け入れ | complete（q030） |
| ws004-p025 | Latitude 5320 の NVMe の read-only の受け入れ | planned のまま WS を閉じた（実機の確認は未実施） |
| ws004-p026 | Archer T3U Nano の identity と firmware の方針 | complete（q055） |
| ws004-p027 | 汎用の WLAN UAPI・core・fake device | complete（q055） |
| ws004-p028 | RTL8822BU の attach・firmware・scan | 自動は complete（q057）、物理は q071 の共有の受け入れ |
| ws004-p029 | WPA2-Personal/CCMP と L2 の data path | 自動は complete（q058）、物理は q071 の共有の受け入れ |
| ws004-p030 | WLAN の lifecycle・再接続・強化 | 自動は complete（q060）、再接続の方針は p044 が置き換え |
| ws004-p031 | legacy HCD の並行の schedule と root port | complete（q047） |
| ws004-p032 | USB の endpoint-halt と direct-root の回復 | complete（q047、物理の回復は未試験） |
| ws004-p033 | amd64 の framebuffer console の直列化 | complete（q047） |
| ws004-p034 | CDC ECM の非同期の TX の計数 | 実装・build まで（test-wait のまま WS を閉じた、QEMU の gate は未実施） |
| ws004-p035 | USB の同じ endpoint の複数 URB の ring | 取り下げ（deferred、計測で要る時だけ） |
| ws004-p036 | RTL8822BU の radio 前の土台 | complete（q056） |
| ws004-p037 | Intel WLAN の identity と firmware の境界 | complete（q061） |
| ws004-p038 | 独立の Intel Wi-Fi 6E AX211 の通常の経路 | uncleared のまま WS を閉じた（自動と VFIO は通過、direct boot の最終の結果は未実施） |
| ws004-p039 | 証拠に基づく WLAN の共通化の見直し | 見直し済み・抽出なし（2026-10-13 P2 q963、cleared の提案のまま閉じた） |
| ws004-p040 | RTL8822BU の passthrough の退行の再現 | complete（q068） |
| ws004-p041 | RTL8822BU の 5 GHz の通常の経路 | complete（q071） |
| ws004-p042 | RTL8822BU の 2.4 GHz の接続の再現 | complete（q069） |
| ws004-p043 | RTL8822BU の物理の接続の状態と command の UX | complete（q070、ユーザーの確認） |
| ws004-p044 | 非同期の WLAN の操作の境界 | complete（q071） |
| ws004-p045 | Archer T3U Plus の可否の調査 | complete（調査のみ） |
| ws004-p046 | Archer T3U Plus の driver の拡張 | complete（q083） |
| ws004-p047 | AX211・RTL8822BU の lifecycle の見直しの修正 | complete（q085） |
| ws004-p048 | 観測できる、独立に再試行する WLAN の stop | complete（q085、RF は未試験） |
| ws004-p049 | USB storage の有限の回復 | completed |
| ws004-p050 | 複数の NVMe controller | completed（q187） |
| ws004-p051 | BUG-134 AX211 の起動停止の解析 | uncleared のまま WS を閉じた（passthrough で再現せず。BUG-134 は resolved） |

## 記録の所在

Phase の計画・結果・試験（`tests/` の HW-T の表と evidence を含む）は削除する。git の commit `fb1f6afee` の `plan/ws004/` にある。Queue の履歴は [plan/history](../history/index.md)。
