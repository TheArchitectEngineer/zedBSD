<!-- awesome-plan project=zedbsd record=ws237 -->

# WS237: i915 の Gen9・Gen9.5（第 6〜10 世代 Core）の display controller の制御

<!-- awesome-plan-current:start -->
Status: completed（2026-10-15 ユーザー「Gen9は残りの安全策の作業をマージしたら終了です。UATは不要です。」、安全化 P3 06dbfa855 → main 63d0532b0）
<!-- retained-after-completion --> Phase の directory・試験は他の WS・plan/tools から参照されているので残す（2026-10-18 Q1、ユーザーの承認の案 (a)）。参照する試験を変える時に 2026-10-06 の基準で plan/tools へ移すか消す。
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

## Phase（見積もりは [design.md](design.md) §5、±50 %）

| ID | 内容 | 状態 |
| --- | --- | --- |
| [ws237-p001（Phase の directory は完了で削除、git の履歴） | 設計: SKL・KBL・CFL の display の register と順（Gen12 の display の code との差）、readout、flip と vblank、backlight、試験の道（SV7 の passthrough と素の起動） | cleared の候補（2026-10-14 P2、Q1 の判定待ち） |
| [ws237-p002（Phase の directory は完了で削除、git の履歴） | readout と primary plane の flip（段 1）、1.5 LW | cleared（2026-10-15 ユーザーの実機: i5 の第 7・第 8 世代で display controller OK・Keiland 使用可） |
| [ws237-p003（Phase の directory は完了で削除、git の履歴） | backlight、1.0 LW | in-progress（P2）— 実装と host の確かめ済み、実機は WS の最後に SV7 でまとめて |
| [ws237-p004（Phase の directory は完了で削除、git の履歴） | mode set と外部の display、6.0 LW | in-progress（P2）— register の順と計算（aa3250e12・cafce9894）、P3 の fixture（b9b6f0e66）、outputs と node と bootfb のつなぎ込み（ee0df7e11・55063dd4d）を実装、host 試験と build 済み。SV7 の確かめは WS の最後 |
| [ws237-p005（Phase の directory は完了で削除、git の履歴） | suspend・resume の display、1.5 LW（番号は 2026-10-14 Q1） | in-progress（P2）— ユーザーの判断 (B) D0 のまま sleep を実装、host と build 済み。SV7 は WS の最後 |
| ws237-p006 | 規約の全文、0.5 LW | 行わない（ユーザーの決定で WS は安全化の merge で終了） |
- 2026-10-14 ユーザー「SV7の確認はせずに進めて、最後に確認してデバッグで調整しましょう。」→ p002 の実機の確かめを待たずに p003〜p005 を実装し、最後に SV7 でまとめて確かめて直す。
- 判断（2026-10-15 ユーザー、クリック）: Gen9 の sleep は「D0 のまま sleep（1 LW、危険小）」。PCI に driver が function を D0 のまま保つ選択を足し、Gen9 の i915 は D3hot に入らずに suspend、panel と backlight だけ消す（sleep 中の電池は悪い）。WS237 の外の src/drivers/pci/pci-power.c を変える（Q1 が許可）。

## SV7 でのまとめての確かめ（ユーザー、素の起動、段ごと）（P2 2026-10-15）

2026-10-14 のユーザーの指示「SV7の確認はせずに進めて、最後に確認してデバッグで調整しましょう。」の、最後の確かめの手順。段は boot の語で選ぶ。右の語ほど左の段を全部含む。
不具合が出たら、1 段ずつ左へ戻すと切り分けられる。既定は `full`（2026-10-15、647d43d37）。`copy` は今までの BUG-310 の表示。

### 準備

1. WS237 を含む main の build の USB（release の候補の image）を用意する。
2. USB の小さい FAT の partition（`vmunix` と `zedbsd.cfg` の有る所）の `zedbsd.cfg` の最後に 1 行足して保存する。行は下の段ごとの表の語。
   `login=graphical` は足さない（重複で起動が止まる）。
3. SV7 で USB から起動し、desktop が出たら Terminal で次を流す。結果の行を写真か text で Q1 に送る。
   `dmesg | grep -e 'i915: gen9' -e 'i915: bootfb'`

### 段ごとの語・期待・見る行

| 語（`zedbsd.cfg` の行） | 期待 | 見る行（`dmesg` の） |
| --- | --- | --- |
| `i915.gen9.display=copy` | 今の表示（BUG-310）。速い動きで tearing がありうる | `i915: gen9 display: using copy (asked for copy by i915.gen9.display=)`、`i915: bootfb: ... presentations are copied by the GPU and then the CPU` |
| `i915.gen9.display=flip` | 同じ解像度で、tearing が無い。window を速く動かしても横の裂け目が出ない | `i915: gen9 display: PWR_WELL_CTL2=... DC_STATE_EN=...`（readout）、`flip on pipe A, buffers at ...`、`presentations are flipped on the firmware's plane`。<br>出てはいけない行: `flip failed (...); using copy`、`did not latch` |
| `i915.gen9.display=backlight` | flip に加え、Settings の明るさで panel が明るく・暗くなる。最低でも真っ黒にならない | `i915: gen9 backlight: ... PWM control ... (N %)`、`the panel's brightness is offered as a backlight device`。<br>`no backlight device` の行が出たら、その理由 |
| `i915.gen9.display=modeset` | backlight に加え、外部の DP・HDMI の monitor を挿すと、1 秒ほどで desktop に 2 つ目の画面として現れる（monitor の推奨の mode。CD clock を超える mode は使わない）。抜くと消える | `i915: gen9 node: external displays on ports B C D (connected 0x...), panel on pipe A, CD clock ... kHz, latencies ...`、<br>挿した時 `i915: gen9 outputs: port X: DP sink`（または `HDMI sink`）`, EDID 256 bytes, 1920x1080 at 148500 kHz`、`port X lit on pipe B, DPLL1, ...`、`lease ... of port X claimed`、<br>抜いた時 `port X's sink went; its display is dark and its lease ended`、`port X dark` |
| `i915.gen9.display=full`（行を足さないのと同じ。既定、2026-10-15） | 今は modeset と同じ | modeset と同じ。`i915: gen9 display: using full (asked for full by the default)` |

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
  - `modeset failed (the panel is not flipped or its pixel clock was not read); using backlight`: panel の flip が使えていない。flip の段の行を見る。
  - `modeset failed (an external display could not be lit: N)` か `(an external display's flip did not latch: 60)` と `the external displays are withdrawn`: 外部の display の mode set か flip が失敗し、その boot の間は外部の display を出さない（panel はそのまま）。
  - `port X has a sink that could not be probed`: AUX・GMBUS の読みの失敗。
  - `port X could not be lit: N`: mode set の失敗。N を知らせる。8 = pipe・DPLL・buffer の空き無し（ENOSPC）、5 = DP の link training の失敗（EIO）。
  - `did not latch`: flip の失敗。
- 外部の monitor が出て panel が乱れる（underrun）: 2 つの pipe の watermark の分け方の問題の見込み。`latencies ...` の行を知らせる。

### 判定（Q1）

- 各段が通ったら、Q1 が `src/drivers/gpu/i915/display/gen9-display.h` の `I915_GEN9_DISPLAY_DEFAULT` を通った一番右の段に変える。
- 変えた後も `i915.gen9.display=copy` で今の表示に戻せる。

## 引き継ぎ（2026-10-15 夜、Q1、サーバの再起動の前）

- 2026-10-15 ユーザー: Gen9・Gen9.5 の display の制御を既定で on → `I915_GEN9_DISPLAY_DEFAULT` を FULL（main、647d43d37）。実機（SV7）では copy より上の段が未確認。
- P2 の作業中の patch（plan/agents/p2/wrapup-20261015.patch、base 73476664c）: 上の段が失敗した時に下の段、最後は copy に戻す安全化。未完。リリースまでに入れるか、既定を下げるかの判断が要る。
- SV7 で起動していたのは古い image（8e2115b）。main の image で確かめる（ユーザーの試験の一覧 U1）。

## ユーザーの実機の結果（2026-10-15）

- ユーザー（原文）:「i5 7000系, i5 8000系、ともにi915のディスプレイコントローラOK,Keiland使用できました。記録と計画の更新をお願いします。」
- 第 7 世代（Kaby Lake、Gen9.5）と第 8 世代（Gen9.5）の Core i5 の実機で、i915 の display controller の制御で Keiland が使えた。段の指定は報告に無いので、main の既定（`I915_GEN9_DISPLAY_DEFAULT` = FULL、647d43d37）の image と見る。
- 判定（Q1）: p002（readout と flip）は cleared。p003（backlight の明るさ）・p004（外部の DP・HDMI）・p005（sleep）は個別の確かめの報告が無いので in-progress のまま、ユーザーの試験の一覧（plan/beta2.md の U5）に載せた。既定の FULL は保つ。
- P2 の安全化の patch（失敗時に下の段・最後は copy）は、既定 FULL が 2 機種で通ったので急がない。p003〜p005 の実機で崩れた時に再開する（patch は plan/agents/p2/wrapup-20261015.patch、P2 の worktree の stash `ws237-wrapup-20261015`）。
- 残り: p003〜p005 の実機、p006（規約の全文）。第 6 世代（Skylake）・第 9・10 世代は未確認。
- 2026-10-15 ユーザーの決定（原文）:「Gen9は残りの安全策の作業をマージしたら終了です。UATは不要です。」→ P3 の安全化（上の段が失敗したら下の段、最後は copy）を merge したら WS237 を completed にする。p003〜p005 の実機の確かめ（U5）は不要（beta2.md の U5 を外した）。

## 既定 full の安全化（2026-10-15 P3、ユーザー「はい、ではそれを安全にマージして下さい。」）

P2 の途中の patch（plan/agents/p2/wrapup-20261015.patch）を完成させた。patch の中の bootfb.c は空の file（書き換えの途中で止まった跡）だったので、bootfb.c は main から戻して結線を書き直した。

- 段の上限: `drv_i915_gen9_display_step()` = 起動の行（無ければ既定 full）の段を、失敗した段の一つ下で抑えた物。上限は下がるだけで、その boot の間は戻らない（`drv_i915_gen9_display_fail()`、compare and exchange で同時の失敗でも上がらない）。失敗した時は 1 行 `i915: gen9 display: <段> failed (<理由>[: <error>]); using <段>`、bind の最後に 1 行 `i915: gen9 display: using <段> (asked for <段> by i915.gen9.display= | the default)`。
- flip の失敗 → copy: 始め（readout の不一致、display window・buffer が取れない）、実行中（flip が 100 ms で latch しない、plane の fault 割り込み（新、bit 7）、buffer を session の空間に map できない）、lease の終わりに firmware の surface が戻らない。実行中の失敗は、その frame を copy で出し、以後 copy（bootfb.c）。backlight の device と外部の display は flip が始めに失敗すると出さない（copy = WS237 の前の表示）。
- mode set の失敗 → backlight: 始め（panel の pixel clock・CD clock が読めない、window に外部の buffer の空きが無い（panel だけの window で flip は続ける））、実行中（外部の display の mode set の失敗（DP の link training・transcoder）、外部の flip が latch しない、sleep の後に点け直せない）。全部の外部の display を消して lease を終え、sink 無しとして見せ（`the external displays are withdrawn`）、hotplug の work は port を見なくなる。session には hotplug の work が 1 秒以内に events の sequence で知らせる。panel の flip が実行中に失敗した時も、hotplug の work が同じく外部の display を下げる。pipe・DPLL・DDB の不足（ENOSPC）は失敗ではない（その claim だけ断る）。
- backlight: panel に PWM が無い（desktop など）のは失敗ではない。一度出した backlight の device は残す（plane に触らない）。
- 成功の経路（今 2 機種で動く full）: register の書き込みの順と値は変わらない。変わるのは plane の fault の割り込みを flip の pipe で有効にしたこと（Linux の i915 も Gen9 で有効にする。正常な機械では来ない）と、log の行（`copy (...)`・`flip not used`・`no external displays` の行が上の 2 種の行に変わった）。
- sleep の後: `drv_i915_gen9_flip_resume()` が panel の power well・transcoder・plane を読んで 1 行 log に出す（書き込み無し）。
- 確かめ: amd64 の vmunix の build（config/ci/config-amd64.mk、warning 0）。host 試験 gen9 flip 48・backlight 21・mode 71・modeset 80・outputs 29・boot 15 の各 check が 0 failures、新しい [run-host-gen9-display-step.sh](tests/run-host-gen9-display-step.sh)（段の上限、22 checks、2 thread の同時の失敗 200 回を含む。上限を上げてしまう変異で 8 件 FAIL になることを確かめた）。flip・mode set の実行中の失敗の道（bootfb・gen9-node の結線）は host で動かせず、読みのレビューだけ。実機・QEMU は未実施。

## 結果（2026-10-15、completed）

- 第 7・第 8 世代の Core i5 の実機で、i915 が display controller を制御し Keiland が使えた（ユーザー、既定 full）。p001・p002 cleared。
- 既定 full の安全化（P3、06dbfa855、main 63d0532b0）: 段の上限を失敗した段の一つ下へ下げるだけ（atomic）。flip の失敗は copy、mode set の失敗は backlight（外部の display を全部引っ込め、panel は flip のまま）。失敗は `i915: gen9 display: <段> failed (<理由>); using <段>`、bind の後に `using <段> (asked for …)` の 1 行。成功の経路の register の書き込みは不変（plane fault の割り込みの有効化と log の行だけ違う）。build（warning 0）と host 試験（flip 48・backlight 21・mode 71・modeset 80・outputs 29・boot 15・step 22）PASS。実行中の失敗の道は host で動かせずレビューだけ。
- 制限・未確認（ユーザーの決定で UAT は不要）: backlight（p003）・外部の DP・HDMI（p004）・sleep の display（p005）は実機で未確認。外部の display の mode set が 1 回失敗すると、その boot の間は外部を出さない（再起動まで）。第 6 世代（Skylake）・第 9・10 世代は未確認。規約の全文の見直し（p006）は行っていない。
- 残す物: [design.md](design.md)、host 試験 [tests/](tests/)（master の Tools に登録）。Phase の directory は削除（git の履歴）。
