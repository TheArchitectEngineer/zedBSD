<!-- awesome-plan project=zedbsd record=ws216-p005-review-1 -->

# ws216-p005 review-1（phase.md c7fbd7423 の設計の敵対的 review、2026-10-13、Q1 が起動した design-reviewer）

照合は c7fbd7423 の tree。P1 の未 commit の実装は見ていない。build・試験・QEMU なし。

**判定: 条件つきで実装を進めてよい。** 致命的 0・重大 2・中 7・軽微 12。fill.c（§2）と z・undo・journal（§4 前半）はこのまま。S-1・M-1 は §3 を直してから notes_renderer_band() を書く。S-2 は D7 (c) の中身なのでユーザーの判断の後（消しゴムだけ後回し）。

## 重大

**S-1. Pi では帯の render pass が作れず塗りが全部失敗する（§3 notes_renderer_band）。**
bcm2711 の vulkan-target.c:283-290・539-550 の colour_layout() は pass の finalLayout に GENERAL・COLOR_ATTACHMENT_OPTIMAL・SHADER_READ_ONLY_OPTIMAL だけを受け、TRANSFER_SRC_OPTIMAL は ENOTSUP。copy の元は GENERAL か TRANSFER_SRC_OPTIMAL（vulkan-buffer-copy.c:399-407）。barrier での TRANSFER_SRC_OPTIMAL への移行は受ける（vulkan-barrier-validate.c:179,215）。WS141 p017 の後も帯の pass が ENOTSUP で、塗りは必ず EIO（reason=device）、p009 で初めて見つかる。
直し: finalLayout を COLOR_ATTACHMENT_OPTIMAL（か GENERAL）、pass の後に明示の image barrier で TRANSFER_SRC_OPTIMAL に移して copy（i915・Venus にも合う）。F9 の模型で「finalLayout が上の 3 つのどれか」「barrier と copy の layout の一致」を確かめる。

**S-2. D7 (c) の消しゴムの規則で多くの塗りが消せず、消える時は見えない物まで消える（§4、F8）。**
円の半径 MAIN_ERASER_RADIUS 10.0f（main.c:102）。§4 は円がどれかの線に当たれば線の操作。stroke_touches()（document.c:1381-1430）は z を見ず、塗りに隠れた線にも当たる。塗りの操作は中心を含む塗りを全体ごと消す。
1. 幅およそ 21 pt 未満の領域（小さな形・文字・表の枠）は円がいつも囲みの線に当たり、塗りは消しゴムで消せない（undo だけ）。
2. 線の上の tap の塗り（§2.5 の半径 0 のやり直しで線を塗り替える）は線の直上に入り元の線を隠す。円は隠れた線に当たって線の操作になり、見えない線だけが消えて見えている塗りが残る。
3. 重なった塗り（赤の上の青）を塗りの操作で消すと中心を含む赤も消える。D7 (c)「その物を消す」と違う。
M-11 の推奨の副作用で F8 は見つけない。
案（UX の取捨、ユーザーの判断）: 線を優先する距離を円の半径でなく「中心から線の見える半幅 + 小さな余裕（例 2 pt）」に。数える線は当たった所で上の不透明な塗りに覆われていない物だけ。塗りの操作は中心で見えている一番上の塗りだけを消す。F8 に「20 pt の円の塗りを消せる」「線を塗り替えた塗りを消せる」「重なった塗りは上だけ消える」。M-11 の状況（線の近くで背景の塗りが消える）とどちらを優先するか。

## 中

**M-1. 読み戻しの同期と coherency が無い（§3）。** pass の外への依存（COLOR_ATTACHMENT_OUTPUT・WRITE → TRANSFER・TRANSFER_READ。今の頁の pass は FRAGMENT_SHADER への依存だけ、render.c:1101-1106）、copy の後の buffer barrier（TRANSFER_WRITE → HOST_READ、手本 libbrowser/paint/vulkan.c:476-485）、HOST_COHERENT か vkInvalidateMappedMemoryRanges、前の frame の fence の待ち（notes_renderer_draw() は present の失敗で fence を待たずに返る、render.c:443-450。command と頂点の塊を使い回すなら帯は fence を待つか vkDeviceWaitIdle）。実機で古い・一部の画素を読み塗りがでたらめになる。直し: §3 に 4 つを書き、F9 で依存・barrier・memory の性質を確かめる。

**M-2. 塗りが境の線の上に入ると線の縁を最大約 0.29 pt 覆う（§2.7 と §4 の z）。** 半透明の物や線の上の tap では z が「その物の直上」で、境の古い線より上に入る。膨らませ戻し r_b − r_c = 0.75 px + 0.1 pt（R=4 で約 0.29 pt）が線の縁を覆い、0.225 pt のペンは片側の半分が消えて見える。F4 は下に入る場合だけ。直し: 見えている物が紙でない（z > 0）時は r_b = r_c + 0.75 px（0.1 pt の潜らせ無し）。F4 に水彩の上で囲みの線が細らないことを足す。D6 (a) の結果として phase.md に書く。

**M-3. 外の輪郭が捨てられた時の結果が未定（§2.9〜11）。** 小さな輪の中で外の輪郭だけが捨てられ穴だけが残ると、逆向きでも nonzero で穴が塗られる。輪郭 0 個は encode が拒む（p003 §4）。直し: 外が捨てられたら穴も全部捨て ENOENT（reason=empty）と notice。F2 に「小さな輪の中」。

**M-4. 輪郭をたどる memory と時間に上限が無い（§2.8〜10）。** 捨てる（9）と単純化（10）が全部たどった後なので、写真・粒の領域で生の角が数百万。配列が「頁の画素ごとに 2+1 byte」の見積に無く、E2BIG も全部の後。直し: 輪郭ごとに「たどる → 面積で捨てる → 0.5 px で単純化」を流れで、生の角の合計の上限（例 1 Mi）で直ちに E2BIG。F5 に写真の模型の最大の memory と時間。

**M-5. PDF の背景が頁の絵の倍率の texture（NEAREST）で、PDF の線画の上の塗りの精度が D5 に届かない（§3）。** 背景の texture は頁の絵の大きさ（main.c:1101-1104、app_background）、sampler NEAREST（render.c:1485-1487）。Pi の全体表示（約 1.2 px/pt、推測）で境の誤差最大約 0.8 pt、PDF の細い線を覆うか白い隙間。M-3 の目標 0.1 pt より 1 桁粗い。直し: 帯ごとに pdf_display_list_rasterize(list, …, R, 0, top)（include/libc/pdf.h:478）で背景を帯の大きさに CPU で描き host linear の texture（4 MiB 以下）に（display list を callback で渡す、p006 との境界）。少なくとも背景の sampler を LINEAR に。

**M-6. 点の上限 8192 で書いた頁の頁全体の塗り（D12）がたいてい断られる見込み（推測）。** 隙間を閉じる 1.5 pt で文字は単語の塊、塊ごとに穴の輪郭。200〜300 語で穴 1 つ 50〜150 点 → 1〜3 万点。8192 は D12 の文の数だが p004 の塊で頂点はもっと持てる。直し: F10 に「A4 に手書き約 200 語の模型の頁全体の塗りの点の数と時間」。超えるなら上限を上げる案（例 65536、頂点と file の見積）と notice のままの案をユーザーへ。

**M-7. 帯の大きさが Pi の 4096 の上限を超えうる（§2.1、§1 M-4）。** 帯の高さ min(H, max(64, floor(4 MiB/(W×4)))) は 4096 で切らない。W < 256（例 50×2000 pt、R=4、W=200）で 5242 行。bcm2711 は高さ 4096 超を ENOTSUP（vulkan-resource.c:250-260）、半分にする経路（ENOMEM だけ）に入らず EIO。R = 4096/頁の幅で W = ceil(頁の幅×R) が float の丸めで 4097 になりうる。直し: 帯の高さに min(…, 4096)、W = min(4096, ceil(…))、device の上限の断りも半分の経路か前もって切る。F5・F6 に細長い頁。

## 軽微

- L-1. 色の閾値 26/255 では縁 1 は境にならず L-18 の問題が縁 1 で残る。z の「見えている物」の閾値「効く alpha 0.1 未満」は水彩の縁 1（0.10）と同じで float で揺れる。境と同じ「色の差が 26/255 を超えるか」で決める。
- L-2. 単純化を 1〜2 px に緩めると 0.25〜0.5 pt ずれ、0.1 pt の潜らせを超えて線を越えるか隙間。輪郭ごとの独立の Douglas-Peucker で穴が外と交わると winding −1 で nonzero に塗られる。制限として書くか交差の検査。
- L-3. 幅 3 pt 未満で §2.5 が半径 0 に落ち隙間を閉じず、頁全体へ漏れうる。0.75 → 0.375 pt と段で下げてから 0。
- L-4. seed が AA の fringe（灰）だと紙も線も境になり細い切れ端だけ塗られる。seed の近く（3×3）で色の差が小さい画素の色を seed の色に。
- L-5. callback band(context, top, height, pixels) に W と R が無い。context か引数か決める。
- L-6. F3 は CPU の合成の raster だけ。GPU の画素は頂点の float の丸め（view.y = −top）で帯の分け方ごとに少し変わり、seed の位置で 1 画素揺れうる。制限として書く。
- L-7. Pi の copy 先の buffer は uncached でなければ ENOTSUP（vulkan-buffer-copy.c:395-396）。uncached を CPU が読むのは遅いので写しは 1 回、buffer の行から直に RAW の表。時間は p009。driver の copy は kernel の CPU の memcpy、WS141 の UIF 化の後の扱いも p009。
- L-8. 塗りは UI の thread を同期で止める（Pi の頁全体で数秒の見込み、推測）。p006 で「塗っています」の表示か時間の上限、と p005 に書く。
- L-9. 試験の不足: 失敗の経路（途中の ENOMEM、EIO）で帯の物と host の表が全部解放されること（ASan）、断った時に document・undo・journal が変わらないこと、線の上の tap、最も薄い橙の鉛筆の円、R < 4 の大きな頁（A0）。
- L-10. document.c から fill.c を呼ぶと WS079・WS175 の host 試験（run-notes-host.sh・run-host-notes-edit.sh）の source の一覧に足す必要（他の WS の file、Q1 の許可）。「触る file」に書くか document.c を fill.c に依らせない。
- L-11. 塗り替えは古い塗りを残したまま上に重ねる。file が大きくなり、塗りの操作で全部消える。置き換えは製品の選択なので Future Work の候補。
- L-12. 記録: ws.md の p005 は planned、phase.md は in-progress。p004 は test-wait なので p005 の依存は「main に入った p004 の出力（塊・遅延の組・notes_frame_stroke）」と書く。開いている text box の overlay と drag の preview は帯に入らないことを制限に。

## review-2 の指摘の解決

| 指摘 | 状態 |
| --- | --- |
| M-1・L-1 | 解決（view で組む、bounds で間引く）。時間の見積は p009（p001 §4.5 の 100〜300 ms は記録の直しが残る） |
| M-2 | 解決（ready の規則と F3） |
| M-3 | 解決（0.75 px + 0.1 pt）。z が上の時に裏目（本書 M-2） |
| M-4 | 一部（帯の高さと W の丸め、本書 M-7） |
| M-11 | 取り入れたが本書 S-2 の副作用 |
| L-4・L-7 | 解決 |
| L-18 | 一部（縁 1、本書 L-1） |
| review-1 の S5・M7・L3・L4 | 解決 |
| review-1 の S4（D7 (c)） | 本書 S-2 |

ユーザーの判断が要るのに技術判断で決めている点: S-2、M-6、M-2、L-11。
確かめていない点: i915 で finalLayout に TRANSFER_SRC_OPTIMAL が通るか、Venus、実際の時間と点の数、P1 の未 commit の実装。
