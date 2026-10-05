<!-- awesome-plan project=zedbsd record=ws166-p001 -->

# ws166-p001: 予測変換の要件と設計

Status: in-progress（2026-10-05、P2。設計を書いた。ユーザーの判断の 4 点待ち）
Disposition: normal
Parent: [WS166](../ws.md)
Queue: q737（Q1、2026-10-05）

## 今の構成（2026-10-05 に source を読んだ）

- 日本語の engine（WS095、`userland/desktop/ime/ja-*.c`）は Windows の IME の操作（2026-09-29 ユーザー）。読みを打つ間（composing）は Space・変換・Enter・Esc・Backspace だけを使い、**Tab と矢印は空いている**（`ja-keys.c` の `keys_composing`）。変換の後（converting）は Space・矢印・数字・PageUp/Down で候補と文節を動かす。
- 候補は `struct ime_output` の `candidates`（最大 40）で program の候補の窓（`popup.c`）に出る。
- 辞書は SKK の形式（`SKK-JISYO.ja`、約 1.8 万行）を hash の表で引く（`ja-dict.c`）。前方一致の検索の索引は無い。
- 利用者の辞書（`ja-user.c`）は読み → 選んだ候補（最大 8、新しい順）と時刻（stamp）を最大 1 万の読みで持つ。
- SKK の engine（WS154）は ddskk の操作。補完（Tab）は WS154 で後回しの候補（ユーザーの判断待ち）。
- WS098（ニューラル化）はブロック中。

## 要件（案）

1. 日本語の engine で、読みを打っている間（変換の前）に、その読みで**始まる**語の候補を候補の窓に出す（前方一致の予測、Windows の IME・スマートフォンの予測と同じ）。
2. 候補の源は、(a) 利用者が確定した語の履歴（利用者の辞書の読みと候補、新しい順）、(b) 辞書の見出しの前方一致。(a) を先に、(b) を後に並べる。
3. 打つのを邪魔しない: 予測は自動で窓に出るが、選ばない限り何も変わらない（文字を打ち続ける・Space で普通の変換・Enter でかなのまま確定は今と同じ）。
4. 秘密の欄（password・PIN・hidden・sensitive）では予測も履歴も使わない。

## 設計（案）

- **D1 索引**: 辞書を読む時に、okuri-nasi の見出しを読みの順に並べた配列（`ja_dict` に `sorted`）を作り、二分探索で前方一致の範囲を出す（1.8 万の見出しで 8 byte × 1.8 万 ≈ 150 KB）。利用者の辞書は 1 万以下なので線形に見る。
- **D2 並べ方**: 利用者の履歴（新しい順、同じ語は 1 度）→ 辞書（読みの短い順、同じ長さは辞書の順）。最大 9 個（候補の窓の 1 頁）。読みが今の読みと全く同じ語は、普通の変換と重ならないよう後ろに回す（案）。
- **D3 出す時**: 読みが 2 かな以上の時（1 かなでは多すぎる）。key ごとに出し直す（索引の二分探索なので軽い）。
- **D4 選び方（Windows の IME と同じ）**: 予測の窓が出ている時、Tab か ↓ で予測の 1 つ目に入り、Tab・↓・↑ で動き、Enter で選んだ語を確定、Esc で予測から出る（読みに戻る）。数字 1〜9 で直接選ぶ。予測に入っていない時の key は今のまま。
- **D5 学習**: 予測で確定した語も、普通の変換と同じく利用者の辞書へ（読み → 語）。履歴の別の file は作らない。
- **D6 窓**: 今の候補の窓を使い、予測であることを示す（見出し「予測」か色、`struct ime_output` に `predicting` を足す案）。
- **D7 設定**: Settings の Languages の頁（WS154）に「予測変換」の switch（`ime.predict`、既定 on）。IME の program は起動の時に読む（WS154 の `--method` と同じく compositor が引数で渡し、変わったら起動し直す）。
- **D8 WS098 との関係**: 並べ方（D2）を 1 つの関数にし、後で言語 model の評価に置き換えられるようにする。WS098 には依らない。

## Phase（案）

| Phase | 内容 |
| --- | --- |
| p002 | engine の側: 辞書の前方一致の索引、予測の候補の生成と並べ方、composing の時の key（Tab・↓・↑・数字・Enter・Esc）、学習。host の試験（`host-engine.sh` に足す）。 |
| p003 | 窓の表示（予測の印）、設定の switch（`ime.predict` と Languages の頁、compositor の引数）。 |
| p004 | T1（QEMU、キーの注入で予測の出方と確定）、実機の UAT、全文の規約。 |

## ユーザーの判断が要る点（Q1 経由）

1. 選び方は D4（Windows の IME と同じ、Tab・↓ で予測に入る）で良いか。
2. 既定は on で良いか（Languages の頁で切れる）。
3. 確定の後に「次の語」を出す予測（前の語から次を出す、bigram の履歴）を入れるか。案は今回は入れない（前方一致だけ）。
4. SKK の engine にも予測（ddskk の補完に近い）を入れるか。案は今回は日本語の engine だけ（SKK は WS154 の補完の判断と合わせる）。
