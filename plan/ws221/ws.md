<!-- awesome-plan project=zedbsd record=ws221 -->

# WS221: Jumbo frame（Ethernet の L2 の大きな MTU）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-13 Q1 作成。担当は次に空いた P。WS220（TCP）と関係: MSS は MTU から決まる）
Primary Milestone: MG005
Related Milestones: なし
<!-- awesome-plan-current:end -->

## 原文（2026-10-13 ユーザー）

「オーケーです。あと、JUMBO FRAMEもお願いします。これはL2ですね。」（WS220 の TCP の Window Scale の話の後）

## 目的と完了の条件

1. 網の device ごとに MTU を 1500 より大きく（最大は device の上限、目標 9000）設定でき（`ifconfig <if> mtu N`、`net` の設定、再起動後も保つ）、受け・送りの両方で大きな frame を扱える。上限を超える値は断る。
2. 対象の driver の能力を調べて対応する（p001 で一覧）: Pi 4 の GENET（`src/drivers/platform/rpi4/rpi4-ethernet.c`、hardware の受けの buffer と最大の frame の長さ）、USB の CDC ECM・NCM（`src/drivers/ethernet/usb-cdc-ecm.c`・`usb-cdc-ncm*.c`、descriptor の wMaxSegmentSize）、amd64 の実機の有線（5320・5330 で使う物）、RTL8822BU の WiFi は対象外（802.11 の上限）。対応できない device は 1500 のまま、理由を記録。
3. IPv4・IPv6 の層が interface の MTU を使う（断片化・Path MTU、IPv6 の最小 1280）。TCP の MSS（WS220）が MTU から決まり、SYN で広告する。
4. 受け入れ: QEMU と実機（Pi 4 の GENET、5330）で MTU 9000（または device の上限）の相手と ping（DF 付きの大きな packet）と TCP の継続の転送が通り、1500 の相手とも混在して通る。build の warning 0、規約の全文の見直し。

## Phase

| Phase | 内容 | Status |
| --- | --- | --- |
| p001 | 調べと設計: device ごとの上限、MTU の設定の口（ioctl・net の設定・保存）、IP・TCP の層の MTU の使い方、packet buffer の大きさ → design-reviewer | planned |
