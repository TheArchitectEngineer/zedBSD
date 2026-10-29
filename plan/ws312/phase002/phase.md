<!-- awesome-plan project=zedbsd record=ws312-p002 -->

# ws312-p002 再組立ての実装と host の単体の試験

- 親: [WS312](../ws.md)。Queue: q1672 / q1672-i01（担当 P6、2026-10-29）。設計は [p001](../phase001/phase.md)（版 2）。
- Status: cleared（2026-10-29。実装と host 試験。QEMU の確かめは p003）

## 成果

- `src/kern/net/ipv6-frag.c`（新）: 再組立ての全部（記録・片・時間切れ・上限・追い出し・組み立て・ECN・RFC 7112 の鎖の検査）。
- `src/kern/net/ipv6.c`: `ipv6_extensions` を `struct packet_buf **` に、fragment header で `ipv6_fragment_input`、2 つ目の fragment header と fragment で来た ND（133〜137）を捨てる、
  `ipv6_init`・`ipv6_timer_run`・`ipv6_timer_next_deadline`・`ipv6_purge_device` から呼ぶ。開発の管理情報（ws130・decision H6）を消した。
- `src/kern/net/packet-buf.c`: 確保されていない class の配列の範囲を計算しない（下の確かめ）。
- `src/kern/net/ipv6.h`: 宣言、`ICMP6_PARAMETER_FIRST_FRAGMENT`、管理情報の除去。`src/kern/net/wire.h`: fragment header の comment。`Makefile`: kernel の source に 1 行。
- 試験: `plan/ws312/tests/frag-host-test.c`・`run-frag-host-test.sh`。

## 確かめ（2026-10-29、host）

- `sh plan/ws312/tests/run-frag-host-test.sh`: `AMD64 frag-host-test: PASS (333 checks, largest 9152)`、`I386 frag-host-test: PASS (301 checks, largest 1984)`、`run-frag-host-test: PASS`（ASan・UBSan）。
  試験の中で見つけた既存の未定義の動作（32 bit の board で、確保されていない jumbo の配列の NULL から `&packet_jumbo_pool[count]` を計算していた、
  `packet_buf_free`・`packet_buf_physical`）を Q1 の指示（2026-10-29「今直して」）で直した: 配列が NULL の class は範囲を計算しない。UBSan の pointer-overflow を外さずに PASS、
  `sh plan/ws221/tests/run-packet-buf-host-test.sh` も PASS（AMD64・I386）。
- 変異の試験（scratchpad で ipv6-frag.c を 1 箇所ずつ壊して同じ試験に通す）: 重なりの検査・Time Exceeded の code・next header の書き換え・重複の判定・鍵の device・全体の上限・
  若い記録・jumbo の上限・TCP の大きさ・CE・ICMPv6 の error の判定の 11 個の変異が全部 FAIL になった。
- build: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ci-amd64 build/ci-amd64/vmunix`、同じく `config-rpi4.mk`・`config-riscv64-virt.mk` が rc=0 warning 0（a87a9781b）。

## 未実施・制限

- QEMU・実機は未実施（p003）。制限は p001 の「未実施・制限」。
