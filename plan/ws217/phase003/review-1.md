# ws217-p003 review-1（phase.md 64e24f620 の設計の敵対的 review、2026-10-13、Q1 が起動した design-reviewer）

source の行番号は 86ee7cf3e の tree。途中で P1 の 690bd2b2a（実装）を S1・M1 の根拠の確かめのためだけに読んだ。

**判定: 条件つきで進めてよい。致命的 0・重大 1・中 7・軽微 12。** 座標の式に誤り無し（4 つの角度、unit 1 と 256、端、半端な画素、0° で今の式と一致、§12 の期待値を手で確かめた）。direct の分け方は D3・D11・D12 と食い違わない。S1・M1・M2 は T1 の前に直す。M3・M4 は Q1 の判断とユーザーへの報告。HAL の変更不要、UAPI は種類の値を 1 つ足すだけで ABI は互換。

## 重大

**S1. open の経路で cancel を pointer の turn の後に置くと「同じ物理の点」が壊れる（§6）。** §6 は open の経路（kwl_output_rotated）で cancel を呼ぶとだけ書き kwl_output_pointer_turn との順を決めていない。shell の指の cancel（kwl_touch_remove と同じ写し）は shell_press(place_x/256 …) で pointer を古い論理の座標へ warp（touch.c:438-445 → place_pointer → kwl_pointer_warp_to、touch.c:2405-2418、clamp 無し）。open の経路では大きさと回転が既に変わっているので、指の古い論理の座標は新しい frame では別の物理の点で、幅 800 に x=1200 のように外へも出る。90↔270（RESIZED 無し）では clamp も走らず pointer が誤った場所に残り shell の release（窓の move の drop）もそこで起こる（p002b review-1 S1 の保証が壊れる）。実装 690bd2b2a の output-switch.c:491（pointer の turn）→ :495（kwl_input_rotated）がこの順。§12 に open の経路と指の組の試験が無い。直し: §6 に「cancel は kwl_output_pointer_turn の前」、回転による cancel の shell の release は pointer を動かさず今の場所で離す（place_pointer を飛ばす）。実行中の経路（output-rotate.c の 361 が 228 より前）は正しい順。

## 中

**M1. cancel の button の release が pointer の drag and drop を drop してしまう（§6）。** touch の ROUTE_POINTER（wl_touch の無い client）と pen の ROUTE_POINTER の cancel は kwl_seat_button(LEFT, 0)（touch.c:446-447、tablet.c:828-833）。drag の最中は最後の button の release で kwl_data_drag_release（seat.c:686-691）、client が pointer の serial で始めた DnD（Files など）は回転の瞬間に古い場所へ drop される（ROUTE_DRAG だけは cancel）。直し: kwl_input_rotated の頭で server->drag があれば kwl_data_drag_cancel を呼んでから button を離すと §6 に。

**M2. T1 の試験で pen の cancel・open の経路・後始末を確かめていない（§12）。** (1) pen を下ろしたまま ROTATE: peninject の down を保持 → ROTATE → KWL TABLET cancel pens=1、client が up と proximity_out、次の move で新しい写像の点から proximity_in、down は来ない（§15 の制限）。(2) open の経路の ROTATED（S1）、指を置いたまま --rotate 付きの output switch か MODE。(3)「続く touch-move は何も起こさない」の観測（wl_touch の client（wltest）が motion を受けない、か POINTER が変わらない）。(4) ROTATE 90 は displays.conf に保存するので最後に ROTATE 0 と POLICY default（p002b review-1 L11）。(5) 期待の log は実装の `KWL TOUCH cancel all reason=rotated fingers=N` を §6・§12 に明記（KWL TOUCH cancel client=… は wl_touch の client の上の指だけ、touch.c:2361）。(6) タップの点は edge の帯・keyboard の zone・title bar を避け期待する行（KWL TOUCH shell …）を固定。

**M3. p001 の review で入れた「grab は終える」を技術判断だけで外している（§14 の 1）。** review-1（phase001/review-1.md:121）と p001 §4.6 A の 5（phase001/phase.md:355）の要件。「回転は Settings の押しの後だけ」は open の経路（hotplug・lid・switch）と p012 の game の session の外しと戻しに当てはまらない。mouse の resize の grab の最中に回ると pointer が同じ物理の点（論理では遠い点）へ飛び grab の差分で窓の大きさが大きく変わる（推測）。直し: p001 のとおり grab を終えるか、Q1 がこの違いを受け入れたことを記録。

**M4. Linux・FreeBSD の touch pad の扱いが変わることをユーザーに報告していない（§9、D11）。** 今は MT かつ INPUT_PROP_POINTER の device（laptop の touch pad、DualShock・DualSense の touch pad、Wacom の finger の node）が touch screen として絶対に写っている（input.c:883-900）。EVIOCGPROP を読むとこれらが touchpad.c の相対の pointer（tap・gesture・加速）になる。直す方向は正しいが D11 の推奨の文にこの副作用は無く製品の挙動が見える形で変わる。直し: Q1 からユーザーに報告、Linux の QEMU+KVM の guest（WS131）で uinput（plan/ws213/tests/uinput-drive.c の形）の MT＋POINTER の device が KWL TOUCHPAD added、MT だけが KWL TOUCH added を T2 に依頼、FreeBSD の build を §11 に。

**M5. 絶対の pointer は property を見ずに常に direct が kernel の新しい POINTER と食い違う（§4）。** kernel は Digitizer の pen に POINTER を付ける（§7）が、Tip Pressure の無い pen は ABS_PRESSURE が無く tablet にならず絶対の pointer に落ち（input.c:145-201）常に direct として回り D3（机の上の板は回さない）に反する。直し: 絶対の pointer は「INPUT_PROP_POINTER が無ければ direct」（QEMU の tablet・AAT・virtio は property が無いので direct のまま）か §15 に制限。

**M6. mirror で回した時の head の上の pen・touch の写像（§15 の不足）。** p002b は extended の時だけ拒み mirror では回せる。p003 では全部の絶対の device が anchor の物理と回転で写り、mirror の head（BUG-267 の pen LCD を mirror に置いた形）の上の pen は head が見せる fit の絵（回っていない、帯つき）と向きがずれる。直し: §15 に「mirror の head の device は p006 まで anchor の回転で写る」、p005 か p006 までは mirror で回転を拒むかを Q1 に。

**M7. 他の panel へ移る open の経路で geometry を warp 無しで書くと古い motion を 1 つ受け入れる（§6）。** kwl_input_geometry は kwl_cursor_box_outputs で書くので warp の番号が進まず（cursor-box.c:207-225）、kwl_output_size_set と geometry の書きの間に I thread が古い geometry で計算した絶対の place がそのまま採られる。RESIZED があれば output_pointer_clamp の warp が捨てるが、同じ大きさで回転だけ違う panel への switch では捨てられない。直し: rotation か physical が変わる時は kwl_cursor_box_outputs でも warp の番号を進めるか恒等の move で warp。

## 軽微

- L1. cancel が output_rotate_turn の中なので TURN の bit にしか付かない（output-rotate.c:210-215）。p010 の REOPEN で漏れないよう §6 に「ROTATED の bit で呼ぶ」。
- L2. §2 の kwl_pointer_warp は physical を今の server の状態から作り outputs を呼び手の物で上書き、別の時点の値だと食い違う（今の 6 つの呼び手は直前の kwl_outputs で問題無し）。不変条件として注記。
- L3. §2 に kwl_cursor_box_init が physical[ANCHOR] を（width, height, 0）、absolute_slot を ANCHOR にすると書く（WS213 の input-thread-host-test.c:443-457 は geometry の warp の前に絶対の report を送る、実装は済み）。
- L4. POINTER は §10 で main thread の pointer だが I thread の event より先に答えて競合、T1 は期待値に一致するまで繰り返し読む形に。
- L5. p001 §6.1・§6.4（L19）の注入の名前 inject-pen／inject-pen-pointer と §8 の Test pen (input-inject)／Test pen tablet (input-inject) が違う。p006 の identity の例と試験の期待値（virtual:0000:0000:Test_pen_(input-inject)）の直しを記録。
- L6. include/uapi/input-inject.h:17-22 の冒頭の説明に PEN の INPUT_PROP_DIRECT と PEN_POINTER を足すと §8 に（touchinject の setup-unknown-kind は kind 4 で 6 を足しても壊れない）。
- L7. §9 の「header の説明」は keiland-backend-evdev.h:41-44 にあり input-evdev.c に無い。
- L8. §15 に制限: 板の pen tablet で Pen（0x02）の collection を申告する物（uclogic 系、推測）は回る、Digitizer（0x01）を申告する pen の液晶は回らない（手で選ぶ口は Future Work）、touch の軸が panel の走査の向きと揃わない panel（calibration）は扱わない。
- L9. 回転の cancel の shell の release は click や gesture を完了させ得る（kwl_touch_remove と同じ形）。shell に「取り消し」の口を足すかは記録だけ。
- L10. p001 §4.6 A の 5 は「park の中」で cancel、p003 は park の前（p003 の方が正しい）、この違いを §14 に。
- L11. host-hid-pen-props の fixture に「Digitizer の pen と Touch Pad（POINTER）の 1 つの descriptor → POINTER」。BUG-267 の if1 は Pen と Generic Desktop の Mouse（0x01 0x02、ReportID 1）を持ち、main の device が mouse と pen の組でも DIRECT になることを期待値に。
- L12. p002b の T1-583 が FAIL して p002b の hook（output_rotate_turn・kwl_output_rotated）が変わると p003 の差し込み点も直す必要、Q1 の承知の上として記録。

## 問題無し

90° の touch (Y, 1279−X)、270° (799−Y, X)、180° (1279−X, 799−Y)、24.8 の逆写像（rotate.c:96-158）の整数部は画素の式と一致。direct の pen は x=Y y=1279−X、傾き (20,0) → (0,−20)。--pointer は ⌊X·799/1279⌋、⌊Y·1279/799⌋。論理の大きさを書くのは kwl_output_size_set だけ（output-switch.c:307-331）で geometry の physical は mode と一致。drv_hid_input_publish は i2c-hid・usb-hid・input-bridge の 3 つから呼ばれ HID_INPUT_TOUCH_ONLY を使う transport は無い。KWL TABLET added の行の末尾に direct= を足しても行の頭で照合する既存の試験は壊れない。

未確認: 実機の USB pen と I2C の pen の collection、QEMU の usb-wacom-tablet の分類、resize の grab の最中の回転の窓の大きさ（M3）、Linux の touchpad.c の実機の動作。
