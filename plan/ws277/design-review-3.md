<!-- design-reviewer の差分の確認（2026-10-20、ws277 design.md 改訂 3）。Q1 が報告の本文をそのまま置いた -->

## WS277 p001 改訂 3 の差分の確認

対象は `/home/awe/zedBSD-claude1/plan/ws277/design.md` 版 3（507eb6f87）、`ws.md`、`phase001/phase.md` です。改訂 2 からの差分は `git diff 539dff06b 507eb6f87 -- plan/ws277` で見ました。

file の編集、build、試験、QEMU の起動はしていません。照合に使った物は次のとおりです。
- zedBSD: `display.c`・`render.c`・`render.h`・`gpu-mmu.c`、`gpu.c`、`gpu-display.h`、`panic.c`・`text-display.c/.h`、`serial-console.c/.h`、`visionfive2.c`、libvulkan の `context.c`・`wsi-display.c`・`wsi-image.c`、Keiland の `compose.c`・`output-switch.c`
- vendor: `vs_dc_hw.c`・`vs_dc.c`・`vs_crtc.c`
- mainline: scratchpad の `ml/` にある `vs_primary_plane.c`・`vs_bridge_regs.h`・`vs_crtc.c`・`vs_dc.c`

### 前回の指摘の対応の判定

| 指摘 | 判定 |
| --- | --- |
| 新 B1 | 直りました（§4.5）。保持の数、destroyed の印、thread の文脈での解放、二度目の present のどれも成り立ちます |
| 新 M1 | IRQ への繰り上げは良いです。ただし s0 + 2 の論証と費用に誤りがあります（下の M1） |
| 新 M2 | 直りました。ENXIO は SURFACE_LOST に写り（`wsi-display.c:1634-1635`）、失敗の時に libvulkan は前の front を保持し続けます（`:1463-1467`）。書き戻しと整合します。U6 の合格の条件と回復の前提は Minor 2・3 |
| 新 M3 | 直りました。vendor の手順との細かい差と、出典どうしの食い違いは Minor 1 |
| 新 M4 | 方針は良いです。ただし §3.2 の「今」の記述が main の現状と合いません（Minor 4） |
| 新 M5 | 直りました。`context.c:155-211`、`wsi-image.c:52-80` と一致します |
| 新 M6 | 「未定・p005 の前の追補」と明記され、それまでの規則もあります。要求どおりです |
| Minor 1 | path は直りました。R4 の wrapper は書かれた条件では作れません（下の M2） |
| Minor 2 | 描き直しは消えました。fatal の印に競合が残ります（Minor 5） |
| Minor 3 | 書かれました。順序に見た目の誤りがあります（Minor 6） |
| Minor 4〜6・8・10・11 | 直りました |
| Minor 7 | ws.md の依存節は直りました。p002 の依存に抜けがあります（下の M3） |
| Minor 9 | flip の記録の環は software の順序しか照合できません（下の M1 の (c)） |

### Blocker

ありません。

### Major

**M1. s0 + 2 の判定は、正しさの前提が書かれておらず、fps を refresh の半分に抑える**

根拠:
- 設計 §2.6:83 は「s0 + 2 で必ず latch 済み」とし、代価を「present の返りが最大 1 frame 遅れる」としています。
- vendor は flip の完了を次の vblank（s0 + 1）で送っています（`vs_crtc.c:325-337` の `drm_crtc_arm_vblank_event`、`vs_dc.c:1335-1338`）。
- libvulkan は PRESENT の ioctl の間 global の `display_mutex` を持つので、present は直列です（`wsi-display.c:1455-1489`）。

起こる事:
- **(a) 速さ**:
  - 前の present が IRQ k+2 で返り、直後に次の commit を書くと、s0 = k+2 です。latch は k+3 ですが、返りは k+4 です。
  - 定常では 2 frame に 1 回しか present できず、60 Hz で最大 30 fps です。「最大 1 frame の遅れ」ではなく、throughput が半分になります。
  - 今の copy の道（present は即時に返る）より遅くなり得ます。ws.md の完了の条件 2（fps）に直接響きます。
- **(b) 正しさ**: 次の 2 つが重なると、s0 + 2 の IRQ は latch の前に来ます。
  - IRQ が帰線の始めに出て、latch が帰線の終わりにある。
  - handler の遅れが帰線（約 0.67 ms）を超える。

  このとき commit は IRQ k と latch k の間、ただし latch k の後に書かれ、s0 = k−1 です。latch は k+1 ですが、IRQ k+1（= s0 + 2）はその前に来ます。そこで前の front を解放すると、B1 と同じ解放後の DMA になります。s0 + 2 が安全なのは「IRQ は latch と同じ点か、その後に出る」という前提の下だけです。設計はこの前提を書いておらず、§11 も s0 + 1 と s0 + 2 のどちらかの問いしか置いていません。

  s0 をどこで取るか（shadow を止めた (1) の前か、(4) の後か）も書かれていません。(1) の前に取ると、書く間に来た vblank を数えてしまいます。
- **(c) 試験**: §10 の flip の記録の環は「kernel が s0 + 2 を待った」事しか照合できません。HW の latch が s0 + 2 より遅い誤りは、どの試験でも検出できません。

直し方:
1. §2.6 に前提を明記します（IRQ の点は latch の点と同じか、その後）。s0 は手順 (4) の後に取ると定めます。
2. p002 で `CURRENT_LOCATION` の bit の並びを決め、p003 の IRQ handler で行の位置を記録して、IRQ が frame のどこで出るかを実機で確かめます（U の項目に足す）。前提が外れたら s0 + 3 にします。
3. D10 の代価を「直列の present では最大 refresh/2」に直します。p004 の範囲に「scanout の位置（`CURRENT_LOCATION`）か COMMIT の自己 clear で、commit の後の最初の vsync を判定し、60 fps に戻す」を入れます（今は「候補（未確認）」の扱い）。p007 の比較では、この制限の有無を記録します。

**M2. R4 の「serial-console の ops を包む wrapper、serial-console.c は変えない」は作れない**

根拠:
- `serial_console_ops` は static です（`src/drivers/generic/serial-console.c:59-70`）。
- 公開されているのは `drv_serial_console_init`（その中で `kern_text_register`）と `receive` だけです（`serial-console.h`）。
- `kern_text_*` は今登録されている ops へ渡すだけです（`text-display.c` の static な `ops()`）。wrapper から呼ぶと自分に戻ります。

起こる事: WS256 の担当は R4 を書かれたとおりには実装できません。serial の関数を visionfive2.c へ写すか、serial-console.c を変えるかを、その場で決めることになります。

直し方: R4 と §4.6 を次のどちらかに書き換えます。
- serial-console に汎用で追加だけの口（ops の表を返す関数など）を足すことを許す。層の向きは汎用の側への追加なので問題ありません。
- wrapper が serial の op を自前で持つ。

この選択は Q1 の判断（「serial-console は汎用のまま」）の言い換えの範囲です。

**M3. p002 の依存に R4（DC8200 の address・IRQ の組み込みの記述）が抜けている**

根拠:
- §9 の R4 の行は「p003（記述は p002 から）」です。D2 (a) により、発見は `visionfive2.c` の記述によります。
- 一方、§7 の p002 の依存と ws.md の依存節は「WS269 p007 の code」だけです。R4 の持ち主も未定です（§9）。

起こる事: p002 を Queue に入れた時点で、DC の窓を写す物がありません。

直し方: p002 の依存に「R4 の記述の部分（出力の範囲）」を足します。または、p002 だけ driver の中の既定の address で始め、R4 の記述が来たら置き換える、と明記します。

### Minor

1. **§2.6 の書く手順の細部**:
   - vendor は手順 (1) で `FRAMEBUFFER_CONFIG_EX` の bit 12 を **clear** し、(3) で set しています（`vs_dc_hw.c:1519-1547`）。設計の (3) は set だけです。bit 12 が自己 clear しない level の bit なら、edge が出ません。p002 の読み戻しの結果を待たずに、vendor と同じく (1) で clear します。
   - mainline は `PANEL_CONFIG_EX` の bit 0 を `VSDC_DISP_PANEL_CONFIG_EX_COMMIT` と名付け、set するだけです（`vs_bridge_regs.h:51-52`、`vs_bridge.c:172・237`）。vendor の「set で止める」と意味が食い違います。§11 に足してください。
2. **U6 の合格の条件**:
   - DC は timing の発生源です。HDMI を抜いても vblank は続くと推測します（PHY の PLL を止める操作は zedBSD にありません）。
   - そうなら「抜いたら ENXIO」は起きず、U6 は誤って FAIL になります。
   - 合格を「DEVICE_LOST と hang にならない。vblank が止まった時だけ ENXIO」とし、どちらが起きたかを記録する形にしてください。
3. **D11 の回復の前提**:
   - §2.6:90 は「libvulkan が swapchain を作り直すまで」としています。
   - Keiland は SURFACE_LOST で `output_lost` にし（`compose.c:4064-4068`）、回復を hotplug の fence に頼ります。window mode でない時や `register_device_event` が無い時は回復しません（`output-switch.c:93-95・148`）。
   - DC8200 の node には hotplug の通知がありません（Future Work）。よって ENXIO の後、desktop は止まったままになり得ます。この制限を D11 に書いてください。
4. **§3.2 の「今」が main と違う**:
   - e4d7edbd7（版 3 の commit より前）で `powervr_gpu_buffer_flags(mappable, shareable)` が入りました（`gpu-mmu.c:524-535`、`render.c:1040-1043`）。SHAREABLE の blob と storage の resource（`render.c:630`、mappable=1）は、既に作った側も迂回しています。
   - SLC を使う写像は「mappable でも shareable でもない blob」だけで、export はそれを拒みません（`render_export` `render.c:843-`）。
   - よって R3 は小さくできます。例えば `render_export` が image の export をその blob に限って拒めば、照会の口（R3 (2)）は要らなくなる案があります。
   - §3.2 の 4 の「試験の blob に限る内部の印」は、照会の口が無いと display から判別できません。中身を定めてください。
   - render.c の行番号（`:897`・`:981-985`・`:786-840`・`:688-691`）は古くなっています。
5. **fatal の印の競合**:
   - `panic.c:58-71` は他の hart を止めずに `reveal_fatal` を呼びます。
   - 他の hart が印を見た後に書き始めると、panic の戻しの後に render の buffer へ commit し直せます。
   - 書く区間（手順 (1)〜(4)）を、IRQ を禁じた短い spinlock の中に置きます。panic の側は上限付きで取りに行き、取れなくても書きます。区間の中で印を見直します。
6. **COPY・小さい BLOB の戻しの順**:
   - §4.2 は「plane を bootfb へ戻し、latch を待ってから copy」としています。この順では、古い bootfb の中身が 1〜2 frame 見えます。
   - zero copy の間 bootfb は scanout されていないので、先に bootfb へ copy してから plane を戻し、latch を待つ順にしてください。
7. **IRQ の数え方**:
   - `AQ_INTR_ACKNOWLEDGE` は両方の panel の共有です（vendor は全 panel を vblank として扱います、`vs_dc.c:1335-1338`）。
   - sequence は HDMI の panel の bit が立った時だけ進めると明記してください。
   - IRQ を常に有効にするか、claim の時からにするかも決めてください（有効にする前に s0 を取ると意味が崩れます）。
8. **細かい不整合**:
   - §2.6:88 の「それ以外の失敗の道は無い」と §4.6 の「fatal の印で ENXIO」が矛盾します。
   - §4.6 の「数 ms の polling は可」で何を読むかが書かれていません。ACK は読むと clear し、handler 専用です。
   - destroy を遅らせた resource の `owner` は `display_close` の `kern_free(session)` の後に無効になります（`display.c:350`）。参照しないと一言書いてください。
9. **p004 の UNGATED**: 「present は refresh の境を待たずに記録する」（`gpu-display.h:60-75`）と §4.3 の「flip 中は次の present が完了を待つ」がぶつかります。pending を置き換える場合、置き換えられた resource の保持の数の扱い（最大 3）も、p004 の設計で決める事として §4.3 に書いてください。
10. **D1 が no-go の時の Phase の順**: §7 の p005 の依存は「D1 が no-go なら p002」です。しかし p005 の後に p003（IRQ・present・保持の数）を行う順が書かれていません。

### 他 WS への依頼 R1〜R7

| 依頼 | 判定 |
| --- | --- |
| R1・R2・R5・R6・R7 | 正しい（R7 は `context.c` と一致） |
| R3 | 一部は main に既に入っている。小さくする案がある（Minor 4） |
| R4 | 書かれたとおりには作れない（M2）。p002 の依存にも要る（M3） |

R8 が要らないことは正しいです。

### 判断 D1〜D11

| 判断 | 評価 |
| --- | --- |
| D1〜D7・D9 | 妥当 |
| D8 (a) | 費用（SHAREABLE の全 buffer が SLC を使わない）は既に main で払われている（Minor 4） |
| D10 (a) | 選択は妥当。代価の記述が誤り。p004 で s0 + 1 相当に戻す事を範囲に入れる（M1） |
| D11 (a) | DEVICE_LOST を避ける点は妥当。U6 と Keiland の回復の制限を書き足す（Minor 2・3） |

### HAL

§8 の結論（hal.h の変更は無し）は保てます。IRQ は既存の kernel の API（`kern_irq_register`、`firmware-boot.c:757`）で足ります。

### 未確認（推測を含む）

- DC8200 の IRQ と latch の位置関係。
- bit 12 と `PANEL_CONFIG_EX` の bit 0 の真の意味（vendor と mainline で名前が違う）。
- HDMI を抜いた時に vblank が止まるか。
- Keiland が VF2 で window mode か。

### 結論

**p001 は今の版のままでは cleared にしません。** ただし Blocker はありません。M1〜M3 はどれも本文の局所の書き直しで直ります。
- M1: §2.6・D10・§7 の p004 の範囲・§10・§11。
- M2: §4.6・R4 の文言。
- M3: §7 の p002 の依存・ws.md。

設計の方向の変更は伴わないので、**Q1 が差分を見るだけで足りる局所の直し**と判断します。Minor 1〜10 は版 4 で直しても、該当する Phase の設計の時に直してもかまいません。ただし Minor 1・5・6 は p003 の実装の前に必要です。
