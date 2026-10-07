# ws005-p023 実機の受け入れ（B5）の手順書

[p023](phase.md) の手順。ユーザーが実機で操作して見る。エージェントは鍵（WiFi の password）を扱わない: 鍵はユーザーが画面か実機の console で入れ、
SSID・鍵を記録・写真・log に残さない（写真は SSID が写らない所か、写ったらユーザーが共有しない）。実機の証拠として書き、QEMU の証拠と混ぜない。

## 準備

- image: Q1 が決めた image（WS129 の release candidate か、その時の main の image、`plan/tools/boot-test.sh` を通した物）を USB に書く。
- 機械: Latitude 5330（AX211、有線は USB の LAN の RTL8156）、Latitude 5320（有線は USB の LAN。無線は WS118 p002 の結果で、内蔵が未対応なら USB の Archer T3U）。
- AP: WPA2-PSK（か WPA2/WPA3 混在）の AP を 1 つ。WPA3-SAE 専用・PMF required の AP は範囲外（ws005-p019 q611 の記録）。
- 利用者: wheel・network group に入った利用者（デモの kei）。
- 結果の欄: 各項目の 結果（PASS / FAIL / 未実施）と、FAIL ならその時の画面の写真と一言。

## 5330（AX211）

| # | 操作 | 正解 | 結果 |
| --- | --- | --- | --- |
| A1 | USB から起動し、kei で login する | desktop が出る。system bar の network の icon がある | |
| A2 | Settings の Wi-Fi の頁で Wi-Fi を on にする | switch が on になり、AP の一覧が出る（数秒） | |
| A3 | 一覧から AP を選び、鍵を入れて Join | 「Connecting...」の後に「Connected」。鍵の欄は閉じる | |
| A4 | Terminal で `net show`、`route -n show`、`cat /etc/resolv.conf` | WiFi の interface に IPv4 の address、default route が 1 本、name server がある | |
| A5 | Terminal で `fetch -o /tmp/f https://example.com/` | 終了の状態 0、`/tmp/f` が空でない | |
| A6 | 再起動し、kei で login する（何も操作しない） | 1 分以内に A3 の AP に自動で接続する（system bar の menu で Connected）。A5 がもう一度通る（B2） | |
| A7 | USB の LAN を挿す | 有線に address が付き、default route は有線（`route -n show`）、A5 が通る | |
| A8 | USB の LAN を抜く | default route が WiFi に移り、A5 が通る（B3） | |
| A9 | Settings で Wi-Fi を off にする | 切断され、画面に off と出る。on に戻すと A6 のように再接続する（B4） | |
| A10 | 保存の無い別の AP（無ければ A3 の AP を Settings で忘れてから）に、わざと違う鍵で Join | 失敗の理由が画面に出る（鍵の誤り）。他の AP へ勝手に接続しない（BUG-187）。次の Join（正しい鍵）は通る（B4） | |

## 5320

| # | 操作 | 正解 | 結果 |
| --- | --- | --- | --- |
| B1 | USB から起動し、kei で login、USB の LAN を挿す | 有線に address、A5 が通る | |
| B2 | 無線（内蔵が対応なら内蔵、未対応なら Archer T3U を挿す）で A2・A3・A5 | 5330 と同じ | |

## 終わりに

- 鍵は保存されたまま（`~/.wifi.conf`、mode 600）。試験の後に消すかはユーザーが決める（`net wifi delete SSID`）。
- FAIL の項目は Q1 が Bug にし、WS129 の既知の問題の一覧へ渡す。
