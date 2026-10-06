<!-- awesome-plan project=zedbsd record=ws120-p001 -->

# ws120-p001: 設計

Parent: [WS120](../ws.md)
Status: cleared（2026-10-07 q831 P2、ユーザーの決定で範囲を改めた。下の「2026-10-07 の決定と設計」）
Disposition: normal
Queue / attempts: none
Goal: 音楽アプリの設計（`plan/ws120/design.md`）を作り、D1〜D4 の選択肢をユーザーに提示して決定を記録する。
Prerequisites: なし
Investigation bound: 3 時間。code は書かない。

## 範囲

- D1（形式）・D2（decoder の方針）・D3（再生の API と他 OS）・D4（機能）の選択肢・利点・危険・工数を、ws.md の既定案を出発点に design.md にまとめ、main 経由でユーザーに提示する。
- app の構成: `userland/desktop/music/`（名前は案）、libkeiui の部品（canvas・titlebar・menu・list・touch）、画面の案（一覧・再生の bar・cover）、keyboard の操作、Files からの起動の引数。
- 再生の API の案（libkeiland）: stream の open（format・rate・channels）、write（非 block、poll できる fd）、pause・resume・flush・drain、再生位置（audiod の `played_position`）、underrun の通知、audiod の切断からの回復。`KEILAND_VERSION` と exports.map。
- decoder の library の案: 置き場所（例 `userland/base/libaudio-decode`）、API（open・情報・metadata・frame ごとの decode・seek）、format ごとの file の分け方、cover の画像を compat の library に渡す方法。
- 試験: 素材の作り方（host に sudo で flac・lame・vorbis-tools を導入し、正弦波・無音・境界の長さを encode、参照の PCM を作る）、M5 の数値（MP3 の許容差）、QEMU の WAV の判定。
- [設計の敵対的レビュー](../../../.claude/agents/design-reviewer.md) の方針に従い、design.md の誤り・欠落を見直す。

## 受け入れ

- design.md があり、D1〜D4 のユーザーの決定（またはユーザーが既定案を承認したこと）と出典が書かれている。p002〜p006 の範囲・所有 path・試験が決まり、それらを planned にできる。

## 検証

文書の review（link・ID）。build・試験は無い。

## 所有 path

`plan/ws120/`。

## 依存・未決の判断

依存なし。D1〜D4 はこの Phase で提示し、決定が来るまで p001 は uncleared で待つ（他の作業は止めない）。

## 2026-10-07 の決定と設計（q831、P2）

ユーザーの決定（Q1 経由、クリック、2026-10-07）:
- (D-AAC)「今は libavcodec、独自は後」: ベータ2 の AAC の decode は videoplayer（WS122 p004）と同じ libavcodec の dlopen の add-in。独自の AAC-LC の decoder は後の Phase（Huffman の表などの出典をユーザーと決めてから）。
- (D-SVC)「その機能はベータ4へ。今は外部サービスのアイコンは権利の関係でいらないです。」: 既存の音楽 service との連携はベータ4。外部の service の icon・名前を app に出さない。
- 形式は 2026-10-02 の決定どおり m4a（MP4 の container の AAC）だけ。WAV・FLAC・MP3・Ogg の旧い計画（p002〜p006）は取り下げ（canceled）。

設計（正常系）:
- app `userland/desktop/music/`（package `music`、窓の題「Music」）: 
  - `tags.c`: MP4 の moov の mvhd（長さ）と udta/meta/ilst（©nam・©ART・aART・©alb・trkn・covr）を読む（mediafile は metadata を持たないので app の中に小さく）。
  - `library.c`: `~/Music` を深さ 4 まで見て `.m4a`（と `.mp4` の音だけの物）を集め、album（aART か ©ART と ©alb）ごとにまとめ、track の番号の順。
  - `play.c`: 再生の thread。mediafile で音の track を開き、videoplayer の `codec.c`（libavcodec の add-in）で decode、`audio.c`（audiod の client）へ 48 kHz・2ch・16 bit で書く。再生・一時停止・seek・次の曲（album の続き）。時刻は audiod の読みの位置。
  - `view.c`: 左に album の一覧（cover・題・artist）、右に選んだ album の曲（番号・題・長さ）、検索、下に再生の bar（cover・題・artist・前・再生/一時停止・次・位置の slider・時間）。libavcodec が無ければ「Playing needs libavcodec」。
  - `main.c`: kl_app の loop。引数の file（Files から）を開いて再生（`~/Music` の外の file も一覧に足す）。
- 音の source（videoplayer/audio.c・codec.c・bitstream.c、mediafile）は music の package でも compile する（共有の library にするのは後）。
- Files: `audio/mp4` を Music で開く（apps.c の built-in に 1 行）。App Home に「Music」。
- 試験: host で tags と library（試験の m4a は python で MP4 の箱を組んで作る。音の中身は要らない）、view の PNG。QEMU（T1）: `~/Music` に置いた m4a の再生（libavcodec の package の入った image、audiod の log と再生の位置）。
