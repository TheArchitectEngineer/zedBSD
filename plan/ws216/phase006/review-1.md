<!-- awesome-plan project=zedbsd record=ws216-p006-review-1 -->

# ws216-p006 review-1（phase.md 244091c0b の設計の敵対的 review、2026-10-13、Q1 が起動した design-reviewer）

source は 244091c0b と main 4f87ce7ba で同じことを確かめ main で読んだ。P1 の未 commit は見ていない。build・試験・QEMU なし。時間の見込みは推測。

**判定: 条件つきで進めてよい。** 致命的 0・重大 1・中 9。重大と C-1〜C-3 は main.c の中で直せ、ユーザーの判断も他の Phase も要らない。

## 重大

**S-1. 指で塗る経路（§5）が app_finger() に止められ、FILL の contact が残り autosave も止まる。** main.c:2135-2140 は BEGIN の後の contact が DRAW か ERASE の時だけ finger_contact = 1。main.c:2148-2152 の END・ABORT は finger_contact が 1 の時だけ app_input()・app_abort_contact() へ。autosave は contact が NONE の時だけ（main.c:782-785）。Finger on で塗りの道具の指の tap: contact は FILL だが finger_contact 0 で END が捨てられ塗られない、contact は FILL のまま、autosave が次の contact まで止まる。次の press は「離しを失った contact」で app_end_contact(app, NULL)（main.c:1787-1788）を通り、そこで塗る実装だと前の点を塗る。2 本目の指の ABORT も捨てられる。案: finger_contact の条件に FILL（と帯を閉じるだけの contact）を足す、app_end_contact(…, NULL) と app_abort_contact() では FILL を塗らずに捨てる、動いた距離は押した点からの最大の距離。

## 中

**C-1. 背景が描けない頁で painter の失敗を EIO（"Could not fill the area"）にしている（§5・§6）。** 画面の背景は PDF が無い時（main.c:1097）、display list が作れない時（main.c:1143、background_failed）、raster・malloc の失敗（main.c:1152 など）は白い紙のまま。§5 の「origin == OVER か編集がある時に painter」は app_background() の条件（base != NULL）や app_page_frame()（main.c:2609、drag を含む）と一致しない。libpdf が描けない PDF の頁は画面では白い紙の上に線が見えるのに塗りが毎回断られる（D5 (a) と合わない）。案: painter を渡すかは「画面がその頁の背景を描いているか」の一つの判定（app_background() から取り出す）、painter に「背景は無い（白）」の値、EIO は画面で描けているのに帯の raster が失敗した時だけ、painter の display list は page・source・look が今の頁と一致するか確かめる（main.c:1110-1116 と同じ）。

**C-2. 帯の背景の raster の時間と Guardrail が記録されていない。** raster.c:186-226 の pdf_display_list_rasterize() は帯ごとに display list の全部の item の edge を組み直す（帯の範囲で間引かない）。A4・R=4 で約 8 Mpx、画面の背景の約 6 倍。p005 review-1 M-1 は GPU の部分だけで Pi は数秒の見込み。Guardrail「画像の処理は GPU で」（plan/guardrail.md:232）は CPU が画素に触るのを「内容が変わった時の一度だけの用意」に限る。文字の多い PDF の上の大きな塗りで Pi の UI が「Filling...」のまま数秒〜十数秒止まり、上限も取り消しも無く log にも数が出ない（推測）。案: NOTES FILL made の log に背景の raster の合計 µs と帯の数、帯ごとの CPU の PDF の raster は D5 の例外（tap の一度）の中と明記、p009 の受け入れに時間の数、遅すぎれば p005 の LINEAR の texture に戻す道を書く。

**C-3. AAT の helper の手順が不完全で古い座標を押しうる（§7）。** helpers_notes_edit.py:91-98 の button() は action を持つ一番新しい NOTES BUTTONS の行を遡って探し、その行は app_action() の後の draw で button の並びの signature が変わった時だけ出る（main.c:2374-2391）。(a) 今の道具が Select などで最後の描く道具が Pen でない時、DRAW で `NOTES TOOL 2 name=highlighter` が出て §7 のどちらの分岐にも入らず時間切れ。(b) NOTES DRAWS open の直後の button(1) は新しい行が出ておらず古い座標を押すか失敗。案: Pen は key の p（NOTES TOOL 1 name=pen を待つ）、button なら mark の後の ` 1:` を含む BUTTONS の行を待ち、Pen 以外の NOTES TOOL ならもう一度 DRAW。

**C-4. 塗りで幅の button を消すと card が中央に寄り直し DRAW と帯が動く（§1・§3）。** card は中身の幅で中央（ui.c:274）、幅の 3 つと区切りは約 128 px（ui.c:489-515）。§1 の「label の幅が変わっても後ろの button が動かない」に反し DRAW 自身が約 64 px 動き、Pen → Fill → Pen のたびに toolbar が左右に動いて続けての tap が外れる。案: 塗りの時は幅の欄の分の slack を残して描かない、U2 に「描く道具の間で全部の button の x が同じ」。

**C-5. 同じ所を 2 回 tap すると同じ塗りが重なる（§5）。** 2 回目は見えている物が 1 回目の塗り（fill-page.c:109-111）で z は直上、領域も同じ（p005 L-11）。Finger on の指の double tap は拡大されず（touch.c:808-811）2 回の BEGIN・END になり、拡大のつもりで同じ塗りが 2 枚、undo が 2 回、file も大きくなる。案: tap の点で見えている一番上が「同じ色の FILL」なら塗らずに状態だけ出す。

**C-6. 帯・道具の状態の遷移が main.c にしか無く host で検出できない（U6）。** DRAW の押し、帯の button、頁の press、key・menu で閉じる、最後の描く道具の更新、8 px の判定は全部 main.c で p007 まで確かめられない。案: 純粋な関数（例 notes_ui_draw_press(state, action)）として ui.c に置き host-notes-ui.c で表の試験。

**C-7. U2 の「label が変わっても後ろの x が同じ」が font が無いと必ず通る。** face が無い時は button の幅が一律 44 px で slack 0（ui.c:972-974・1083-1084）。案: 試験は tree の fonts/keiland/Mahora-Regular.ttf を開き ui->face != NULL を確かめてから比べる。

**C-8. 依存の書き方と Phase の状態の投影。** ws.md:36 は p006 の依存を「p004、p005」、p004・p005 は test-wait で clearance は p007 を待ち p007 は p006 に依存して輪。ws.md の p006 の行は planned、phase.md は in-progress。案: 「main に入った p004・p005 の出力（notes_fill_page()・notes_fill_notice()・頂点の塊）」の出力の依存に直し ws.md の行も直す。

**C-9. p001 の受け入れ・計画との差を記録していない。** p001 §8 の p006 の行と §9 の WS081 の行には touch.c の double tap の停止、host-notes-fill-touch.c、受け入れ「塗りの道具・Finger on で double tap が拡大しない」がある。touch.c を変えない理由は正しい（touch.c:808-811）が U の表に受け入れと証拠が無い。案: U5 に plan/ws081/tests/run-notestouch.sh（Master に登録済み）を足し p001 §8 との差を書く。

## 軽微

- L-1. 消しゴムの時に色を押すと Pen に替わる（main.c:1398-1399）。「最後の描く道具」に戻す方が一貫。最後の描く道具を更新する経路（帯・key・menu・DRAW）を §1 に列挙。
- L-2. 幅の点の半径は widths[index] > 8.0f で index ごと（ui.c:497）で、水彩 2・6・9 px、筆 2・4.5・9 px と不揃い。道具ごとに点の大きさの組を。
- L-3. 帯の card の影（12 + 4 px）は toolbar の絵の下端（NOTES_TOOLBAR_IMAGE_HEIGHT = 112）で切れ、帯は約 70〜110 行なので頁の上に影の切れ目。影を小さくするか絵の高さとの関係を書く。
- L-4. Eraser と同じ作りの slack を DRAW の直後に置くと DRAW と Eraser の間に label ごとに違う穴。slack は道具の組の最後に（か DRAW の幅を一番長い label に固定し字を中央に）。
- L-5. painter の中の背景の image の確保が VK_ERROR_OUT_OF_DEVICE_MEMORY なら ENOMEM で帯を半分にする経路へ（§6 は EIO）。background_page = NULL は painter が一度でも走った後の全部の経路で（帯の大きさが頁の絵と偶然同じだと main.c:1101-1108 の確かめを通ってしまう）。painter は白で clear してから raster（raster.c:150）、出る画素は不透明な premultiplied の BGRA。
- L-6. 「Filling...」は頁の外の tap（EINVAL）でも出てから断られる。頁の内かを先に。成功の後に status を消すと前の status（"Saved" など）も消える。断りの notice は MAIN_NOTICE_MS で（今の app_status() は MAIN_STATUS_MS）。
- L-7. 帯を閉じる条件に指の page の tap（Finger off は app_touch() の !toolbar の tap、main.c:1929）、Esc、頁の移動を明記。指の scroll・pinch の間は閉じない。
- L-8. 指の 8 px は touch の tap の slop より狭い見込み（推測）。指だけ大きくするか p007 で測る。
- L-9. NOTES BUTTONS overflow の「一度だけ」は process ごと（struct notes_ui に flag）。
- L-10. notes_fill_page() の引数を int background から painter に変えるのは p005 の口の変更。p005 の phase.md に注記し host-notes-fill.c の呼び出しを U4 で直す。
- L-11. U2 に、帯が開いている間 notice が帯の行に描かれないこと、card の横の status はそのまま出ること、帯の点で notes_ui_hit() が帯の action を返すこと。
- L-12. Tool menu の新しい item の id は 45〜48（menu.c:64-71、50 は Fullscreen）で空きは 4 つだけと書く。U5 の「WS175 の 2 つ」は script の名前（run-host-notes-edit.sh・run-host-clean.sh）で。
- L-13. 残件として beta2 の手引き（docs/release/zedbsd-1.0.0-beta2-guide.md）の Notes の節を Q1 に送ると書く（p001 §7 の末尾）。

## 問題が無かった点

double tap の拡大は Finger on の間すでに止まる（touch.c:808-811、WS081 の試験も write mode で確かめている）。下に入る塗りは reshaped++ で頁の絵が描き直される（document.c:456-458）。背景の image の作り直しは vkDeviceWaitIdle を通り（render.c:495-499）frame も帯も fence を待つ。action の番号 48〜52 は空き、button は最大約 25 で 48 に収まる。WS079・WS081 の guest の試験は action 8・14・32 を NOTES BUTTONS から引くので並びの変更に影響されない。tests/scenarios/apps/notes/ は Pen・Marker の button に依らない。key B・L・W・F は既存の key・menu の shortcut と衝突しない（Control+W は Close のまま）。

## ユーザーに聞く点（実装を止めない）

1. label の言葉: 帯と DRAW の button は "Water"、Tool menu は "Watercolor"。toolbar も "Watercolor" に揃えるか（幅は増える）。
2. DRAW の button に帯が開くことを示す印（"v" など）を付けるか。付けない場合、Pen の時に Pen を押すと帯が開く。
3. Pi で数秒かかる塗りで「Filling...」のまま待つか、上限（N 秒）で止めて notice か、p009 の数を見てから決めるか。
4. 塗りの道具で頁の上を drag した時、何もしない（log だけ）か「Tap to fill」の status を出すか。
5. （報告）帯を開いている間は notice が出ず、閉じた時に時間が残っていなければ失われる。起動の一度だけの案内も帯を開いていると見えないことがある。
