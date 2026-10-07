<!-- awesome-plan project=zedbsd record=ws157-p001 -->

# ws157-p001: 写真の app の要件と設計

Status: cleared（2026-10-07 q835: ユーザーの要件（§1）で書き直した。D2 と D3・D5 はユーザーがクリックで選択（「月ごとの TSV＋album ごと」「複写、同じ中身は取込まない」）、D1・D4・D6・D7 と Phase の組み替えは Q1 が了承）
Disposition: normal
Parent: [WS157](../ws.md)
Queue: q835（2026-10-07、P2）

## 1. ユーザーの要件（2026-10-07、原文）

「~/Pictures/Library 以下で管理します。取り込み機能ありです。データベースあり、サムネイル管理あり、フォルダ名は img/year/month/day で、ファイル名は維持。
アルバムはデータベースのメタデータでリンクを管理。直接ファイルを置くのではなく、取り込みで管理。データベースはクラウドsyncしやすい形式（サイズが小さい、分割されてる）。」

これで 2026-10-07 の最初の既定案（~/Pictures を読むだけ、album は folder、印は photos.conf。p002・p003 で実装）は置き換え。p002・p003 は canceled、code は p004・p005 で作り直す。

## 2. 要件の読み替え

| # | 要件 | 設計 |
| --- | --- | --- |
| R1 | ~/Pictures/Library 以下で管理 | library の root は `~/Pictures/Library`。その下に `img/`（写真の file）と `db/`（データベース） |
| R2 | 取り込みで管理（直接置かない） | 写真は取り込みだけで library に入る。`img/` に人が置いた file は一覧に出ない（db の行が無いので） |
| R3 | 取り込み機能 | File > Import...（folder か file を選ぶ）と `photos --import PATH`。folder は中まで探す |
| R4 | `img/year/month/day`、file 名は維持 | `img/YYYY/MM/DD/<元の名前>`。日付は撮影日 |
| R5 | データベース、album は db の metadata で link | 写真の行と album の file（写真の id の一覧）。album は file を複写・移動しない |
| R6 | sync しやすい（小さい、分割） | 写真の行は撮影の月ごとの file、album は 1 つ 1 file、行の text。変わった file だけが送られる |
| R7 | サムネイル管理 | 縮小画像を disk に持ち、次からは decode しない |

## 3. 設計

- **D1 置き場**: `~/Pictures/Library/img/YYYY/MM/DD/<元の名前>`。日付は EXIF の DateTimeOriginal（無ければ DateTime、無ければ file の mtime の地方時）。
  同じ名前で中身の違う file が既にあれば `名前-1.jpg`、`名前-2.jpg`…（元の名前は db の行に残す）。
- **D2 データベース**（ユーザー選択）: `~/Pictures/Library/db/`、UTF-8 の行の text（1 行 1 記録、欄は tab で区切る）。
  - 写真: `db/photos/YYYY-MM.tsv`（撮影の月ごと。1 行目 `# keiland-photos 1`）。欄: id、path（root からの相対）、SHA-256、大きさ、撮影日、取り込み日
    （`YYYY-MM-DDTHH:MM:SS`）、幅、高さ（未知は 0）、お気に入り（0・1）、回転（右回りの 1/4 の数 0〜3）、元の名前。
  - album: `db/albums/<album の id>.album`（1 album 1 file。1 行目 `# keiland-photos-album 1`、`name<TAB>名前`、`photo<TAB>写真の id` の行）。
  - 書き込みは一時 file と rename。変わった月・album の file だけを書く。tab・改行を含む名前の file は取り込まない（backlog）。
  - 理由: 1 枚 1 file では file の数が多すぎ（数万）、1 つの file では 1 枚の印の変更で全体が送られる。月は取り込みと編集の単位に近い。
- **D3 id と重複**（ユーザー選択）: 写真の id は中身の SHA-256 の先頭 32 字（16 進）。重複は SHA-256 の全体で判定し、同じ中身は取り込まない（数を知らせる）。
  album の id は 16 byte の乱数の 16 進。
- **D4 縮小画像**: `$XDG_CACHE_HOME/keiland/photos/<写真の id>.ppm`（無ければ `~/.cache/...`。P6、160 の正方形、回転は表示の時に掛ける）。作り直せる物なので
  library（sync の対象）には置かない。decode は今は Photos の中（背景の thread）。WS168 の keiland-preview に任せるのはその p004 の後。
- **D5 取り込みの流れ**（ユーザー選択）: 選んだ file・folder（深さ 4、隠し folder は見ない）の JPEG・PNG・GIF ごとに: SHA-256 → 重複なら飛ばす → 日付 →
  置き場（D1）→ **複写**（元の file は残す）→ 行を足す。終わったら変わった月の file を書き、件数（取り込み・重複・失敗）を出す。
- **D6** `~/Pictures` の直下の file・folder はもう見ない。お気に入り・回転は db の行に（photos.conf は使わない）。
- **D7 album の操作**: Photo > Add to Album...（album の一覧と新しい album の名前の欄の card）。左の一覧に album（名前と枚数）。

範囲外（backlog・Future）: album からの除去・album の名前の変更と削除、写真の削除、取り込みの移動、camera・SD・USB の自動の取り込み（WS132）、
db の sync の衝突の合わせ込み（2 台で同じ月を変えた時）、場所・人、HEIC・RAW、編集。

## 4. Phase

- p002・p003（最初の既定案の実装）: canceled（2026-10-07 ユーザーの要件で置き換え）。
- [p004](../phase004/phase.md): library・db・取り込み（`photos.h`・`library.c`・`db.c`・`import.c`・`exif.c`）と host 試験。
- [p005](../phase005/phase.md): app（取り込み・album・縮小画像の cache、AAT）、T1。
