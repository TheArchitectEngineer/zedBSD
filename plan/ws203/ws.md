<!-- awesome-plan project=zedbsd record=ws203 -->
# WS203: Raspberry Pi 4 内蔵 Ethernet (BCM2711 GENET v5)

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: p001・p002・p004 cleared（GENET と BCM54213PE の driver、main 7b16e364c）。残りの p003 は Pi 4 の実機の有線 LAN・DHCP・SSH の確かめだけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」）
Completed: 2026-10-27（q1608 の精査）
Primary Milestone: MG005
Related Milestones: MG003・MG008
Objectives: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

USB とは独立した GENET v5 と BCM54213PE の PHY を既存の network stack に登録し、既存の networkd・DHCP・SSH から使えるようにする。範囲は MTU 1500、ring 16、IPv4/IPv6 の既存の stack。offload・jumbo・WOL・suspend・複数 queue は含まない。HAL の API は変えない。

## ユーザーの決定

- 2026-10-11（由来）「とりあえず、RPi4のEthernetドライバを実装できますか？デバッグが楽になると思います。」
- 2026-10-11「src/drivers/ethernet/bcm54213pe.c と、src/drivers/platform/rpi4/rpi4-ethernet.c に分けて実装するのがいいと思います。」、続けて訂正「rpi4-ethernet.c に GENETのMACがあるんじゃないかなあ？」→ GENET の MAC・DMA・ring・net_device・FDT・board の起動は `rpi4-ethernet.c`、外付けの PHY だけ `bcm54213pe.c`（MDIO の callback で繋ぐ）。
- 2026-10-11「mainに統合をお願いします。」（e9ddb4540・d1def8aef）、「main仁藤剛してください。」（7b16e364c の統合の承認への回答）。
- 2026-10-27（完了の根拠、q1608 経由）「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」

## 結果

- `src/drivers/platform/rpi4/rpi4-ethernet.c`（GENET の MAC・DMA・ring・net_device・FDT）と `src/drivers/ethernet/bcm54213pe.c`（PHY）を実装し、全 CPU 共通の Ethernet の menu で ON/OFF（RPi4 で既定 ON）。実機の初期化の失敗を GIC の trigger・MMIO の共有の alias・非 coherent な DMA の page で直した（p004）。warning 0 の arm64 kernel の build と限定の host 試験 PASS。main 7b16e364c。
- 実機（2026-10-11、ユーザーの報告と read-only の SSH）: 10.0.30.2 で en0 UP/RUNNING、IPv4 あり、1000 Mbps full-duplex、RX/TX の counter が進み error・drop 0、SSH が通った。

## 制限・移管

- DHCP の lease の出所とケーブルの抜き差しは個別に確かめていない（実機の残りの確認は未実施）。
- USB の ws048-p009 の実機の失敗は別件。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws203-p001 | FDT・PHY・DMA・ring・net_device の driver と build・menu の配線 | cleared（2026-10-11） |
| ws203-p002 | 最終 source の規約の全文、warning 0 の build、限定の host model | cleared（2026-10-11） |
| ws203-p003 | 実機の有線 LAN・DHCP・SSH の受け入れ | uncleared のまま WS を閉じた（link・IPv4・SSH・送受信は実機で確認、DHCP の lease の出所と抜き差しは未確認） |
| ws203-p004 | 実機の初期化の失敗の GIC・MMIO の補完と規約・build | cleared（2026-10-11、7b16e364c） |

## 記録の所在

Phase の計画・結果・試験の証拠（`tests/`）は削除した。git の commit `fb1f6afee` の `plan/ws203/` にある。Queue の記録は [codex-queue.md](codex-queue.md)・[codex-repair-20261011.md](codex-repair-20261011.md)、履歴は [plan/history](../history/index.md)。
