# ws254-p001 DHCP の 1 分おきの取り直し

Status: in-progress（2026-10-18 Q1: Status の行が無かったので写した。元: in-progress（q1226、QEMU PASS、Q1 の判定待ち））

- 親: [WS254](../ws.md)、Queue: q1226-i01（P4）、状態: in-progress（実装・host 試験・QEMU の確認は済み。cleared は Q1 の判定）
- 範囲: WS254 の完了の条件 1〜3。userland/base/networkd（managed-lan.c・managed-lan.h・main.c）。
- ユーザー（2026-10-16）:「また、networkdは、DHCPが失敗したあと、1分おきにDHCPを試みる修正を入れるのがいいと思います。」
  追加（2026-10-16、Q1 経由）:「DHCP の 1 分おきの取り直しですが、インタフェースのdown->up->DHCPという手順にしてください。でないとインタフェースのリセットが必要なケースに対処できないです。」

## 設計と実装（2026-10-16 P4）

- 判断は `managed-lan.c`（装置を開かない policy）、実行は `main.c`。
  - interface ごとに `retry_at`（次の取り直しの時刻、monotonic の µs）・`retry_pending`（次の configure は取り直し）・`resetting`（down/up の最中）。
  - `networkd_lan_retry_schedule`: configure が address を得られなかった時、**DHCP の設定・UNCONFIGURED・cable あり・held down でない・管理中**の interface にだけ、失敗の 60 秒後を予約する。static と `disabled` は対象外。
  - `networkd_lan_retry_due`: 時刻が来た interface を、まだ条件を満たせば PENDING ＋ `retry_pending` にする。`networkd_lan_retry_timeout`: event loop の poll の待ち時間。
  - `networkd_lan_next` は `work.reset = retry_pending` を返す。`networkd_lan_configured` が結果で `retry_pending`・`retry_at` を消す。
  - `net up`（`networkd_lan_release`）・設定の変更（`networkd_lan_set_interface`）・`net down`（`hold_down`）は `retry_pending`・`retry_at` を消し、今まで通りの普通の configure（reset 無し）。cable の抜き差しの扱いは変えない（cable が抜ければ取り直しの条件が偽になって止まり、戻れば普通の configure）。
- 取り直しの手順（main.c `lan_configure`）: `lan_reset_begin`（`ifconfig IF down`、syslog NOTICE「DHCP retry: taking the interface down and up」）→ 既存の `lan_configure_steps`（`ifconfig IF up` → `dhcpc -t T IF`）→ `lan_reset_end`。
  - down/up が起こす carrier の事象は reset の最中は policy の状態を変えない（`networkd_lan_event`・`networkd_lan_observe` が `resetting` を見る）。`lan_reset_end` は溜まった route の事象を読み、`resetting` を外し、`lan_snapshot` で本当の cable の有無を読み直す。up できずに down のままの interface は `networkd_lan_down` で raise からやり直す。
  - 失敗のたびに次の 60 秒後を予約。最初の失敗だけ WARNING（従来の行）と NOTICE「DHCP is asked again every 60 seconds」、取り直しの失敗・成功は NOTICE（毎分の WARNING を避ける）。
  - confirmed commit が armed の間は取り直しを走らせない（rollback が有線の状態を持つため。event loop がその間 route の事象を読まないのと同じ）。
- 169.254 は割り当てない（変更なし）。
- Wi-Fi: 変えていない。管理する Wi-Fi は DHCP の失敗（`work->stage = "DHCP transaction"`）を join の失敗として扱い、自動の探索が 5 秒（`NETWORKD_WLAN_RESCAN_SECONDS`）ごとに join から（link を作り直して DHCP）やり直すので、1 分おきより短い間隔で既に取り直している。

## 確認

| 確認 | 結果 |
| --- | --- |
| build: `make -j16 BUILD=build/ws254 ZEDBSD_CONFIG=plan/tools/guest/config-amd64-ssh.mk build/ws254/bin/networkd` | rc=0、warning 0（`-Wall -Wextra -Werror`） |
| host 試験（新規）: `sh plan/ws254/tests/dhcp-retry-host-test.sh` | **PASS** 36/36（`WS254-T001 dhcp-retry: PASS`）: 60 秒後に reset 付きで取り直し、失敗ごとに次、lease で止まる、static・管理外・cable 無し・held down は取り直さない、`net up` は reset 無し、reset 中の carrier の事象と snapshot は状態を変えない |
| 既存の host 試験: `make BUILD=build/ws254 ZEDBSD_CONFIG=plan/tools/guest/config-amd64-ssh.mk managed-lan-host-test` | **PASS**（`MLAN-T001 managed-lan: PASS`、変更前の項目そのまま） |
| `plan/tools/style-check.py`（変えた所） | 新しい code の指摘 0（残りは変更前からの行。全文の見直しは p002） |
| `git diff --check` | 0 |
| **QEMU**: `sh plan/ws254/tests/retry-qemu.sh --no-build`（image は `plan/tools/guest/test-image.sh plan/ws254/tests/config.mk build/amd64 --file /etc/net.conf=plan/ws254/tests/net.conf ZEDBSD_TEST_IMAGE_TAG=ws254`、tree は 1f22728f5 ＋この変更） | **PASS**（`WS254-Q001 retry-qemu: PASS`、leased=y after 206s、resets=3、obtained=1） |

QEMU の確認の形: guest の ue0 は QEMU の user network（SSH の転送）、ue1 は `-netdev dgram` で host の `plan/ws254/tests/dhcp-server.py` につなぎ、server は起動の 200 秒後から答える。
判定は guest の `/var/log/messages`（SSH で読んだ）、`ifconfig`（SSH）、server が受けた DHCP の message。console・serial の log は使っていない。

guest の記録（`grep networkd /var/log/messages`、guest の時計は UTC）:

```
07:54:38 networkd: ue1: dhcp obtained no address: dhcpc: ue1: offer: Connection timed out
07:54:38 networkd: ue1: DHCP is asked again every 60 seconds
07:55:38 networkd: ue1: DHCP retry: taking the interface down and up
07:55:43 networkd: ue1: DHCP retry obtained no address: dhcpc: ue1: offer: Connection timed out
07:56:43 networkd: ue1: DHCP retry: taking the interface down and up
07:56:48 networkd: ue1: DHCP retry obtained no address: dhcpc: ue1: offer: Connection timed out
07:57:48 networkd: ue1: DHCP retry: taking the interface down and up
07:57:48 networkd: ue1: DHCP retry obtained an address
```

server の側（host の時計）: DISCOVER の組の始まりが 4.6 s・69.9 s・135.0 s・200.1 s（失敗の 60 秒後 ＋ dhcpc の 5 秒）、200.1 s の組に OFFER・ACK を返した。
その後の `ifconfig ue1` は `inet 192.168.77.10 netmask 255.255.255.0`、ue0 は 10.0.2.15 のまま（SSH は終始つながった）。
証拠: worktree `/home/awe/zedBSD-worktrees/p4/build/ws254/retry-qemu.run.0Sxf2R/`（result.txt・messages-networkd.txt・dhcp.log・ifconfig-a.txt・screen.png。commit しない）。

未実施: 実機（5330・RDNA 機）での確認。QEMU の usb-net で `ifconfig down/up` が carrier の事象を出すかは見ていない（どちらでも動く設計。reset 中の事象は host 試験で確かめた）。cable の抜き差しの QEMU での確認（host 試験だけ）。

## 試験の file

- `plan/ws254/tests/dhcp-retry-host-test.c`・`.sh`（host、guest 無し）。
- `plan/ws254/tests/retry-qemu.sh`・`dhcp-server.py`・`config.mk`・`net.conf`（QEMU）。

## 残り

- p002（規約の全文の見直し）。
- 回帰に残すかは Q1 の判断（残すなら master の Tools へ）。
