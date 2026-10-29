<!-- awesome-plan project=zedbsd record=ws315-p001 -->
# ws315-p001: 設計（Realtek の vendor mode の driver）

Status: in-progress（2026-10-30 P3 q1694-i01: design.md を作成、design-reviewer に回す）
Parent: [WS315](../ws.md)
Queue: q1694 / q1694-i01（P3）
依存: WS221 p014（xHCI の 1 endpoint に複数の要求、NCM の到達点）

## 由来

- ユーザー（原文、2026-10-30）: ws.md の「ユーザーの決定」。「Realtek方式については、NCMでどこまで速度が上がるか観察してから、ぜひ実装しましょう。」
  「5GbEのNICは持っていません。今後購入してテストしたいです。資料だけで実装しておいてほしいです。」
- ユーザー（原文、2026-10-30、Q1 経由）:「試験に関しては私はすべてのライセンスに同意しておりますので、自動でいいです」→ image の build が firmware の license の確認で
  止まる時は自分の worktree で `yes | make firmware-licenses` を流してよい（Q1）。5330 の image の試験と camcap ができる。
- 基準（WS221 p014）: 5330 の RTL8156、NCM send 2097・recv 1449、Linux r8152 send 2353.4・recv 2353.6 Mbit/s。

## 設計

[design.md](../design.md)。要点:

- 出典: FreeBSD 15.0 の if_ure（BSD）が主。Linux r8152（GPL）は読むだけ、値と手順は block ごとに出典を示して使う（guardrail「MIT・GPL の出典のデータの引用」）。
- 新しい driver `usb-rtl815x`（RTL8156・8156B、RTL8157 は資料だけ）。vendor の interface（0xff/0xff/0）に match 120 を返し、USB core が config 1 を選ぶ（NCM の 100 より上）。
  USB core・NCM・xHCI は変えない。選び分けは `CONFIG_DRIVER_USB_RTL815X` と boot の token `usb.rtl815x=off`。
- RX の集約 48 KB × 4 URB、TX の詰め 16 KB × 4 URB、TX の TCP/IPv4 の checksum と RX の checksum の offload、jumbo 9000、link は interrupt-in と worker の thread。TSO は stack に口が無く範囲の外。
- Phase: p002 実装、p003 5330 の実機、p004 RTL8157、p005 規約の全文の見直し。

## 結果

（design-reviewer の後に記入）
