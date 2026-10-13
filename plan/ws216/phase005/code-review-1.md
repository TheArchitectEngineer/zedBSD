<!-- awesome-plan project=zedbsd record=ws216-p005-code-review-1 -->

# ws216-p005 code review-1（git diff a3215c5f7 e98624d0f -- userland/desktop/notes/、2026-10-13、Q1 が起動した review の担当）

**判定: ベータ2 の image（5330・Pi 4）に入れたままでよい。** 致命的 0・重大 0・中 2・軽微 6。塗りは UI から呼べない（main.c は notes_fill_page() をまだ呼ばない）。塗りの無い頁の消しゴムの結果は前の版と一致（乱数の種 3000 通りで erase_at・erase_parts_at・drag・undo・redo を混ぜ、stroke の id・道具・点・undo の数が全部一致）。render.c の変更で今の画面の描き方の結果は変わらない（render_pictures_ready() は toolbar の NULL の確かめが増えただけ、背景の LINEAR は画面では 1 対 1（main.c:2612）、Pi は linear tiling で FILTER_LINEAR を報告（bcm2711 vulkan-query.c:387）し filter の LINEAR も処理（vulkan-native-draw.c:568）、帯の物は塗りの時だけ）。file の形式は変わらない。host 試験 run-notes-fill.sh（plain・ASan・UBSan）・run-notes-render.sh・ws079 run-notes-host.sh を scratchpad の写しで流して ok。

## 中

**M1. 塗りの無い頁でも消しゴムが遅くなる（document.c:1849 stroke_near、1668 erase_decide）。** erase_decide() は種類が決まっていない間（drag が何にも当たっていない間と drag でない 1 回の呼び出しの全部）、全部の線の全部の線分の距離を毎回測る。stroke_near() には stroke_touches() の bounds の早い打ち切りが無い。塗りの操作の間も erase_hits() が全部の線に stroke_near()。測り（amd64 -O2、線 3000 本 × 100 点、インクの無い所の 1 回）: 前 0.019 ms → 後 2.4 ms（約 125 倍）、Pi 4 はその数倍の見込み、手書き約 200 語の A4 で 0.3〜0.5 ms。直し: stroke_near() の先頭に bounds ± DOCUMENT_ERASE_NEAR の確かめ（結果は不変）。

**M2. 小さな点を tap すると領域が空のまま輪郭化へ進み、桁あふれした大きさを確保しに行く（fill.c:343-361 → fill_back 1094 → 1198-1199）。** 0.75 pt ほどのペンの点（R=4 で 3×3 px）を tap → 周り 5×5 の多数決で seed の色が白、seed は境 → 閉じる半径 1.5・0.75・0.375・0 pt のどれでも narrow → 空のまま fill_back() → left=width・right=0 で box[2] < box[0]、box_width と行の数が uint32 で桁あふれ → calloc(約 2^64, 1) → ENOMEM。notice が "Could not fill the area"、log が reason=failed error=12（本来は ENOENT）、ASan では allocation-size-too-big で abort。scratchpad で再現（400×400 の白に 3×3 の黒い点、中心を tap → error=12）。document は変わらない。今は UI から呼べないのでベータ2 には出ない。p006 の前に直す: 最後の閉じる半径でも narrow か map.area == 0 なら fill_back の前に ENOENT。

## 軽微

1. 帯の幅を 2 か所で別々に計算: fill-page.c:100 の fill.width は 4096 で切らず fill.c:276 は切る。render.c:696 の memcpy は渡された幅を信じる。今は 4096 が 2 の冪で一致（page の幅 1024〜20000 の全 float で確かめた）が、片方が変わると host の帯の buffer を越えて書く。notes_fill_band に幅も渡すか fill-page.c でも 4096 で切る。
2. render_band_record() は host の絵の ready の flag を submit の前に 1 にする。submit が失敗すると絵は PREINITIALIZED のまま記録は GENERAL（画面の frame の道と同じ、device lost の時だけ）。
3. vkWaitForFences が timeout（10 s）か失敗でも fill-page.c:116 が notes_renderer_band_free() で使われているかもしれない物を捨てる。失敗の時は vkDeviceWaitIdle を挟む。
4. 「当たった所で覆われている」を中心に一番近い点だけで判定（document.c:1885 stroke_covered）。線の中心に一番近い所が塗りの下でも、円の中の別の所で見えている線は消えない。ユーザーの決定の文言とずれる所がある（塗りのある頁だけ）。
5. fill_page_stroke() は notes_document_add_stroke_at() が失敗しても next_id を進める。害は無い。
6. Pi 4 の帯はまだ動く見込みが無い（既知、p009）: bcm2711 の format_properties は depth・stencil の format を報告せず帯の stencil の image が作れない見込みで塗りは EIO。読み戻し先が uncached でないと ENOTSUP（L-7）とあわせて p009 の残件。

## 問題が無かった点

E2BIG・ENOENT・EINVAL の後に頁・undo・journal・next_id は変わらない。undo・redo は place を持つ entry で正しく、journal は place ごと書いて読み戻す。帯の同期（帯ごとの vkDeviceWaitIdle、pass の外への依存、COLOR_ATTACHMENT_OPTIMAL→TRANSFER_SRC と TRANSFER_WRITE→HOST_READ の barrier、fence、Pi が受ける finalLayout）。通常の frame との衝突なし（command pool は RESET_COMMAND_BUFFER つき、頂点は毎 frame 写し直し、fence は submit の前に reset）。fill.c・fill-page.c・render_band_make に解放漏れ・二重解放なし（ASan も通る）。大きな頁・細長い頁で size_t・uint32 の掛け算はあふれない（最大 16 Mi px、帯は 4096 で切る）。
