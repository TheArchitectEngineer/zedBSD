<!-- awesome-plan project=zedbsd record=ws221-p006 -->
# ws221-p006: 実機の受け入れ（jumbo frame と TCP）

Status: cleared（2026-10-29 P3 q1664-i04: 主の実機 VisionFive 2 の値とユーザーの判定「まあこの水準なら合格です。」を根拠に。Pi 4（UGREEN）は未実施、5330 は zedBSD の側では jumbo の対象外、下の「対象と読み替え」）
Parent: [WS221](../ws.md)
Queue: q1664 / q1664-i04（P3）
依存: p002〜p005・p008・p009

## 対象と読み替え

- ws.md の案: 「5330（RTL8156）と Pi 4（UGREEN 2.5GbE）で、jumbo の switch の上の 9000 の相手と 1500 の相手の混在、大きい ping（長さの一致）、TCP の継続の転送（両向き）」
- ユーザー（原文、2026-10-29）:「P3は5330の代わりにVF2を使えませんか？こちらはjumbo frameが使えるかも？」→ Q1: 主の実機を VisionFive 2（10.0.30.2、JH7110 の GMAC、dw-eqos、1GbE）に。5330 の RTL8156 は CDC NCM で descriptor の上限 1518（mtu_max 1504、[p003](../phase003/phase.md)）なので zedBSD の側の jumbo の対象にならず、5330 は Debian で相手（9000 の相手と 1500 の相手）として使った。
- ユーザーの判定（原文、2026-10-29、p005 の VF2 の値: MTU 9000 で受け 987.1・送り 990.7〜990.9、理論 991）:「VF2で受信が若干遅いのは、割り込み応答性能なのかなあ。なんだろうね。まあこの水準なら合格です。」
- Q1 の依頼（2026-10-29）:「p006（実機の受け入れ。ユーザーの判定「まあこの水準なら合格です。」の VF2 の値を記録の根拠に）」

## 確かめた事（VF2、全部 SSH の上の program の出力と net.tcpstats で判定、console は読んでいない）

| 項目 | 結果 | 記録 |
| --- | --- | --- |
| 大きい ping（DF、長さの一致） | VF2 と 5330 を一時 9000: `ping -s 8972 10.0.30.3` が 8980 bytes で返る。1500 で `-s 1473` は `Message too long` | [p002](../phase002/phase.md)、`plan/ws221/tests/vf2-jumbo.sh` |
| jumbo の UDP と pool | 読まない UDP socket に 8000 byte × 10 と 1000 byte × 5: jumbo 2・小 5 を保ち、後で jumbo の pool が全部空く | [p002](../phase002/phase.md) |
| TCP の継続の転送（9000、両向き） | 相手 5330（9000）: 受け 990.7・990.7、送り 990.7・990.7 Mbps（30 秒、最遅の 1 秒 ≥ 987.7、理論 991）。p005 の時は受け 987.1（ユーザーの判定の値）、p009 i03 で 990.6 | [p008](../phase008/phase.md) の i04 (C)、[p005](../phase005/phase.md) |
| TCP の継続の転送（1500、両向き） | 相手 5330（1500）: 送り 949.2 ×3、受け 949.3 ×2（理論 949） | [p008](../phase008/phase.md) の i04 (C) |
| 1500 の相手との混在 | VF2 は 9000 のまま、1500 の相手 centris（10.0.10.2）と: 送り 933.8・受け 947.9 Mbps、送りの MSS は相手の 1460、自分の広告は 8960 | [p008](../phase008/phase.md) の i04 (C) |
| 回帰 | tcp-model 全 PASS、QEMU `qemu-tcp.sh` PASS、5 platform の vmunix・net・networkd の build | [p008](../phase008/phase.md) |

## 未実施

- Pi 4 の UGREEN 2.5GbE（RTL8825BU と言われた物）での jumbo: 実機を使っていない。
- 同じ時刻に 9000 の相手と 1500 の相手へ並べて流す試験（上の混在は時刻を分けた）。
- IPv6 の jumbo の TCP と Packet Too Big（[p008](../phase008/phase.md) の未実施）。

VF2 の状態（2026-10-29 08:3x）: `/boot/boot0/vmunix` は fff5467c0 の tree の build（sha256 6cf2a25b35b3564f…）、vmunix.old は 9592ac4be の tree、en0 1500。
