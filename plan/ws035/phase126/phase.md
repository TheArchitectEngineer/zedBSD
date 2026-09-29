<!-- awesome-plan project=zedbsd record=ws035p126 -->

# ws035-p126: login・Log Out の表示の引き継ぎの黒を無くす（Venus の driver が最後の画を保つ、F-048 の代案）

Phase ID: `ws035-p126`
Parent: [WS035](../ws.md)
Status: in-progress（2026-09-29、サブエージェント、worktree `wt/ws035`）
Phase disposition: normal
Queue: なし（2026-09-29 main の割り当て「F-048（login・Log Out の表示の引き継ぎの黒、約 1.1 秒）を無くす」。デモ（2026-10-17）に必須の仕上げ）
Future Work: [F-048](../../future-work.md)（行の更新は main に依頼する）

## 範囲

- login（greeter → session）と Log Out（session → greeter）の替わり目の黒（[p101](../phase101/phase.md) で約 1.1 秒）を、Venus（QEMU）で無くす。
- 人間の判断が要らない範囲だけ: Venus の driver（`src/drivers/gpu/venus/display.c`）の最小の変更。HAL（`include/hal/hal.h`）・UAPI
  （`include/drivers/gpu.h`・`include/uapi/`）・libvulkan・kernel の revoke は変えない。
- 確認: Linux の Venus の guest（graphical の login の image）で `zdesktop-p101.sh` の login・Log Out を撮り続け、黒の画の数と時間を前後で比べる。

## 設計の選択と理由

F-048 の案は 2 つ（[login-manager-design.md](../login-manager-design.md) §5）:

| 案 | 中身 | 要るもの | 判断 |
| --- | --- | --- | --- |
| A（本案） | lease を持った `/dev/gpu0` の fd を sessiond → greeter → session と渡す | libvulkan の「開いた fd と lease を使う」入口（`VK_KHR_display` の外の zedBSD 固有の入口、WS075・libvulkan の持ち主と相談）、渡した後の取り上げ（kernel の revoke、別 WS）。greeter の root でない uid と session の uid の間で同じ open file を共有するので、revoke が無いと greeter の残りが session の画面を描ける | 人間の判断が要る（§8-2 の revoke、libvulkan の API）。この Phase では扱わない |
| **B（代案、採る）** | Venus の driver が、lease の終わりに最後の画を scanout に保ち、次の lease の最初の frame で置き換える | driver の中だけ。i915 は [ws075-p016](../../ws075/phase016/phase.md) で同じ考え（release で最後の絵を次の lease の flip まで保つ）を実装済み | 判断が要らない。HAL・UAPI・libvulkan を変えない |

B で黒が消える理由: 今の黒は (1) release で driver が scanout を外し（`SET_SCANOUT` の resource 0）、(2) 起きた console の worker が隠れた console の
snapshot（p101 で黒）を描くことから来る。その後の取る側の Vulkan の device・swapchain の作成と最初の frame（p101 の内訳で約 0.5 秒 + α）の間、
画面は黒のまま。B では release で scanout を外さず、前の持ち主の最後の画（greeter の「Starting session...」、または desktop）が次の frame まで残る。

### 保つ条件（全てのとき）

1. 主の出力（identifier 1、scanout 0）。
2. 今の画が blob（`GPU_DISPLAY_PRESENT_BLOB`、`shared_front`）。blob の share は scanout の hold（`drv_venus_share_hold_locked`）で
   作った process の context と独立に生きる（share.c の既存の約束）ので、greeter が終わって context が消えても画は保てる。copied の画
   （private の front、持ち主の context で作った storage）は今までどおり外す。libvulkan の WSI は Venus で常に blob を出す。
3. kernel の log が quiet（`kern_log_quiet()`、`kmsg=quiet` の graphical boot）。隠れた console はどうせ黒なので、保っても console の文字を
   隠さない。quiet でない構成（kernel の開発、試験の lean な image）は今までどおり release で console の文字に戻る。
4. console の worker が居て止まりつつない（期限で保ちを終わらせる者が要る）、transport が壊れていない。

### 保ちの終わり

- **次の lease の最初の frame**: `display_blob_frame`・`display_frame` が新しい画を選んだ後に、保っていた share を放す（既存の「前の
  `shared_front` を放す」の経路そのまま）。保った時間を log（`venus: the next lease's first frame after holding the last picture for N ms`）。
- **期限**（`VENUS_DISPLAY_HOLD_TICKS` = 10 秒、i915 と同じ）: 次の lease が来ない（session が落ちて sessiond も greeter を起こさない等）なら、
  console の worker が期限に起き、scanout を外して share を放し、今までどおり console（quiet なら黒）を描く。
- **console の表示**（`kern_text_reveal`: panic・console の読み）: log が loud になり text の世代が進む → worker が起き、quiet でないので保ちを
  終えて console の文字を描く。
- **legacy の scanout 0**（`drv_venus_display_legacy_available_locked`）: 保ちを終えてから legacy に渡す。
- 次の lease が claim したが frame を出さずに release した: 保ちは続け、期限は延ばさない（最初の release の時刻から 10 秒）。
- controller の reset・detach: 今の持ち主の出力と同じ（reset が全ての scanout の参照を終える）。

## 実装

（実装後に記す）

## 検証

（実装後に記す）

## Resume point

2026-09-29: 設計を決めた（B）。worktree の `build/amd64` に sysroot と graphical の login の image を build 中（前の状態の計測用）。
次: 前の状態を `zdesktop-p101.sh` で計り、display.c を変えて build（warning 0）、同じ試験で後の状態を計る。
