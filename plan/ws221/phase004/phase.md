<!-- awesome-plan project=zedbsd record=ws221-p004 -->
# ws221-p004: 設定（net.conf の mtu、net の console、reconcile、networkd、policy）

Status: in-progress（2026-10-29 P3 q1664-i03: 実装と host 試験は済み、QEMU の試験を流している）
Parent: [WS221](../ws.md)
Queue: q1664 / q1664-i02 → q1664-i03（P3）
設計: [p001](../phase001/phase.md) §3.7（net.conf・net の console・reconcile・networkd の protocol・起動の経路・Settings の経路・host 試験）、§9 は全部推し

## 済み

| 項目 | file | commit | 確かめ |
| --- | --- | --- | --- |
| net.conf の interface の key `mtu: N`（1280〜65535、一度だけ）、`struct netconf_interface` の `mtu`・`mtu_set`、validate、writer は `mtu_set` の時だけ | `userland/base/net/netconf.{h,c}` | f2827fdb3 | host-netconf-mtu PASS |
| reconcile: enabled の interface で previous と MTU が違えば `emit_interface` の前に `MTU IF N`（target に無ければ `MTU IF 0`）。DOWN/UP を挟まない、disabled には出さない。**previous で disabled だった interface は MTU が分からない扱いで target の値を出す**（disabled には MTU を付けないので、同じ値でも付いていない。i03 で直した） | `userland/base/net/reconcile.c` | f2827fdb3・967e6354d | host-netconf-mtu PASS |
| protocol: `NETWORKD_OP_MTU = 21`、`NETWORKD_FIELD_MTU = 24`、LAN_ENABLE の record の codec `networkd_lan_record_format`・`networkd_lan_record_parse` | `userland/base/net/protocol.{h,c}` | f2827fdb3 | host-lan-policy-mtu PASS |
| net: backend の `MTU`（INTERFACE と FIELD_MTU、値は 0 か 1280〜65535、`decimal_mtu`）、config-if の `mtu N`・`no mtu` と help の行、`lan_send_policy` を `networkd_lan_record_format` に（enabled で `mtu_set` の時 record の `mtu`） | `userland/base/net/main.c` | aa0aaa093 | amd64 の build warning 0 |
| networkd: `decode_request` の FIELD_MTU（seen 512、0 か 1280〜65535）、MTU の field の組（INTERFACE+MTU、TOKEN 可）、handler（`/sbin/ifconfig IF mtu N|default`、成功で `networkd_lan_set_mtu`、再構成しない）、serialize の条件に MTU、`operation_name` の "MTU"（root だけ）、rollback の `V1 MTU IF N` と interface 名の検査、`lan_policy_decode` を `networkd_lan_record_parse` に、`lan_configure_steps` の up の前に `ifconfig IF mtu N`（失敗は syslog して続ける）、Settings の経路（`lan_configure_save` が保存後の item の MTU を返し `lan_configure_policy` が policy に写す） | `userland/base/networkd/main.c` | aa0aaa093 | 同上 |
| managed-lan: `struct networkd_lan_policy` の `mtu`、`networkd_lan_set_mtu`（policy に名がある時だけ値を書く） | `userland/base/networkd/managed-lan.{h,c}` | aa0aaa093 | host-lan-policy-mtu PASS、managed-lan-host-test PASS |
| docs: console の一覧に `mtu BYTES`・`no mtu`、up のまま変わる・disabled には付けない・起動と cable の差し直しで付ける・address の無い interface の制限・古い image の互換 | `docs/reference/network-console.md` | 967e6354d | — |
| host 試験 | `plan/ws221/tests/host-netconf-mtu.{c,sh}`（parse・validate・write の往復、拒否、reconcile の emit）、`host-lan-policy-mtu.{c,sh}`（record の全ての形の往復と不正な形、Settings の edit が mtu を保つ、`networkd_lan_set_mtu`） | 967e6354d | 2026-10-29 06:4x: 両方 PASS（ASan・UBSan） |

host 試験の範囲の注: `lan_send_policy` の netconf の item から record への写し（mask の計算を含む）は net/main.c の中で、host 試験は codec の往復までを見る。写しは QEMU の試験 5（起動の経路）で確かめる。

## 残り（再開点）

1. QEMU: `plan/ws221/tests/qemu-netconf-mtu.sh`（image は config-amd64-jumbo.mk、p001 §8.2 の 6・7）: console の `mtu 1400` → commit → net.conf と ifconfig、`no mtu`、`mtu 9000` の拒否、`commit confirmed 1` の timer の rollback、再起動の後の 1400、adapter の抜き差しの後の 1400。2026-10-29 06:45 に流し始めた（出力 build/tmp/qemu-netconf-mtu-1.txt）。
2. 5 platform の vmunix と net・networkd の build warning 0（merge の前）。
3. 実機は p006。

## 実機の状態

- 2026-10-29 06:3x（q1664-i03 の始め）に VF2 を読んだ: `uname` は zedBSD riscv64、`/boot/boot0/vmunix` は p3-ws221 の build/vf2/vmunix（05:46、8ea1d234c の tree）と sha256 一致、vmunix.old は別物（b0166efaf の記録）、en0 mtu 1500、TX dropped 416924（前の run の値のまま、再起動していない）。記録と一致。この Phase では VF2 に何も置いていない。
