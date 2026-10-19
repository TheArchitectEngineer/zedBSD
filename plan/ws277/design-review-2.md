<!-- design-reviewer の差分の確認（2026-10-20、ws277 design.md 改訂 2）。Q1 が報告の本文をそのまま置いた -->

## WS277 p001 改訂 2 の差分の確認

対象: `/home/awe/zedBSD-claude1/plan/ws277/design.md` 版 2（539dff06b、`git diff b9bfd86fb 539dff06b -- plan/ws277`）、`plan/ws277/ws.md`、`plan/ws277/phase001/phase.md`。

file は編集していません。build・試験・QEMU も走らせていません。

照合した物:
- zedBSD: `display.c`・`render.c`・`render.h`、`src/drivers/gpu/gpu.c`、`include/drivers/gpu/gpu-display.h`、`text-display.h`・`text-display.c`・`panic.c`・`user-probe.c`、`src/drivers/generic/serial-console.c`、`src/kern/platform/visionfive2.c`、`vf2-pmu.h`、HAL の `src/hal/riscv64/cons.c`・`cons.h`・`lib.c`、libvulkan の `context.c`・`wsi-display.c`・`wsi-swapchain.c`・`wsi-image.c`・`memory.c`。
- vendor: `vs_dc_hw.c/.h`。
- mainline: gh api で `vs_primary_plane.c`・`vs_primary_plane_regs.h`・`vs_crtc_regs.h`・`vs_dc_top_regs.h` を取得し、scratchpad の `/tmp/claude-1000/-home-awe-zedBSD-claude1/70c47280-c9e2-5e3f-b5d9-072cd0539dfc/scratchpad/ml/` に置いた。
- Mesa は今回は取り直していません（版 1 のレビューの確認のまま）。

### 前回の指摘の対応の判定

| 指摘 | 判定 |
| --- | --- |
| B1 | 契約（latch の後に返る）は書かれた。ただし p002 の待ち方に新しい誤りがある（新 M1・M2）。部分 |
| B2 | 方針は良い。ただし R3 に抜け道がある（新 M4）。部分 |
| B3 | 大きさの一致の条件は良い。ただし書き込みの途中の部分 latch が残る（新 M3）。小さい BLOB の時の戻しの記述も無い（Minor 3）。部分 |
| M1 | 直った |
| M2 | 「参照の移し替え」が誤り（新 B1）。未解決 |
| M3 | 方針は良い。path・持ち主・「fbcons の描き直し」が誤り（Minor 1・2） |
| M4・M5 | 直った（blend の offset は今決められる、Minor 5） |
| M6 | 中身が誤り（新 M5。前回の私の M6 の記述も不正確だった） |
| M7 | 直った（U0 の時期の問題は Minor 8） |
| M8 | 直った。p004 の後の戻し先は未定（新 M6） |
| M9 | 直った |
| Minor 1〜6・8〜11 | 直った（6 は新 M1 を参照） |
| Minor 7 | 部分（Minor 7 を参照） |

### Blocker

**B1（新）. §4.5 の「front の参照を resource から display へ移す」で、二度目の present ができない（または解放後の使用になる）**
- 根拠:
  - display の resource は import の時に lookup の retain を 1 つだけ持ちます（`display.c` import_image、`:939`。解放は `resource_destroy` の `:468`）。
  - `render.h` には lookup の外に retain がありません（`render.h:28-50`）。
  - swapchain は同じ 2〜3 個の image を何度も present します。
- 起こる事:
  - present R → 参照を display へ移す → present S が latch → display が R の参照を外す。この時点で R は参照の無い resource のまま生きています。
  - 次に R を present すると、参照を取り直す道がありません。backing は import の時にしか来ません。
  - その間に export と render 側の handle が消えると、buffer は解放されています。それでも DC8200 は解放済みの物理を scanout し、copy の道の `show_shared_frame`（`:791`）は解放済みの view を読みます。
  - p003 の UNGATED（pin は callback の間だけ、`gpu-display.h:60-75`）でも同じです。
- 直し方:
  - 「移す」をやめ、display.c の中で display の resource に保持の数（front・pending の数）を持たせます。`resource_destroy` は保持中なら解放を遅らせ、最後に外す時（thread の文脈）に `powervr_buffer_release` します。これなら他の WS の変更は要りません。
  - または WS269 に `powervr_buffer_retain` を `render.h` へ足してもらいます（依頼 R8 を足す）。

### Major

**M1（新）. p002 の polling による latch の判定が成り立たない（B1 がまた起きる）**
- 根拠:
  - tick は 1 kHz です（`include/hal/arch.h:100`）。`kern_usleep_range` は busy の待ちです（`clock.h:196-201`）。
  - 1080p60 の垂直帰線は約 45/1125 行、約 0.67 ms です。眠りながらの polling では「active の外の行」をほぼ観測できず、上限に達します。busy で待つと 1 frame ごとに最大 16 ms の CPU を使い、CPU の copy を省く目的と食い違います。
  - `CURRENT_LOCATION` は vendor・mainline とも定義だけで、使っていません（`vs_dc_hw.h:145`、mainline `vs_crtc_regs.h`）。bit の並びは未確認です。
  - 帰線の中、latch の点の後に COMMIT を書くと、設計の条件（外を通って先頭に戻る）は次の latch の前に満たされたと誤って判定され得ます。
- 起こる事: 前の front がまだ読まれている間に present が成功を返し、libvulkan が hold を外します（`wsi-display.c:1475-1483`）。B1 と同じ崩れです。
- 直し方（どれかを選ぶ）:
  - (a) vblank の IRQ（p003 の IRQ の部分）を p002 に繰り上げる。推奨です。
  - (b) polling を残すなら、判定の手順を厳密に定めます。commit を書いた直後の標本が active の中にある事を確かめ、そこから行が戻る（wrap する）のを待ちます。帰線の中で書いた時は、次の帰線まで待ちます。加えて p002 の最初に `CURRENT_LOCATION` の bit の並びと COMMIT の自己 clear を確かめる手順を置きます。
  - どちらを採るかを判断 D10 として出します。

**M2（新）. 上限を超えた時の EIO は libvulkan で VK_ERROR_DEVICE_LOST になる**
- 根拠: `wsi-display.c:1637-1639`（EIO・ETIMEDOUT → DEVICE_LOST）。
- 起こる事:
  - HDMI を抜く、または負荷で待つ thread が 50 ms 以上走れないと、compositor の Vulkan device が失われます。
  - 書いた commit が後から latch される事もあります。その時 libvulkan は新しい image を hold していないので、app が描き直している最中の image を scanout します（memory は安全でも、崩れが出ます）。
- 直し方:
  - 上限の判定は時計ではなく、DC が進んでいるか（行が動くか）で行います。
  - 本当に出力を失った時は ENODEV・ENXIO（SURFACE_LOST）にします。
  - 失敗で返す前に register を前の front へ書き戻して commit します。
  - 扱いを判断 D11 として出します。

**M3（新）. register を書いている途中の部分 latch（shadow を止めない）**
- 根拠:
  - vendor は書く間、`PANEL_CONFIG_EX`（0x2518 + 4n）の bit 0 で shadow の更新を止め、書き終えてから戻します（`vs_dc_hw.c:1519-1547`、`vs_dc.c:1352・1359`）。
  - 設計は「要らなければ書かない」とし、実機の目視で確かめるとしています（§2.6）。1 frame だけの崩れは目視では検出できません。
- 起こる事: 新しい ADDRESS と古い STRIDE・SIZE が 1 frame 採られると、新しい buffer の stride が bootfb の pitch より小さい時に、DC が buffer の外を読みます（B3 と同じ種類の一時の漏れ）。
- 直し方（どれか）:
  - vendor の止める → 書く → 戻すの手順を常に使う（害は無い）。
  - 途中の状態が常に範囲内になる書き順を決める。stride が縮む時は STRIDE を先に、伸びる時は ADDRESS を先に書く。
  - p002 では stride が snapshot と等しい時だけ zero copy にする。

**M4（新）. R3（SHAREABLE の時に SLC を迂回する）に抜け道がある**
- 根拠:
  - core の image の export は、blob が SHAREABLE で作られたかを見ません（`gpu.c:4853-4903`。flag の検査は fd の flag だけ）。
  - `render_export`（`render.c:786-840`）も見ません。
  - `render_import` は import の側を常に mappable=1、つまり迂回で写します（`render.c:897`、`:981-985`）。作った側（非 mappable）と import した側で属性が混ざります。
- 起こる事: SHAREABLE でない blob や storage の resource を image として export すると、display はそれを zero copy で出し、SLC の古い内容が画面に出ます（B2 がまた起きる）。
- 直し方:
  - R3 を「buffer の属性として迂回し、その buffer の全部の写像を同じ属性にする」と書き直します。
  - display の import は、迂回の印の無い buffer を copy の道に回します（`render.h` の照会が要る。R3 に含める）。
  - D8 の (a) の費用（SHAREABLE・CROSS_DEVICE の全部の buffer が SLC を使わなくなる。app と compositor の共有にも及ぶかは推測）を明記します。

**M5（新）. §5・R7 の libvulkan の条件が誤っている（前回の私の M6 も不正確でした）**
- 根拠: `context.c`。
  - `:176` で copy_display は `xml_version == 1.4.343（WINQ）` の時だけ真です。1.3.269 の capset なら、vendor の flag に関係なく shared の道を試します。
  - flag が「ちょうど OPAQUE|STRICT_QUEUE|QUIESCE|HOST_SCANOUT」である必要があるのは WINQ の時だけです（`:205-211`）。
  - ほかの必須の条件は次です。
    - wire_version 1、capset が 156 byte 以上、timelines が 0 でない（`:155-174`）。
    - `GPU_CAP_SHARE` と linear の SAMPLED（`wsi-image.c:52-58`）。
  - 1.3.269 では外部 memory が DMABUF で、blob は `SHAREABLE|CROSS_DEVICE` になります（`wsi-image.c:63`、`memory.c:1249-1251`）。
- 起こる事: WS269・WS270 に、要らない STRICT_QUEUE・QUIESCE の義務を課すおそれがあります。または版の選び方を誤ります。
- 直し方: R7 を「1.3.269 で出すか、1.4.343 なら完全な flag と義務」という選択として書き直し、上の必須の条件を列挙します。

**M6（新）. 自前で mode を設定した後（p004、または D1 が no-go の時）の戻し先が無い**
- 根拠:
  - release・panic・COPY の道は、U-Boot の plane の snapshot と bootfb を戻し先にしています（§2.4・§4.5・§4.6）。
  - p004 で 720p などに変えると、snapshot の SIZE・STRIDE は今の timing と合いません。
  - D1 が no-go の時は p004 が先になり、snapshot 自体がありません。
- 直し方:
  - p004 の前提として、次を設計に足します（p004 の前の追補でもよいが、版 3 に「未定」と明記する）。
    - release・panic の時に mode も戻すか、console を今の mode に合わせて描き直すか。
    - D1 が no-go の時の console と panic の扱い。

### Minor

1. **path と持ち主の誤り**:
   - `src/kern/gpu.c` は正しくは `src/drivers/gpu/gpu.c` です。
   - `src/drivers/platform/visionfive2/serial-console.c` は正しくは `src/drivers/generic/serial-console.c` です。これは汎用の file で、riscv-virt も使います（`src/kern/platform/riscv-virt.c`）。
   - `visionfive2.c` は正しくは `src/kern/platform/visionfive2.c` です。
   - R4 の「VF2 の text の ops に reveal_fatal を足す」は、汎用の file に VF2 の DC への依存を入れることになり、層の向きが逆です。VF2 の platform に wrapper の ops を置くか、serial-console に汎用の fatal hook の登録口を作る形にしてください。持ち主は WS256 p008（cleared）の後で Q1 が決めます。
2. **「HAL の fbcons の描き直し（既存の reveal と同じ道）」は VF2 に存在しません**:
   - serial の ops は reveal・reveal_fatal を持たず、resume は何もしません（`serial-console.c` の ops）。
   - grid の描き直し（`riscv64_cons_redraw`、`src/hal/riscv64/cons.h:20`）は HAL の中だけの関数で、hal.h にありません。kernel から呼ぶ形にすると HAL の API の変更になり、承認が要ります。
   - 描き直しは不要です（panic の文字は `hal_putc` から描かれる）。この記述を消してください。§4.5 (5) の「描き直し」も同じです。
   - 他の hart の present が reveal_fatal の後に commit し直さないよう、fatal の印を present の側が見る事も足してください。
3. 小さい BLOB の frame を copy の道へ回す時、zero copy の front から bootfb へ plane を戻す commit と latch の待ちが要ります。§4.2 には COPY の行にしか書かれていません。
4. **`stride_alignment = 32` を p002 から出すと、今より遅くなる場合がある**: libvulkan は import の前に、stride が 32 の倍数でない image を拒みます（`wsi-display.c:1290-1293`）。その時は readback の道に落ち（`wsi-swapchain.c:1104-1120`）、今の kernel の BLOB の CPU の copy より遅くなります（例えば 1366 幅）。32 は R6 が入った後に出すか、4 のまま import で判定してください。
5. **blend の offset は今決められる**: 0x2510 + 4n です（vendor `vs_dc_hw.h:74`、mainline `VSDC_FB_BLEND_CONFIG`）。swizzle は 2 bit（23-24）、format は 6 bit（26-31）です。
6. **mutex を持ったままの待ちが他の session を止める**: display の mutex は、全部の session の `resource_create`・`resource_destroy`・`import_image`・`close` の数え上げにも使われています（`display.c`）。待ちの間（最大 50 ms）これらが止まります。「flip 中」の状態を作って、mutex を外して待つ形を推奨します。
7. **Minor 7 は部分的**:
   - §7 の p002 の依存「WS269 p006 の lookup」は Phase 全体の依存のままです。p006 は in-progress です（`plan/ws269/ws.md:30`）。「lookup の code（merge 済み）」と、出力の範囲で書いてください。
   - ws.md の「依存」節は R1〜R7 と WS269 p010 を反映していません。
8. **U0 は p002 の code を要する**: go/no-go（U0）は、attach・判定・記録の code が無いと判定できません。16〜24 LW を入れる前に、判定と記録だけの小さい Phase（1〜3 LW）に分けることを推奨します。
9. **U3・U4 では誤りを検出しにくい**:
   - ADDRESS の読み戻しは、おそらく shadow（書いた値）で、latch された値ではありません（推測）。
   - 1 frame の崩れは目視で見落とします。
   - 検出の限界を記録するか、kernel の中で latch の sequence と present の返りの順を記録して照合する、決定的な確かめを足してください。
10. **R6 の補足**: libvulkan は scanout の image を自分で `VK_IMAGE_TILING_LINEAR` で作り（`wsi-image.c:80`）、GPU の copy（`present_copy_shared`）で書きます。R6 の中身は次の 2 つです。
    - PowerVR の Vulkan で、linear の image の `rowPitch` を 32 byte の倍数にする（descriptor はここから作られる）。
    - transfer の書き出しを LINEAR にする。
11. **acquire の遅れ（性能、推測）**: libvulkan は PRESENT の ioctl の間、global の `display_mutex` を持っています（`wsi-display.c:1414` 付近）。present が latch まで待つと、acquire の `image_available` も最大 1 frame 待ちます。bcm2711 と同じ形なので許容できますが、p006 の測定の項目に入れてください。

### 他 WS への依頼 R1〜R7

| 依頼 | 判定 |
| --- | --- |
| R1・R2 | 正しい |
| R3 | 抜け道がある（M4） |
| R4 | path と持ち主、層の向きが誤り（Minor 1） |
| R5 | 正しい（任意である事も正しい） |
| R6 | 補足が要る（Minor 10） |
| R7 | 中身が誤り（M5） |

漏れ:
- B1（新）を `render.h` の retain で解く場合の WS269 への依頼（R8）。display.c の中で解けば不要です。
- R3 に含める「迂回の印の照会」の口。

### 判断 D1〜D9

| 判断 | 評価 |
| --- | --- |
| D1 (a) | 妥当。go/no-go の確かめを小さい Phase に分ける事を推奨（Minor 8） |
| D2・D3・D5・D6・D9 | 妥当 |
| D4 (a) | 方向は妥当。条件の中身を直す（M5） |
| D7 (a) | 妥当。ただし M6 の戻し先と一緒に決める |
| D8 (a) | M4 の直しと費用の明記が前提 |

足りない判断:
- D10: p002 で latch をどう知るか（IRQ を繰り上げるか、厳密な polling か）。
- D11: latch の上限を超えた時と HDMI を抜いた時の扱い（errno、書き戻し）。

### HAL

Minor 2 のとおり HAL の描き直しを呼ばない形であれば、`hal.h` の変更は要りません。§8 の結論は保てます。

### 未確認（推測を含む）

- `CURRENT_LOCATION` の bit の並び。COMMIT の自己 clear。ADDRESS の読み戻しが shadow の値か、latch された値か。
- `gpu_image_validate` は grep で SHAREABLE を見ていない事を確かめただけで、本体は読んでいません。
- SHAREABLE の blob が app と compositor の共有にも使われるか（M4 の費用）。
- mainline の手順が JH7110 で正しいか。

### 結論

**p001 を cleared にしてはいけません。**
- 新 B1（参照の移し替え）は、そのまま実装すると二度目の present ができないか、解放後の使用になります。
- 新 M1〜M5 は、どれも B1・B2・B3 の直しの不完全さ、または他の WS への誤った依頼です。
- 版 3 で B1・M1〜M5 を直し、M6 は「p004 の前に追補」と明記し、Minor 1・2・7 を直してから、差分の再確認に回してください。
- 全体の方針（線形の X 系の zero copy、U-Boot の mode の引き継ぎ、latch の後に返す契約、HAL の変更無し）は妥当です。
