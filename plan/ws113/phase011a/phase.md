<!-- awesome-plan project=zedbsd record=ws113-p011a -->

# ws113-p011a: i915 の 1 出力の付け替え（Keiland の指示で点ける出力を替える）

Parent: [WS113](../ws.md)
Status: planned（2026-10-07 q850、P2: 設計の案。実装は Q1 の合図の後、i915 の display の file は P1 の WS051 p004a と重なるので着手の前に Q1 へ）
Disposition: normal
Primary Milestone: MG006（WS から継承）
Queue: q850（p004a の後）
目安: 3〜5 h（実機 5330 の HDMI が中心）

## 目的

2026-10-07 ユーザーの N8（[ws052-p007](../../ws052/phase007/phase.md) §11）: 蓋を閉じたら外部の画面だけで使い続け、開けたら内蔵へ戻す。i915 は今 GOP の出力（resident）だけを点け、他の出力の claim を `ENOSPC` で断る（p002）。同時に 2 つを点ける p011 より小さく、**点ける 1 つの出力を Keiland の明示の指示で付け替える**。compositor の側は [p004a](../phase004/phase.md)。

## 今の事実（main 61004284b を読んだ）

- resident の output は起動時に 1 度だけ選ぶ（`display/output.c` の `i915_output_choose`: GOP が eDP なら eDP、HDMI（DDI B）なら HDMI を pipe B・DVI で、他の interface は none）。`display->output.hdmi` と `display->output.state`（HDMI の mode・PLL）が resident の run の入力。
- 点けるのは最初の present の時（`present.c`: worker が display の window に入り `drv_i915_lcd_kernel_resident_run` で modeset）。release の後は最後の絵を `I915_PRESENT_HOLD_MS`（10 秒）保ち、次の lease が無ければ window を出て stop の道で消す。sleep の後に HDMI が点かなければ eDP に替えてもう 1 度 run する道が既に在る（`present.c:591-595`、ws052-p009）。つまり window の外（止まっている時）なら、`display->output` を替えて次の run で別の出力を点けられる。
- 表示の ID: resident は常に `I915_DISPLAY_ID`（1）・generation 1（`display.c:2735` 付近・`present.c` の `I915_PRESENT_DISPLAY_ID`）、他の connector は `I915_DISPLAY_OTHER_ID + connector`。名前（A2 の key）は resident が eDP なら `…:edp:A`、HDMI なら `…:hdmi:B`。
- libvulkan は display を (device, display_id) で覚え、名前は最初の列挙の値のまま（`wsi.c` の `wsi_display_get`）。resident の ID が出力に依らず 1 なら、付け替えの後に同じ VkDisplayKHR が別の connector を指し名前も古いまま → **ID を connector ごとに固定する必要がある**。

## 設計

1. **ID は connector ごと**: hotplug の道に connector がある時、resident も `I915_DISPLAY_OTHER_ID + connector` の ID で出す（今の 1 は hotplug の道が無い時だけ）。generation は connector ごとの値にし、付け替えで両方を 1 進める（旧 generation の mode・surface を libvulkan が断る）。`present.c`・`control.c`（power・refresh）の ID の照合も resident の今の ID に合わせる。
2. **claim**（`i915_display_claim`）: 対象が resident でない connector の時、
   - lease が在る（`rd->owner != NULL`）→ `ENOSPC`（今と同じ。同時の 2 つは p011）。
   - 切断・generation 違い → `ENXIO`・`ESTALE`。
   - この driver が点けられない interface（DP・USB-C の DP-alt は WS051 の後）→ `EOPNOTSUPP`。
   - 点けられる（eDP の DDI A、HDMI の DDI B）→ 付け替えの item を worker に出して待つ: window の中（hold の最中）なら hold を終えて window を出る（stop の道で今の出力を消す。eDP は panel の電源と backlight も落ちる）→ `display->output` を対象に（HDMI は `i915_output_hdmi` で EDID から mode と PLL を選び直す、eDP は `output.hdmi = 0`）→ generation を進め topology の sequence を 1 進める（ACTIVE が移るので、hotplug の fence を持つ client は数え直す）→ lease を渡す。点けるのは今と同じく最初の present。
   - 付け替えの失敗（HDMI の EDID が読めない、mode が無い）→ 元の resident に戻して `EIO`（libvulkan は SURFACE_LOST、compositor は元の出力へ戻る）。
3. **release と戻し**: release は今のまま（10 秒の hold）。GOP の出力でない resident の hold が lease 無しで終わったら（compositor が終わった・切り替えに失敗した）、resident を GOP の出力に戻す（点けない。次の claim・present で点く）。D-RELEASE の「GOP の出力先へ戻る」を保つ。
4. **切断**: resident の connector が抜かれたら、今の lease の present を `ENXIO` にし（libvulkan は SURFACE_LOST）、window を出て、resident を GOP の出力に戻す。compositor（p004a）は残る出力へ切り替える。
5. **起動時**: 今のまま（GOP の出力）。

## 試験

- host: ID の対応（resident と other の ID が connector に固定、付け替えで generation が進む）と claim の判断の表（lease の有無・interface・切断・generation）を `plan/ws113/tests/` の host 試験で（display.c の判断を純粋な関数に出す）。
- 実機（5330、eDP と HDMI、`flock /tmp/i915-hw.lock`）: native の probe（`userland/tests/display-control` か新しい小さな probe）で、eDP の lease を release → HDMI を claim・present → HDMI に絵・eDP は消灯 → release して 10 秒 → GOP（eDP）に戻る。p004a の compositor の切り替え（Keiland の画面が HDMI に移り、戻る）は ws052-p012 の UAT にまとめる。
- build warning 0（vmunix の kernel include check まで）、規約。

## 依存と衝突

依存: p002（inventory・HPD、in-progress・T1 待ち）。衝突: P1 の WS051 p004a（`takeover.c`・`output.c`・`display.c` の DP-alt）。実装の前に Q1 へ知らせ、同じ file は小さく commit して早く merge を頼む。
