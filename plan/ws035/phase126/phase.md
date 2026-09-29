<!-- awesome-plan project=zedbsd record=ws035p126 -->

# ws035-p126: login・Log Out の表示の引き継ぎの黒を無くす（Venus の driver が最後の画を保つ、F-048 の代案）

Phase ID: `ws035-p126`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-29、サブエージェント、worktree `wt/ws035`。QEMU の Venus、実機は未実施）
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

## 実装（2026-09-29）

`src/drivers/gpu/venus/display.c` だけ（HAL・UAPI・libvulkan・userland は変えていない）:

- engine に保ちの状態（`holding`・`hold_since`・`hold_until`、controller の mutex の下）と `VENUS_DISPLAY_HOLD_TICKS`（10 秒）。
- `display_release_output()`: `display_hold_permitted()`（上の 4 条件）が真なら、`SET_SCANOUT` の resource 0 を送らず、`shared_front`（share の
  hold）を持ち主の無い主の出力に残し、`display_hold_begin()` で期限を始める（既に保っているなら期限を延ばさない）。private の front・back は
  scanout されていないので今までどおり放す。保たないときは主の出力の保ちを終える（`holding = 0`）。
- `display_blob_frame()`・`display_frame()`: 新しい画を選んで前の `shared_front` を放した後に `display_hold_replaced()`（保ちを終え、保った ms を log）。
- `display_console_update()`: 持ち主が無く保っている間は、quiet かつ期限の前なら何も描かない。そうでなければ `display_hold_end()`
  （scanout を外し share を放す）の後に今までどおり console を描く。worker は保っている間だけ `hold_until` を期限に眠る。
- `drv_venus_display_legacy_available_locked()`: legacy の scanout 0 の前に `display_hold_end()`。
- kernel の log（ring、quiet なら画面に出ない）: `venus: lease released; holding the last picture`・`venus: the next lease's first frame after
  holding the last picture for N ms`・`venus: the held picture was withdrawn for the console or a legacy scanout`。

試験の道具 `plan/ws035/tests/zdesktop-p126.sh`（新）: graphical の login の image（boot で kei を自動 login）で、App Home の Log Out →
greeter → kei の password と Enter の login を CYCLES 回、各替わり目を `frames.py` で撮り続け、黒の画の数と黒の時間（最初の黒から次の黒で
ない画まで、画は約 0.1〜0.2 秒ごと）を出す。文字 console の画・替わり目の手順の欠けは FAIL、`--no-black` なら黒も FAIL。dmesg の保ちの行を
`OUTDIR/dmesg-hold.txt` に。p101 の `zdesktop-p101.sh` は root の login の前提（今の image は kei の自動 login と password）で古い。

## 検証（amd64、Linux host の Venus の guest（`zdesktop-guest.sh`、1280x800、`GUEST_RUNTIME=build/ws035-run`）、graphical の login の image、2026-09-29）

**前**（`build/p126-before.img`、変更前の同じ tree）: `zdesktop-p126.sh build/p126-before 3`（手順は全て ok）

| 替わり目 | 1 | 2 | 3 |
| --- | --- | --- | --- |
| Log Out（session → greeter）の黒 | 8 枚・1409 ms | 13 枚・1368 ms | 7 枚・1422 ms |
| login（greeter → session）の黒 | 9 枚・1500 ms | 10 枚・1748 ms | 8 枚・1395 ms |

**後**（`build/p126-final.img`、最終の source）: `zdesktop-p126.sh build/p126-final 3 --no-black` **PASS**: 6 回の替わり目の全てで
**黒 0 枚・文字 console 0 枚**（Log Out 85・80・78 枚、login 78・78・76 枚）。dmesg: 6 回とも `lease released; holding the last picture` →
`the next lease's first frame after holding the last picture for` 1261〜1379 ms（この間、画面は前の持ち主の最後の画: login は greeter の
「Starting session...」、Log Out は desktop）。同じ試験を途中の build（`build/p126-after.img`、comment と log の文言の前）でも 3 周 PASS
（保った時間 1159〜1602 ms）。

保ちの終わりの他の経路（`build/p126-after.img`）:

- **期限**: session の中で `service stop greeter`（sessiond と session の compositor が終わり、次の lease は来ない）→ 最後の画が約 10 秒残り、
  `the held picture was withdrawn` の後に黒（隠れた console の snapshot、1920x1072）。その後 `service start greeter` で自動 login の session が
  普通に画を出す（`zdesktop-check.py` PASS、`p126-20260929-restart-after-hold-end.png`）。
- **console の表示**（reveal）: `service stop greeter` の後、期限の前に `/dev/console` を読む → 期限より前（stop の後 10 秒以内）に文字 console
  （`kern_text_reveal` → log が loud → worker が保ちを終える）。
- **loud の console**（reveal の後）: 次の Log Out・login は保たない（dmesg に新しい `holding` が無い）。替わり目に文字 console が 7 枚ずつ出る
  （p126 の前の、loud の構成での今までの動き）。

- faults: `build/p126-after.img` の 3 周の後の dmesg に `venus: shared allocation ... retained`・transport の失敗・`killed by signal` は無い
  （`build/p126-final.img` の run では dmesg の保ちの行だけを見た）。前の image の最初の guest の起動で 1 度 `kern: pid 135 killed by signal 11
  (vector 14) at 0xaf0bc, address 0x18` を見た（この変更の前の image、何の process かは未調査。範囲外として main へ）。
- build: `plan/ws035/tests/build-login-image.sh build/amd64 graphical`（worktree の `build/amd64`、sysroot は `sysroot-amd64` で同じ dir に作った）、
  kernel は `-Werror` で warning 0。
- boot test: `plan/tools/boot-test.sh build/p126-after.img`（GPU の無い q35、sessiond は表示が無く console の login）PASS
  （`build/ws035-shots/p126-20260929-boot-test.png`）。
- 規約: `plan/tools/style-check.py src/drivers/gpu/venus/display.c` の指摘は HEAD の前と同じ 27 件（全て既存の形、新しい指摘 0）。`git diff --check` 清浄。
- 画面（`build/ws035-shots/`）: `p126-20260929-login-held-greeter.png`（login の替わり目、保った greeter の「Starting session...」）、
  `-login-desktop.png`（次の frame）、`-logout-held-desktop.png`（Log Out の替わり目、保った desktop）、`-logout-greeter.png`、前の黒
  `-before-login-black.png`。

未実施: 実機（i915 は ws075-p016 の別の実装、この変更は Venus だけ）。demo の image（`config-amd64-demo-venus.mk`、1920x1280）の demo-walk
（同じ driver の経路で、image の build の時間のため省いた）。host の単体試験（Venus の display の host 試験は無い）。

## 制限と残り

- **loud の console では保たない**: `ZEDBSD_BOOT_KERNEL_MESSAGES=y`（2026-09-29 のユーザーの決定の「起動の kernel の message を画面に出す」）の
  graphical boot、または reveal の後は、替わり目に今までどおり文字 console が約 1.4 秒出る。そこで保つと、手で起こした zdesktop を終えた後に
  shell の文字が最大 10 秒見えなくなる。loud でも保つか（例: 保つ間の text の変化で終える）は人間の判断（main・ユーザーへ）。
- F-048 の本案（lease を持った fd の受け渡しと kernel の revoke）は未着手のまま。黒は無くなったが、替わり目の約 1.3 秒は前の持ち主の画の
  まま止まって見える（入力は効かない）。
- `zdesktop-p101.sh` は今の image（kei の自動 login、password）に合わない。p126 の試験が同じ替わり目を覆う。

## Resume point

2026-09-29: cleared。次は ws.md の残り（デモの通しの不具合）から人間の判断が要らないもの。F-048 の行の更新（Venus は p126 で黒を無くした、
本案と loud の console は残り）は main に依頼。
