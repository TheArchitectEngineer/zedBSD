<!-- awesome-plan project=zedbsd record=ws237 -->

# WS237: i915 の Gen9・Gen9.5（第 6〜10 世代 Core）の display controller の制御

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-14 起票・着手、P2）
Primary Milestone: MG006
Parent: [Master](../master.md)
Related: [WS229](../ws229/ws.md)（Gen9 の GT と Vulkan、display は GOP のまま）、[BUG-310](../bugs/BUG-310.md)（bootfb: GPU で描いて GOP の framebuffer へ CPU で写す、resolved）
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-14）

「SV7でKeilandが起動でき、利用できました。せっかくなので、ディスプレイコントローラの制御も実装してください。」

## 目標

- Gen9・Gen9.5 で i915 が display controller を自分で制御し、bootfb の CPU の写し（tearing・copy の費用）を無くす。
- 段: (1) firmware が点けた pipe・plane・panel の状態の readout と、同じ mode のままの primary plane の flip（vblank の割り込みで page flip、copy 無し）、(2) panel の backlight（明るさ）、(3) mode set（PLL・DDI・pipe・transcoder）と外部の DP・HDMI、(4) suspend・resume の時の display。
- ベータ2 の既定: 段 (1) が実機で通れば、bootfb の代わりに使う。通らなければ bootfb のまま（drop できる形）。
- source の手本: Linux の i915 は MIT（file ごとに確かめる）。zedBSD の Gen12 の display（resident display・takeover）の作りに合わせる。

## Phase（見積もりは [p001 の design.md](phase001/design.md) §5、±50 %）

| ID | 内容 | 状態 |
| --- | --- | --- |
| [ws237-p001](phase001/phase.md) | 設計: SKL・KBL・CFL の display の register と順（Gen12 の display の code との差）、readout、flip と vblank、backlight、試験の道（SV7 の passthrough と素の起動） | cleared の候補（2026-10-14 P2、Q1 の判定待ち） |
| [ws237-p002](phase002/phase.md) | readout と primary plane の flip（段 1）、1.5 LW | in-progress（P2）— 実装と host の確かめ済み（a5613f4b2）、T1（QEMU・SV7 の passthrough・5330）とユーザーの SV7 の素の起動の試験待ち |
| [ws237-p003](phase003/phase.md) | backlight、1.0 LW | in-progress（P2）— 実装と host の確かめ済み、実機は WS の最後に SV7 でまとめて |
| [ws237-p004](phase004/phase.md) | mode set と外部の display、6.0 LW | in-progress（P2）— register の順と計算（aa3250e12・cafce9894）、P3 の fixture（b9b6f0e66）、outputs と node と bootfb のつなぎ込み（ee0df7e11・55063dd4d）を実装、host 試験と build 済み。SV7 の確かめは WS の最後 |
| [ws237-p005](phase005/phase.md) | suspend・resume の display、1.5 LW（番号は 2026-10-14 Q1） | in-progress（P2）— ユーザーの判断 (B) D0 のまま sleep を実装、host と build 済み。SV7 は WS の最後 |
| ws237-p006 | 規約の全文、0.5 LW | planned |
- 2026-10-14 ユーザー「SV7の確認はせずに進めて、最後に確認してデバッグで調整しましょう。」→ p002 の実機の確かめを待たずに p003〜p005 を実装し、最後に SV7 でまとめて確かめて直す。
- 判断（2026-10-15 ユーザー、クリック）: Gen9 の sleep は「D0 のまま sleep（1 LW、危険小）」。PCI に driver が function を D0 のまま保つ選択を足し、Gen9 の i915 は D3hot に入らずに suspend、panel と backlight だけ消す（sleep 中の電池は悪い）。WS237 の外の src/drivers/pci/pci-power.c を変える（Q1 が許可）。

## SV7 でのまとめての確かめ（ユーザー、素の起動、段ごと）（P2 2026-10-15）

2026-10-14 のユーザーの指示「SV7の確認はせずに進めて、最後に確認してデバッグで調整しましょう。」の、最後の確かめの手順。段は boot の語で選ぶ。右の語ほど左の段を全部含む。
不具合が出たら、1 段ずつ左へ戻すと切り分けられる。既定は `copy`（今の BUG-310 の表示）のまま。

### 準備

1. WS237 を含む main の build の USB（release の候補の image）を用意する。
2. USB の小さい FAT の partition（`vmunix` と `zedbsd.cfg` の有る所）の `zedbsd.cfg` の最後に 1 行足して保存する。行は下の段ごとの表の語。
   `login=graphical` は足さない（重複で起動が止まる）。
3. SV7 で USB から起動し、desktop が出たら Terminal で次を流す。結果の行を写真か text で Q1 に送る。
   `dmesg | grep -e 'i915: gen9' -e 'i915: bootfb'`

### 段ごとの語・期待・見る行

| 語（`zedbsd.cfg` の行） | 期待 | 見る行（`dmesg` の） |
| --- | --- | --- |
| `i915.gen9.display=copy`（行を足さないのと同じ） | 今の表示（BUG-310）。速い動きで tearing がありうる | `i915: gen9 display: copy (...)`、`i915: bootfb: ... presentations are copied by the GPU and then the CPU` |
| `i915.gen9.display=flip` | 同じ解像度で、tearing が無い。window を速く動かしても横の裂け目が出ない | `i915: gen9 display: PWR_WELL_CTL2=... DC_STATE_EN=...`（readout）、`flip on pipe A, buffers at ...`、`presentations are flipped on the firmware's plane`。<br>出てはいけない行: `flip not used (...)`、`did not latch` |
| `i915.gen9.display=backlight` | flip に加え、Settings の明るさで panel が明るく・暗くなる。最低でも真っ黒にならない | `i915: gen9 backlight: ... PWM control ... (N %)`、`the panel's brightness is offered as a backlight device`。<br>`no backlight device` の行が出たら、その理由 |
| `i915.gen9.display=modeset` | backlight に加え、外部の DP・HDMI の monitor を挿すと、1 秒ほどで desktop に 2 つ目の画面として現れる（monitor の推奨の mode。CD clock を超える mode は使わない）。抜くと消える | `i915: gen9 node: external displays on ports B C D (connected 0x...), panel on pipe A, CD clock ... kHz, latencies ...`、<br>挿した時 `i915: gen9 outputs: port X: DP sink`（または `HDMI sink`）`, EDID 256 bytes, 1920x1080 at 148500 kHz`、`port X lit on pipe B, DPLL1, ...`、`lease ... of port X claimed`、<br>抜いた時 `port X's sink went; its display is dark and its lease ended`、`port X dark` |
| `i915.gen9.display=full` | 今は modeset と同じ | modeset と同じ |

### sleep（どの段でも）

1. 上の段の確かめの後、desktop の電源の menu から sleep する（または蓋を閉じる）。
2. 期待: 画面の明かりが消え、外部の monitor は信号が無くなる。
3. 電源ボタン（または蓋を開ける）で起こす。
4. 期待: 数秒以内に同じ画面に戻り、外部の monitor も前の画面に戻る。
5. 見る行: `i915: suspend: a Gen9 device sleeps in D0 ...`、`i915: gen9 backlight: the panel's light is off for the sleep`、`... is on again`、`i915: resume: the Gen9 display is back`。
   - 外部の monitor が戻らなければ `port X could not be lit again after the sleep: N`。
   - `suspend refused: ... cannot stay in D0` が出たら、sleep に入っていない。

### 不具合の時

- 画面が乱れる・黒い・固まる: 電源を切り、`zedbsd.cfg` の行を 1 段左の語に変えて起動し直す。どの段で崩れたかを Q1 に知らせる。
  - `copy` に戻すと今の表示に戻る。
- 外部の monitor が出ない: `dmesg` に次のどれがあるかを見る。
  - `no external displays (the panel is not flipped or its pixel clock was not read)`: panel の flip が使えていない。flip の段の行を見る。
  - `port X has a sink that could not be probed`: AUX・GMBUS の読みの失敗。
  - `port X could not be lit: N`: mode set の失敗。N を知らせる。8 = pipe・DPLL・buffer の空き無し（ENOSPC）、5 = DP の link training の失敗（EIO）。
  - `did not latch`: flip の失敗。
- 外部の monitor が出て panel が乱れる（underrun）: 2 つの pipe の watermark の分け方の問題の見込み。`latencies ...` の行を知らせる。

### 判定（Q1）

- 各段が通ったら、Q1 が `src/drivers/gpu/i915/display/gen9-display.h` の `I915_GEN9_DISPLAY_DEFAULT` を通った一番右の段に変える。
- 変えた後も `i915.gen9.display=copy` で今の表示に戻せる。
