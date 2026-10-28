<!-- awesome-plan project=zedbsd record=ws221-p004 -->
# ws221-p004: 設定（net.conf の mtu、net の console、reconcile、networkd、policy）

Status: in-progress（2026-10-29 P3 q1664-i02: 途中でラップアップ。下の「済み」と「残り」）
Parent: [WS221](../ws.md)
Queue: q1664 / q1664-i02（P3）
設計: [p001](../phase001/phase.md) §3.7（net.conf・net の console・reconcile・networkd の protocol・起動の経路・Settings の経路・host 試験）、§9 は全部推し

## 済み（WIP commit、未試験）

| 項目 | file | 確かめ |
| --- | --- | --- |
| net.conf の interface の key `mtu: N`（1280〜65535、一度だけ）、`struct netconf_interface` の `mtu`・`mtu_set`、validate、writer は `mtu_set` の時だけ（`vlan-id` の後）、`NETCONF_MTU_MIN/MAX` | `userland/base/net/netconf.{h,c}` | host の cc（-Wall -Wextra -Werror）で compile のみ |
| reconcile: target の enabled の interface で previous と MTU が違えば `emit_interface` の前に `MTU IF N`（target に無ければ `MTU IF 0`）。DOWN/UP は挟まない、disabled には出さない | `userland/base/net/reconcile.c` | 同上 |
| protocol: `NETWORKD_OP_MTU = 21`、`NETWORKD_FIELD_MTU = 24`、LAN_ENABLE の record の codec `networkd_lan_record_format`・`networkd_lan_record_parse`（`struct networkd_lan_record`、末尾の `mtu N`、dhcp は 3/5 語・static は 4/6 語・disabled は 2 語、他は EINVAL。host 試験のため net と networkd の両方から使う形に切り出した） | `userland/base/net/protocol.{h,c}` | 同上。まだ誰も呼ばない |

## 残り（再開点）

1. `net/main.c`: `backend_opcode` に `{ "MTU", NETWORKD_OP_MTU }`、`backend_payload` に MTU（count 2、INTERFACE と `networkd_field_write_u32(FIELD_MTU)`）。**これが入るまで、手で net.conf に `mtu` を書いて console で変えると reconcile の `MTU` が「invalid backend request」で commit が失敗する**（`mtu` を書かなければ何も変わらない）。
2. `net` の console: `mtu N`・`no mtu`（config-if）、help の行、`docs/reference/network-console.md` の一覧。
3. `lan_send_policy` を `networkd_lan_record_format` に（item の `mtu_set` を record の `mtu` に）。
4. networkd: `decode_request` に FIELD_MTU（seen の bit 512、0 か 1280〜65535）、opcode の field の組（INTERFACE+MTU、TOKEN 可）、`execute` の handler（`/sbin/ifconfig IF mtu N|default`、`run_command_until`、成功で managed-lan の policy の `mtu` だけを書き換える新しい関数 `networkd_lan_set_mtu`、再構成はしない）、serialize の条件（3937 付近: `ROUTE6_REMOVE` と同じく明示に足す）、`operation_name` に "MTU"（root だけ、members の一覧に入れない）、rollback の parser に `V1 MTU IF N`、interface 名の検査の一覧に MTU。5672 付近（構成中の interface を止める op）には入れない。
5. `lan_policy_decode` を `networkd_lan_record_parse` に、`struct networkd_lan_policy` に `unsigned mtu`、`lan_configure_steps` の `ifconfig IF up` の前に `policy.mtu != 0` なら `ifconfig IF mtu N`（失敗は syslog して続ける）、`lan_configure_policy` に編集後の item の `mtu` を写す。
6. host 試験 `plan/ws221/tests/host-netconf-mtu.c`・`host-lan-policy-mtu.c`（p001 §3.7 の「host 試験」）、`managed-lan-host-test` の非退行、5 platform の vmunix と net・networkd の build warning 0。
7. QEMU で net.conf の `mtu 1400` → 再起動で ue1 が 1400（p001 §8.2 の残り）。実機は p006。

## 実機の状態（2026-10-29 ラップアップの時）

この起動（q1664-i02）では VF2・5330 に触っていない。VF2 は前の起動の記録のまま: `/boot/boot0/vmunix` は 8ea1d234c の tree の build（vmunix.old は b0166efaf、vmunix.p3orig は元）、en0 の MTU 1500（前の起動の script が読み戻した）。2026-10-29 ユーザーの指示で VF2 をユーザーに返した。
