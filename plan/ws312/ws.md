<!-- awesome-plan project=zedbsd record=ws312 -->

# WS312: IPv6 の fragment の受け取りと再組立て

Status: completed（2026-10-29、Q1 の判定）
Primary Milestone: MG005

## ユーザーの指示（原文、2026-10-29）

「IPv6のfragment対応は、今P6を立ててやっていいです。」（F-WS221-ipv6-frag を昇格。P6 の通常の「実装だけ・merge しない」と違い、試験まで行い merge した。）

## 結果

- `src/kern/net/ipv6-frag.c`（新）: 受けた IPv6 の fragment を再組立てして上の層に渡す（RFC 8200 §4.5）。重なりは全体を捨てる（RFC 5722）、atomic fragment はそのまま渡す（RFC 6946）、60 秒の時間切れと ICMPv6 Time Exceeded code 1（offset 0 の片を受けていた時だけ）、ECN は RFC 3168 §5.3、fragment で来た ND（type 133〜137）は捨てる（RFC 6980）、2 つ目の fragment header は捨てる。
- 上限: 1 つの packet に入る所まで（64 bit は 9152 byte、32 bit は 1984 byte）、TCP の datagram は標準の packet（1984 byte）まで、片の数は 32（jumbo の片は 12、driver の受けの予約 8 を残す）、最初の片から 1 秒の内の記録は追い出さない。
- `src/kern/net/ipv6.c`: `ipv6_extensions` が fragment header で `ipv6_fragment_input` に渡す。timer・purge・init の呼び出し。
- ついでに直した物: `src/kern/net/packet-buf.c` の、確保されていない class（NULL）から範囲を計算していた所（UBSan の pointer-overflow、害は無かった）。
- 確かめ: host の単体の試験（ASan・UBSan、64 bit 333 checks・32 bit 301 checks、変異 11 通りが全部 FAIL になる）、amd64・rpi4・riscv64 の vmunix warning 0、QEMU（`plan/tools/ipv6-frag/qemu-frag.sh`、ok 19・FAIL 0 を 2 回）: ping6 の 1408 byte の返り、UDP 1400・4000・8000 byte が長さと hash の一致で届く、12000 は届かない、逆順・atomic は届き重なりは届かない、Time Exceeded・Parameter Problem、fragment の NS には答えない。実機は未実施（QEMU の証拠だけ）。

## 制限・移管

- 送りの fragment（自分が分けて送る）は範囲の外（`ping -6 -s 3000` の返りは来ない）。IPv4 の fragment の再組立ても無い（`ipv4.c` が捨てる、ユーザーの指示が IPv6 だけ）。
- 9152 byte を超える datagram（RFC 8200 の 65535 まで）は組み立てない。追い出しは送り元を区別しない。記録の鍵の device は参照を取らない pointer。RFC 7112 の上の層の長さの検査は最小の長さだけ。辿る extension header は 0・43・60 だけ。統計の数は数えない。
- 試験の道具は `plan/tools/ipv6-frag/`（[plan/tools.md](../tools.md) に登録）。

## Phase（全て cleared、記録は git の履歴に）

| Phase | 目的 | 状態 |
| --- | --- | --- |
| p001 | 調べと設計（design-reviewer の指摘を版 2 で全部反映） | cleared |
| p002 | 再組立ての実装と host の単体の試験 | cleared |
| p003 | QEMU の試験 | cleared |
| p004 | 規約の全文の見直し | cleared |
