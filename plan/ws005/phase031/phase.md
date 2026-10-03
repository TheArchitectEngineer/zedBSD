<!-- awesome-plan project=zedbsd record=ws005-p031 -->
# ws005-p031: BUG-157 — 鍵の拒否の理由を、その後の後始末・recovery が ENETDOWN・別の error で上書きしない（残りの経路）

Status: in-progress（q651-i01、P1 generation12、2026-10-04。実装・host 試験済み。確認は S2 の実機（AX211 は QEMU に無い））
Disposition: normal
Parent: [WS005](../ws.md)
Bug: [BUG-157](../../bugs/BUG-157.md)
Queue: q651（2026-10-04 user「P1、P2はBUG-143, 053, 052, 162, 103, 129, 033, 157, 139を修正します。直す順序は任せます。」）

## 範囲

[ws005-p028](../phase028/phase.md) は共通の WLAN core の close・quiesce の retire が理由を消す経路を直した。この Phase は残りの「後の状態が最初の理由を
上書きする」経路を読みで探して直す（実機無し）。

## 原因（読み、2026-10-04）

5330 の「Could not join (Network is down)」は system bar の `network.c` の `"Could not %s (%s)"` で、networkd の join の答えの errno（ENETDOWN）。
networkd から ENETDOWN が出る経路を読んだ。

1. **（主）networkd の `run_managed_connect` が後始末の error で join の理由を置き換える。** `wifi connect`（machine mode）が EACCES で失敗すると、
   networkd は `retire_managed_connection` で `wifi disconnect` を走らせる。その disconnect が失敗すると `saved = errno` で**後始末の error が join の
   答えになる**。AX211 の ioctl の入口は、recovery の間（`recovery_pending`・`recovery_running`）で session がまだ止まっていない時に
   `SIOCSWLANDISCONNECT` を **ENETDOWN** で断る（wifi の tool の disconnect は EBUSY だけを再試行する）。鍵の拒否の後に AX211 が recovery に入ると、
   join の答えが ENETDOWN になる。あわせて EACCES でなくなるので、手動の join の「鍵の拒否」の記録（`wifi_rejection_note`）も抜けていた。
2. **AX211 の recovery が同じ generation の 2 度目の link loss を報告し、共通 core がその理由（recovery の error、EIO など）で EACCES を上書きする。**
   `station_link_lost_controlled` は WPA2 engine が IDLE なら ENOTCONN で何もしないが、1 度目の停止で engine の後始末（key の削除）が失敗すると engine は
   FAILED のまま残り、2 度目の報告が `terminal_error` を書き換える。host 試験で再現した（下）。

## 実装

| file | 変更 |
| --- | --- |
| `userland/base/networkd/main.c` `run_managed_connect` | 失敗した join は今まで通り必ず retire するが、retire の失敗で errno を置き換えない（join の理由を返す）。retire の失敗は log（`join failed: …; its cleanup is retried`）と、既存の `schedule_retirement_retry` に任せる |
| `src/drivers/wifi/intel-ax211/intel-ax211.c` `ax211_net_ioctl` | recovery の間（quarantine でない）の `SIOCSWLANDISCONNECT` を ENETDOWN でなく **EBUSY**（再試行できる答え）に。recovery が session を止め終えると `session_stopped` で disconnect が通る。connect・その他は不変 |
| `src/kern/net/wifi/wlan.c` `station_link_lost_controlled` | station が既に FAILED で terminal_error を持つ時は、その最初の理由を保つ（新しい connect は state・terminal_error を消すので、ここで見る失敗はこの generation のもの） |
| `plan/ws005/tests/host-wlan-retire.c` | case 4: key の削除が失敗する radio で EACCES の link loss → recovery の EIO の link loss の後も status が EACCES |

HAL・UAPI は変えていない。

## 検証

- host: `sh plan/ws005/tests/host-wlan-retire.sh` → `host-wlan-retire: PASS`（case 1〜4）。修正前の wlan.c（`REVERT=1 REVERT_COMMIT=HEAD`、
  コミット前に実行）で `after the recovery: state=8 terminal=5 (EACCES kept): FAIL`（case 4 が再現）。
- build: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p1-q651/ci build/p1-q651/ci/vmunix`（AX211 が有効）→ exit 0、warning 0、
  `amd64 vmunix check: PASS`。`make -j16 BUILD=build/p1-q651/amd64 build/p1-q651/amd64/bin/networkd` → exit 0、warning 0。
- style: `python3 plan/ws073/tests/style-diff.py`（変えた 3 つの source）→ findings on changed lines: 0。`git diff --check` 空。
- networkd の host 試験: 無い（main.c に host の試験の土台が無い）。読みで確かめた: 成功は今まで通り 0、失敗は retire の後に join の errno。自動の
  join の loop は `managed_wlan.connection.interface` で次へ進むか決めるので（errno ではない）、振る舞いは EACCES の記録が増えるだけ。
- QEMU: 出さない（AX211 は QEMU に無い。RTL8822BU の passthrough の試験は Venus の guest が要り、今は Venus の renderer が無い）。
- 実機（S2、5330 の AX211）: 未実施。手順は [BUG-157](../../bugs/BUG-157.md) の「S2 での確かめ方」。追加で見る log: networkd の
  `join failed: Permission denied; its cleanup is retried`（この修正の経路を通った印）。

## 残り

- S2 の実機で、間違えた鍵が「did not accept the key」（Settings）・`Could not join (Permission denied)`（system bar）になることを確かめる。
- 前の操作の recovery の最中に来た connect の admission の ENETDOWN（AX211 の ioctl の入口）は変えていない（その時は radio が実際に止まっている）。
