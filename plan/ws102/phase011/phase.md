<!-- awesome-plan project=zedbsd record=ws102-p011 -->

# ws102-p011: L3 の数値目標への直し（遅れ・動き）

Parent: [WS102](../ws.md)
Status: in-progress — test-wait（2026-10-13 P2 q952: 基準値（T1-510）の読みで遅れの原因の見立てを立て、それを確かめる計測（waited_us）を足した。T1 の再計測の後に直す所を決める。直す所は WS102 の外（compositor の frame の完了の時期か libkeiui の present）になる見込みで、その時は Q1・ユーザーの判断）
Disposition: normal
Primary Milestone: MG006（WS から継承）
Queue / attempts: q913（P1）
依存: [ws102-p010](../phase010/phase.md) の基準値（T1）。

## 範囲

p010 の `latency` の段の基準値で目標（design.md §3 の L3 (a)(b)）を外れた所を直す。直す所は基準値を見て決める。読みで分かっている候補:

- (b) 開く動き: QEMU の Venus は 7 枚/秒前後（p008: frame の間隔 130〜143 ms）しか描けないので、20 ms は QEMU では満たせない。滑り（200 ms、
  時間で決まる）は frame が粗くても終わりの位置は正しい。合否は 5330 の実機（i915）か Windows の QEMU で `slide end` の行を見る（p008 の
  Q1 の判断と同じ扱い）。
- (a) 送出: 離しから送出までは compositor の中の処理だけ（key の event の作成と送り）。p95 が 5 ms を越えたら、送出の後の client への flush
  の時期（event loop の回りの最後でまとめて flush するか）を見る。
- (a) app の frame: Text Editor の再描画の時間に依る。50 ms を越えたら、compositor の frame の callback の時期（client の描画の開始）と
  Text Editor（libkeiui）の描画を分けて測る。

## 確認の予定

`osk-guest.sh latency` を直しの前後で（T1）、`osk-latency.py --check` で (a) が met。(b) は実機の値を記録。

## 2026-10-13 P2（q952）: 基準値の読みと原因の見立て

基準値（[p010](../phase010/phase.md)、T1-510、QEMU Venus）: send_us p95 1 ms（目標 5 ms、達）、**frame_us median 1 ms・p95 103 ms・max 150 ms**（目標 50 ms、未達）、slide の max_gap p95 166 ms（目標 20 ms、未達）。guest の時計は 1 ms 刻み。

### frame_us の尾の見立て（source の読み、未確かめ）

- 中央値 1 ms: 多くの key は Text Editor が受けてすぐ（1 ms 以内）次の buffer を commit している。Text Editor の描画は遅れの原因でない。
- 尾（約 5% 以上が 100〜150 ms）は QEMU の compositor の 1 コマ（85〜174 ms、p008・p010）と同じ大きさ。Text Editor（libkeiui）は Vulkan の swapchain を **`VK_PRESENT_MODE_FIFO_KHR`**（`libkeiland/ui/present.c:668`）で作り、libvulkan の Wayland の WSI は FIFO の present で**前の frame の frame callback を待ってから commit する**（`libvulkan/wsi-wayland.c:774-785` の `wayland_wait`）。compositor は frame callback を、その buffer を使った合成の frame が終わってから返す（`compose.c` の `compose_hold` → frame の完了で `kwl_callbacks_done`）。wl_shm の経路（`present-shm.c`）も 2 枚の buffer が両方 compositor に持たれていれば release を待つ。
- Text Editor は cursor を 530 ms ごとに点滅させて描き直し（`textedit/app.c` の `APP_BLINK_MS`、key で点滅の起点を戻す）、試験の tap は約 610 ms ごと（`qkey_tap`: move・150 ms・down・60 ms・up・400 ms）。key の離しが点滅の frame の直後（その合成が終わる前）に来ると、key の frame は点滅の frame の callback（QEMU で最大 1 コマ）を待つ。これが尾の見立て。実機（5330 の i915、1 コマ約 16 ms）なら同じ待ちは 1 コマ以内で、50 ms に収まる見込み（未測定）。
- slide の max_gap（b）は p011 の範囲の記述どおり QEMU の compositor の 1 コマそのもので、QEMU では 20 ms にならない（実機で測る）。

### 足した計測（直しの前に見立てを確かめる）

| 所 | 変更 |
| --- | --- |
| `wayland/objects.c` | `kwl_callbacks_done`（frame callback の done）と `kwl_buffer_put`（`wl_buffer.release`）で、client に知らせた後に `kwl_keyboard_client_told(client)` を呼ぶ |
| `wayland/keyboard.c`・`kwl.h` | `kwl_keyboard_client_told`: key の frame を待っている client なら、離しの後に知らせた最後の時刻を持つ。`KWL OSK latency frame_us=N waited_us=W`（W は離しから最後の知らせまで、0 は離しの後に知らせが無い = 待っていない） |
| `plan/ws102/tests/osk-latency.py` | `waited_us` を読み、frame_us を「待った物」「待たない物」に分け、waited_us と「待ちの終わりから commit まで」も出す（旧い log の行も読める） |

見立てが正しければ、尾の key は waited_us が frame_us に近く、「待ちの終わりから commit まで」が 1〜2 ms、待たない物の p95 は数 ms になる。

| コマンド | 結果 |
| --- | --- |
| `make BUILD=build/ws102-p011 ZEDBSD_CONFIG=config/current-uat.mk -j16 build/ws102-p011/bin/wayland` | rc 0、warning 0 |
| `make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/ws102-p011-linux -j16 all` | rc 0、warning 0 |
| `osk-latency.py` に手で書いた log（waited_us の有る行・無い行・旧い形の行） | 分けた要約が期待どおり |
| `python3 plan/tools/style-check.py userland/desktop/wayland/keyboard.c userland/desktop/wayland/objects.c` | 変えた所の指摘 0（既存の 4 件のみ） |

### 直しの候補（T1 の結果の後、どれも WS102 の外なので Q1・ユーザーの判断）

- (1) compositor が frame callback を返す時期を、合成の frame の完了（GPU の終わり）から、その buffer を合成に取り込んだ時（sample の時）へ早める。client は compositor の GPU の作業と並んで次の frame を描ける。全 client の frame の速さに効く。compositor の frame の枠組み（WS210 p013 の側）と重なる。
- (2) libkeiui の swapchain を MAILBOX（libvulkan の WSI は持つ、`wsi-wayland.c:401`）にする: present が前の frame を待たず、未表示の frame を置き換える。待ちは消えるが、frame の速さの上限が無くなり、連続で描く app は `KL_PACING_FRAME`（WS210 p014）に頼る。全 app に効く。
- (3) 何もしない: QEMU の値は参考（p010 と WS094 の扱い）として、5330 の実機で `latency` を測って (a)(b) を判定する。
