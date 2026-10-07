<!-- awesome-plan project=zedbsd record=ws175-p008 -->
# ws175-p008: Notes の UI（画像の段: Select の道具・画像の挿入と差し替え・削除・Reset。文字の段: Text の道具・編集の box・IME・font と size）

Parent: [WS175](../ws.md)
Status: cleared（2026-10-07 Q1 の判定: T1-333 で notes.pdf-edit-text が fail でなく、text box が「…liquor jugs ZEBRA」で終わる）（旧: in-progress（画像の段は 2026-10-06 Q1 判定で cleared（画面の resize の確認だけ p010 に残す）。文字の段（ws079-p017 と一つの作業）を 2026-10-06 P2 が実装、build warning 0 と host 試験 PASS。画面は p010 の T1））
Disposition: normal
Queue: Q1 の順（2026-10-06「画像の段の UI の p008 を先に、その後 p004・p005」、D6 (b)）
依存: [p007](../phase007/phase.md)（cleared）

## 範囲（design.md §7）

道具（Select）、選択と handle、移動と大きさの drag、画像の挿入（file chooser）と差し替え、削除・Reset、key、描画（編集の在る page の背景）、
menu、log [L9]、AAT の draft の手直し。文字の段（Text の道具・編集の box・IME [M7]・font の選択・Font/Size）は p004・p005 の後に、
[ws079-p017](../../ws079/phase017/phase.md)（Notes の IME 対応の text box）と一つの作業として行う（2026-10-06 Q1「同じ機能なので」）。

## 実装（2026-10-06 P2）

- **道具と toolbar**（`ui.c`・`app.h`）: Pen・Marker・Eraser の後に **Select**。Select の時は色と太さの代わりに **Image**（挿入）・**Replace**・**Delete**・**Reset**
  のボタン（押せない時は淡い）。design.md §7.2 の「物の上の操作の帯」は、文字を描ける toolbar の中のボタンにした（設計からの変更、下）。
  `notes_ui_state` に can_insert・selected・can_replace・can_reset。font の読み込み（`notes_ui_open`）は触っていない（P1 の q812）。
- **menu**（`menu.c`）: Edit に Insert Image…・Replace Image…・Delete・Reset、Tool に Select（radio）。状態で有効・無効。
- **入力**（`main.c`）: Select の press は選んだ物の handle（四隅から 12 px 以内）なら大きさ、物の上なら選んで移動、何も無ければ選択を外す。
  文字の行は選ばない（p004 の後）。読めない stream の page は「This page cannot be edited…」。drag の motion は shown space の写像（移動、または対角の
  隅を固定した拡大縮小。縦横比を保つ、Shift で縦横別、最小 4 pt・最大 page の 4 倍）、release で 1 つの変更（元の物は PLACED の変換の後に写像、挿入した
  物は置き方の後に写像）。押しただけ（動かない）は何も変えない。key: Delete・Backspace で削除、矢印で 1 pt（Shift で 10 pt）移動、Esc で選択を外す
  （無ければ今までどおり fullscreen を出る）。道具・page の変更、undo・redo で選択を外す。
- **画像の file**（新規 `picture-file.c`）: JPEG は frame header の大きさと成分、APP1 の EXIF の向き（`picture/picture.c` の
  `kl_picture_exif_orientation`、[L6][N18]）、bytes はそのまま。CMYK・YCCK は ENOTSUP [M15]。PNG は 8bit Gray・RGB で alpha・palette・tRNS・interlace
  が無ければ bytes のまま（libpdf が行を素通し）、それ以外は libpng-compat で RGBA に解いて圧縮（[N3]）。一辺 16384・64 M 画素を超えると E2BIG。
- **挿入と差し替え**: file chooser（題 Insert Image・Replace Image、Images の filter）。挿入は見えている page の部分の中央に、page の幅・高さの半分と
  72 dpi の大きさ以内（向き 5〜8 は縦横を入れ替え、§5.3）、Select の道具で選んだ状態にする。差し替えは選んだ物の flags に IMAGE。
  chooser の目的（PDF・挿入・差し替え）は chooser を開いた時だけ記録する（開いている間に別の目的で押しても上書きしない）。
- **描画**: 編集の在る page（と drag 中の page）の背景は page の editor の `pdf_page_editor_render`（Notes 自身の page は blank の editor）。
  `app_look`（文書の `edit_serial` と drag の preview の数）が変わると背景と page の絵を描き直す。drag の間は 100 ms ごとに editor に仮の置き方を入れて
  描き直す（design.md §7.3 の「物だけの texture を GPU で動かす」の代わり。下）。選んだ物の青い枠（2 px）と四隅の 8 px の handle を描く。
  drag を落とした時（abort）は editor を作り直して仮の置き方を捨てる。model 側は `edit.c` の put・take で `reshaped` と `edit_serial` を増やす。
- **log**: `NOTES TOOL N name=pen|highlighter|eraser|select`（eraser は `parts=` の後）、`NOTES EDIT select page= object= kind=image|graphic inserted= clipped=`、
  `NOTES EDIT move … dx= dy=`、`resize … sx= sy=`、`replace … image=WxH`、`insert page= kind=image object= image=WxH`、`delete`、`reset`、`undo|redo page= edits=`、
  `NOTES SAVE … strokes= edits= edited_pages= bytes= path=`、開いた後の `NOTES EDITS opened edits= edited_pages= rebased=0|1`。
- **build**: Notes の 3 つの Makefile に `picture-file.c`・`picture/picture.c` と libpng-compat・libjpeg-compat・libgif-compat、`platform/amd64/vmunix.mk` の
  Notes の link の規則にも同じ 3 つ（picture.c が libjpeg と libgif を参照する）。
- **AAT の draft**: `tests/scenarios/apps/notes/pdf-edit-image.md`・`pdf-insert-image.md` を実装の操作と log に合わせた（status は draft のまま、p010 で active）。

## T1-253 の結果と直し（2026-10-06）

- T1-253（AAT で手の操作、試料は edit-images.pdf）: Select・選択の枠・move・Replace・undo・redo・Delete・保存・Insert・先頭の bytes の一致は期待どおり。
  **resize は確かめられなかった**（Ctrl+Z の後に handle を drag すると move）。PNG は `/home/awe/zedBSD-worktrees/t1/build/t1-253m/`。
- 原因: undo・redo は選択を外していたので、Ctrl+Z の後は handle が無く、元の handle の所の press は物の hit（move）になった。
- 直し: undo・redo が編集の entry の時、その物（entry の後か前の状態の key か id）を `notes_page_object_index`（edit.c、新規）で editor の index に引き、
  Select の道具なら選び直す（消した物は選ばない）。host 試験に index の 2 項目（notes-edit 35/35 ×3）。画面での resize の確認は p010 か次の T1 で。

## 設計からの変更（Q1 に報告）

- 操作の帯（物の上に浮く帯）は toolbar の Select の時のボタンにした（文字を描く仕組みが toolbar にしか無いため。機能は同じ）。
- drag の描画は「物だけの texture を GPU で変換」ではなく、100 ms ごとに page を描き直す（CPU）。大きな page では drag が重くなりうる（p010 で確かめる）。
- `NOTES OPENED … edits= rebased=` は既存の `NOTES OPEN … kind= path=` を試験が完全一致で読むため、別の行 `NOTES EDITS opened …` にした。
- [M8] 指の long-press の drag は未実装。指は今までどおり scroll（toolbar の Finger を入れると 1 本の指が pointer と同じに選択・移動する）。

## 試験と結果（host、2026-10-06）

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws175/tests/run-host-notes-edit.sh <scratch>`（新規 `host-notes-picture.c` 12 項目を追加: EXIF の向き 6 の JPEG、RGB の PNG はそのまま、RGBA と palette の PNG は圧縮した RGBA、半透明、CMYK と text は ENOTSUP、無い file は ENOENT、各画像を挿入して page の editor が描く） | notes-edit 33/33・picture 12/12 ×3（plain・ASan・UBSan）、PASS |
| build: zedBSD の `bin/notes`（config-amd64-zdesktop、-Werror）、keiland-linux の `bin/notes` | warning 0 |
| `python3 plan/tools/style-check.py userland/desktop/notes/*.c` | 0 |
| `python3 plan/tools/aat/check-scenarios.py` | PASS |

## 文字の段（2026-10-06 P2、ws079-p017 と一つの作業）

Q1 の指示（2026-10-06）: 「IME は P1 が入れた libkeiland の kl_ui_window_input・kl_ui_window_text（KL 47）と kl_text_area を使えるなら使う。
Notes の font の読み込みの周りは避ける。model が書けない text を受ける件は、UI で editor に確かめてから確定する形に。」

- **編集の box**（新規 `box.c`）: libkeiland の widget を Notes の Vulkan の上に重ねる。既存の行は `kl_field`（1 行、Enter で確定）、挿入の文字は
  `kl_text_area`（複数行、Enter は改行）。kl_ui が描く canvas は renderer の新しい overlay の texture（`NOTES_TEXTURE_OVERLAY`、窓の大きさ、
  host が書く linear image、背景の texture と同じ作り）の上に作るので、widget の座標が窓の座標になり、pointer の入力と IME の caret の矩形
  （`kl_ui_window_text`）がそのまま合う。canvas は premultiplied なので、描いた矩形だけ straight alpha に直す。box の font は box.c が自分で開く
  （`keiland.ttf` と CJK の `keiland-fallback.ttf`。toolbar の `notes_ui_open`・`MAIN_FONT` は触っていない）。
- **入力の配管 [M7]**（`window.c`）: box が開いている間（`notes_window_box`）、key（repeat を含む。box の間だけ `kl_window_set_repeat(1)`）と
  `KL_WINDOW_TEXT_COMMIT/PREEDIT/DELETE` は box の queue だけに入り、pointer の motion・button（toolbar の帯の外）も box の queue に入る。main loop は
  それを `kl_ui_window_input` に渡して box の frame を描き（`notes_box_draw`）、frame の後に `kl_ui_window_text` で text input の on/off と caret の
  矩形を compositor に知らせる。toolbar の帯の press を box に渡さないのは、kl_ui が外の press で focus を外すため（Font・A-・A+・色を押しても
  打ち続けられる）。
- **Text の道具**（`main.c`、toolbar の Text、menu の Tool > Text、key T）: 頁の行の上の press はその行の文字を box で開く（行の下に出す。打つ間は
  100 ms ごとに頁の editor に `set_text` の preview を入れて描き直す。box を閉じると editor を作り直す）。挿入した文字の上ならその文字・font・
  size・色で開く。それ以外の頁の上の press は、release でそこに新しい文字の box（drag した横の幅が折り返しの幅、drag しなければ折り返さない）。
  toolbar は Font（名前、押すと Sans→Mono→Japanese、既存の行は Original も）・A-・「12 pt」・A+（0.5 pt 刻み、6〜144）と pen の 5 色。
- **Select の道具の文字**: 行と挿入した文字を選べる（青い枠と handle、移動・大きさ。文字の大きさの drag は Shift でも等倍）。double-click・
  toolbar の Edit・Enter で box を開く。toolbar は Edit・Font・A-・size・A+・Delete・Reset。Font は 1 つの変更（行の今の文字で font を替える）、
  A-/A+ は挿入の文字なら size、既存の行は左上を中心の等倍の拡大縮小。文字を変えられない行（TEXT_FIXED・INVISIBLE）は Edit が淡く、移動・削除だけ。
- **確定と editor の確認**（p004 の残り、Q1 の「model が書けない text を受ける件」）: Esc・Enter（行）・box の外の press・box を閉じる他の操作
  （道具・頁・保存など。Font・size・色・fullscreen は box を保つ）で確定する。確定の前に `notes_page_try_edit`（`edit.c`、新規）が page の editor に
  その状態を当て、editor の答え（`PDF_EDIT_TEXT_*`）を返して editor を作り直させる。ENOTSUP（font が無い）・MISSING（どの font にも無い字）・
  その他の失敗は model に入れず、box を開いたまま status に理由を出す（`NOTES EDIT text refused … error= result=`）。受けた時だけ
  `notes_document_edit_object` で 1 つの変更（undo できる）にする。文字を空にすると行の削除・挿入の文字の取り消し。変えていなければ何もしない。
  元の font に無い字で置き換えの font になった行は status で知らせる。box の間の Ctrl+Z は box の文字を開いた時に戻す（box の中の undo の代わり）。
  Notes を閉じる時に開いている box は確定を試み、editor が受けなければ捨てる。
- **log**: `NOTES TOOL 41 name=text`、`NOTES TEXT box open kind=line|inserted|new page= object= font= rect=x,y,w,h`、`NOTES TEXT box close`、
  `NOTES EDIT select … kind=text … fixed= text="…"`（先頭 40 byte）、`NOTES EDIT text page= object= kind= chars= font= fallback=`（挿入は ` size= color=`）、
  `NOTES EDIT font page= object= font= fallback=`、`NOTES EDIT size page= object= size=`、`NOTES EDIT text refused …`。
- **build**: Notes の 3 つの Makefile に `box.c`。
- **AAT の draft**: `pdf-edit-text.md`・`pdf-insert-text-font.md` を実装の操作と log に合わせた（Inter の記述を Sans（Mahora）に、ü の代わりに US 配列で
  打てる大文字。status は draft のまま、p010 で active）。

### 設計からの変更（Q1 に報告）

- §7.4 の「行の位置に置き換えの後の font・size で文字を描き、caret を Notes が重ねる」その場の編集ではなく、行の下に libkeiland の field（UI の
  font の 14 px）を出し、頁の行は 100 ms ごとの preview で打った文字を置き換えの font で見せる（Q1 の指示の kl_text_area・IME の部品を使うため。
  挿入の文字は box の中だけに出て、確定で頁に描く）。
- §7.2 の物の上の操作の帯は、画像の段と同じく toolbar のボタン（Edit・Font・A-・A+・Delete・Reset）。
- box の中の key は libkeiland の field・text area の key（US 配列の文字、←→、Home・End、↑↓（area）、Shift の選択、Backspace・Delete、Ctrl+A）。
  **Ctrl の語の移動、Ctrl+C・X・V（clipboard）、box の中の undo は無い**（libkeiland の widget に無い。Ctrl+Z は開いた時の文字に戻す）。
- [L8] 回転した文字の向きに沿った caret は無い（box は常に横書きの field）。

### 試験と結果（host、2026-10-06）

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws175/tests/run-host-notes-edit.sh <scratch>`（`host-notes-edit.c` に `notes_page_try_edit` の 5 項目: 行の新しい文字を試しても model と editor は変わらない、どの font にも無い字 U+0378 は MISSING、日本語は CJK で REPLACED、新しい文字の挿入を試しても頁に残らない） | notes-edit 49/49・picture 12/12 ×3（plain・ASan・UBSan）、qpdf ok、PASS |
| `sh plan/ws079/tests/run-notes-host.sh` | ok |
| build: zedBSD の `bin/notes`（config-amd64-zdesktop、-Werror）、keiland-linux の `bin/notes` | warning 0 |
| `python3 plan/tools/style-check.py userland/desktop/notes/*.c --summary` | 0 |
| `python3 plan/tools/aat/check-scenarios.py` | PASS（91） |

未実施: 画面（box の表示・IME の preedit と候補の位置・screen keyboard・preview・Select の文字の操作）は QEMU で未確認。p010 で T1（AAT の
pdf-edit-text・pdf-insert-text-font）。FreeBSD の Makefile は box.c を足したが build していない。

## 未実施・残り

- 画面の確認（Select・drag・handle・挿入・差し替え・削除・Reset・undo、保存と開き直し）: 未実施。QEMU は p010 で T1（AAT の pdf-edit-image・pdf-insert-image）。
- [M8] 指の long-press、[N13] 表示中の page の前後の外の editor を捨てること（今は各 page の editor を残す）、[M13] の autosave の cache と時間。
- 文字の段の画面の確認（上）、box の中の clipboard・語の移動・undo、回転した文字の caret [L8]、指で box の caret を動かすこと（指の tap は box に渡していない）、
  box の表示中の zoom・scroll に box が付いて動くこと（今は開いた時の窓の位置に留まる）。

## 積み残し

準正常系・異常系の未実装は [WS177 の P2 の一覧](../../ws177/backlog-p2.md)（2026-10-06 ユーザー「専用の1つのベータ2積み残しというWSに入れてください」）。

## 2026-10-07 T1-324 の直し（P2）

`apps.notes.pdf-edit-text` が単独でも fail（2 行目に " ZEBRA" を打った後、最後の字と Esc が届かず `NOTES EDIT text … fallback=1` の行が出ない）。
原因: libkeiland の `kl_ui_end` は、widget が 1 frame に 1 つしか取らない key の残りを次の frame に回し「もう 1 frame 要る」を返すが、
`notes_box_draw` はその返り値を捨て、main の loop は新しい box の入力が来た時にだけ box の frame を描いていた（速く打たれた key の残りが次の事象まで待つ）。
直し: `struct notes_box` に `again`（`kl_ui_end` の返り値）を足し、main の loop は `box_count != 0` か `box.open && box.again` で frame を描く（open・close で 0）。
aat-input の shift の疑いは不要（full の中で " ZEBRA" の最後の字と Esc 2 回が落ちたのも同じ原因）。確認: zedBSD の build（`build/p2-ci/bin/notes`）warning 0。
再試験は T1（Q1 に文面）。

2026-10-07 T1-331 の続き（P2）: fail は消えたが zebra-typed の PNG は「…jugs ZE」。log（`build/t1-331b`）では box の bytes が 43 の時に撮影し、その後 44・45 と
届いて最後は chars=45（全部の字が入った）。原因は helper の撮影が早いことと、box が 1 frame に 1 key しか取らず frame ごとに page の再描画が挟まって遅いこと。
直し: helper（`plan/tools/aat/scenarios/helpers_notes_edit.py`）は `NOTES TEXT box reported=… bytes=45` を待ってから撮る。main の loop は box の frame の後、
`box.again` の間 `MAIN_BOX_CATCH_UP`（32）回まで続けて box の frame を描き、窓を描く前に追い付く。zedBSD の build warning 0。
