<!-- awesome-plan project=zedbsd record=ws095-p017 -->

# ws095-p017: IME の辞書を /usr/share/keiland/ime へ移し、1 つの file にまとめる

Status: in-progress（q708-i01、P2 generation14、2026-10-05。実装・host 試験済み、QEMU（T1）待ち）
Disposition: normal
Parent: [WS095](../ws.md)
Queue: q708-i01（P2 generation14）

## ユーザーの指摘と決定（2026-10-04 夜〜2026-10-05）

- 指摘:「/usr/share/kei/imeというフォルダがありますが、/usr/share/keiland/imeにするべきと思いました。」
- 決定（2026-10-05）:「IME の辞書	/usr/share/keiland/ime への移動と、辞書を 1 つにまとめる。」

## 範囲

1. install の場所を `/usr/share/kei/ime/ja/` → `/usr/share/keiland/ime/ja/` に（`userland/desktop/ime/main.c:36-37` の path、`userland/desktop/ime/dict/Makefile`、`userland/desktop/keiland-linux.mk:151-152`・`keiland-freebsd.mk:152-153`、試験・文書）。
2. `SKK-JISYO.X`（REmacs の本体、約 387 KB）と `SKK-JISYO.kei`（Kei の補い、約千語）を **1 つの file** にまとめる（名前の案 `SKK-JISYO.ja`）。今の引く順（`.kei` を先に）を保つため、`.kei` の語を各見出しの候補の先頭に入れ、`.X` の候補を後ろに続ける（同じ候補は重ねない）。header には両方の出所と license（どちらも Awe Morris の著作、zlib。`.X` は REmacs の辞書を 2026-09-29 に zlib で再 license、WS095 D1・D3）を書く。まとめる script を tree に置き、まとめた結果を commit する（元の 2 file は tree から外す。履歴は git に残る）。
3. IME の load は 1 file だけを読む（補いの辞書の別の load をやめる）。利用者の辞書の場所は変えない。
4. WS154（SKK の IME）は Emacs の辞書を別に複写して持つ（ユーザーの決定、ws154）ので、この統合の影響を受けない。

## 受け入れ

- 変換の結果が統合の前と同じ（host の試験: ws095 の held-out の当たりの数が変わらない、`.kei` の語が先に出る）。3 OS の build warning 0。QEMU の IME の試験（T1）。C の全文の規約。

## 設計と結果（2026-10-05、q708-i01 P2 generation14）

- **1 つの file の形（範囲 2 の案から変えた点）**: 見出しごとに `.kei` の候補を `.X` の候補の前に混ぜる案は、分割（`ja-segment.c`）が「補いの辞書の見出し」を system の辞書の見出しより重く見る（同じ数の segment なら補いの語の分割を選ぶ、補いの送り仮名は日常の物、など）ので、混ぜると補いの区別が消えて変換が変わる。受け入れ（統合の前と同じ結果）を守るため、`SKK-JISYO.ja` は **2 つの部分**の 1 file にした: 全体の header（両方の出所と license）、補いの部分（前の `.kei` をそのまま）、行 `;; ==== part: system ====`、REmacs の辞書の部分（前の `.X` を header ごとそのまま）。SKK の道具には 1 つの辞書として読める（区切りは comment）。
- **load**: `ja_dict_load_parts`（`ja-dict.c`）が file を 1 回読み、区切りの行の前を補い、後を system の辞書として別々に index する（区切りの無い file は全部 system）。`ja_core_open` は補いの path が無い時（IME の本番）にこれを使い、補いを先に引く。補いの path を別に渡す API（host の試験が使う）は残した。IME（`main.c`）は 1 file（`KEILAND_DATADIR "/keiland/ime/ja/SKK-JISYO.ja"`）だけを渡す。利用者の辞書の場所（`~/.config/kei/ime/`）は変えていない。
- **install**: `/usr/share/keiland/ime/ja/SKK-JISYO.ja`（`dict/Makefile`、`keiland-linux.mk`・`keiland-freebsd.mk` の `share/keiland/ime/ja/`）。まとめる script `userland/desktop/ime/dict/merge.sh` を tree に置き、結果を commit、元の 2 file は tree から外した（履歴は git）。`userland/base/emacs/dict/SKK-JISYO.X`（REmacs の側、WS154 の SKK も別の複写）は触れていない。
- **確認（host）**: `plan/ws095/tests/measure.sh` で 100 文（97/100）・held-out（109/125）・held-out2（81/110）を「`.X`＋`.kei` の 2 file」と「`SKK-JISYO.ja` の 1 file」で流し、**各文の出力が byte で同じ**（`cmp`）。`plan/ws095/tests/host-engine.sh` に 2 つの部分の読み・区切りの無い file・engine が補いを先に引く check を足し、233 passed 0 failed。build: zedBSD の `bin/keiland-ime` warning 0（`ZEDBSD_CONFIG=plan/ws095/tests/config-amd64-ime.mk BUILD=build/p2-q703`）、Linux warning 0（`share/keiland/ime/ja/SKK-JISYO.ja` を install）、FreeBSD は方針で不要。design.md §9.1 に改めた点を書いた。
- **試験の依頼（T1、Q1 経由）**: image `plan/ws095/tests/build-ime-image.sh <BUILD>`、`plan/ws095/tests/ime-guest.sh start`、`plan/ws095/tests/ime-p004.sh`（kanji→漢字、watasi→私 の変換が新しい path の辞書で通る）。
