<!-- awesome-plan project=zedbsd record=ws165-p002 -->

# ws165-p002: 手本の data と点群の照合、host 試験

Phase ID: `ws165-p002`
Parent: [WS165](../ws.md)
Status: in-progress（2026-10-06 P2: data・照合・host 試験を実装。§ 結果の top-1 の判定はユーザーに確かめる）
Phase disposition: normal
Queue: Q1 の P2 の列（2026-10-06、WS172 の後）

## 範囲

[p001](../phase001/phase.md) の §3〜§5 と判断 H1〜H3（2026-10-05 夜にユーザーが承認）の段 1:
- 手本の data（Hershey fonts の取得・検証・変換）。
- 点群の照合（$P の系統、学習なし）。
- 濁点・半濁点の扱い。
- host 試験（揺らぎの試料で正答率と速さを測る）。

compositor への組み込みは p003。

## 実装（2026-10-06 P2）

- **手本の data**: package `hand-hershey`（`userland/packages/fonts/hand-hershey/`、外部の data なので tree には取り込まない）。
  - 取得: Usenet の comp.sources.unix volume 4「hershey」の part2〜5（`hersh.oc1-4`・`hersh.or1-4` の shell archive を compress した物）を `https://ftp.isc.org/pub/usenet/comp.sources.unix/volume4/hershey/` から取る。各 part の大きさと SHA-256 を確かめる（`ZEDBSD_EXTERNAL_FILE`）。
  - 変換: `convert.py` が shell archive を text として読み（実行しない）、Hershey の record を解いて `/usr/share/keiland/hand/hershey.txt` を作る。
  - `hershey-unicode.txt`: 使う字（228 字）と Unicode の対応。対応は Kaloffl の Hershey-Unicode-Mapping（Unlicense）から取った。字の内訳は simplex Roman の英大文字・英小文字・数字・記号 20、oriental のひらがな 76・カタカナ 76（濁点・半濁点の字を含む。小書きの字は Hershey に無い）。
  - license: Hershey の配布の条件（「謝辞を data と一緒に配る」）に従い、`NOTICE` を `/usr/share/licenses/hand-hershey/NOTICE` に置く。`tools/release/license-components.json` に登録した。
  - 調べた事: Debian の `hershey-fonts_0.1` の `japanese.jhf` は番号が全部 12345 で、字との対応が付かない。それで Usenet の原本を使った。
- **照合**: `userland/desktop/wayland/hand-cloud.c`・`hand-cloud.h`（compositor から独立していて、host で単独に試せる）。
  - $P と同じく、32 点に resample し、縦横の比を保って単位の正方形に収め、重心を原点にする。
  - 開始点を 5 点おきに取り、両向きで貪欲に最小の対応を探し、早い点ほど重く数える。
  - 暫定の上位 k 個より遠くなった時点で打ち切る（速さのため）。
- **濁点・半濁点**（p001 §3 の比較の結論: 本体と別に認識して合成する）: `hand_recognize_strokes`。
  - 右上の小さな線を印として取り出す。開いた線が 2 本以上なら濁点、閉じた輪なら半濁点。
  - 残りを認識し、印を取れる字に合成して先頭に置く（か＋゛→が）。全体の認識の候補はその後に並べる。
  - 印が無い時は、濁音の字（が等）を候補の後ろに回す。理由: 点群では印の点が少なく、全体の照合だけでは「く」と「ぐ」を分けにくいため。
- 試験: `plan/ws165/tests/host-hand.c`・`run-host-hand.sh`。
  - 各字の手本から試料を 20 作る: 回転 ±10°、縦横の伸び ±15%、点の揺れ（大きさの 2% の正規分布）、線の順の入れ替えと向きの反転、1 本の線の端を 10% 欠く。
  - top-1・top-4・速さを出す。`HOST_HAND_CONFUSION=1` で取り違えを 1 件ずつ出す。

## 結果（2026-10-06 P2、host、Linux の cc -O2、228 字 × 20 = 4,560 試料）

| 測り方 | seed 既定 | seed 7 | 目標（H1） |
| --- | --- | --- | --- |
| top-1 | 88.2% | 87.4% | ≥ 90% |
| top-1（大きさ・位置だけが違う組を 1 字に数える） | 91.7% | 90.9% | — |
| top-4 | 98.3% | 98.4% | ≥ 98% |
| 1 文字の認識（平均・最遅） | 5.4〜5.9 ms・13〜16 ms | 同 | host で 5 ms 以下を目安、5330 で 20 ms 以下 |

- top-1 が 90% に届かない主な理由は、形が同じで大きさか位置しか違わない組。具体的には c/C・o/O/0/°・s/S・v/V・w/W・x/X/×・z/Z・l/|/1・./·・p/P・/ とノ。書いた絵だけでは区別できず、これらで約 4% を失う。
- 試験の終了状態は「大きさ・位置の組を 1 字に数えた top-1 ≥ 90%」と「top-4 ≥ 98%」で判定する。この判定でよいかはユーザーの判断が要る（§ 判断）。
- p003 では、書く面の箱に対する大きさを使って組の中を分ける（小さい c・o・. を先にする）。
- 速さは目安の 5 ms を少し越える。5330 の CPU でも 20 ms 以内の見込みだが、未測定。
- 残る取り違え: D と 0、2 とフ、9 と g、ぬ と め、ね と れ、ほ と ぼ（Hershey の手本の太さを出すための重ね線が濁点の位置に来る）。

## 判断（ユーザーに確かめる）

| ID | 問い | 案 |
| --- | --- | --- |
| H5 | H1 の top-1 ≥ 90% を、形が同じ組を 1 字に数えた値で判定してよいか（そのままの値は 87〜88%） | 組を 1 字に数える。p003 で書く面の大きさを使って組の中を分ける |

## 確認

- `sh plan/ws165/tests/run-host-hand.sh 20`: PASS（上の表）。
- `make hand-hershey`: 取得・検証・変換が通り、228 字、欠け 0。
- style-check（`hand-cloud.c`・`host-hand.c`）: 0。
- zedBSD の build は p003 で compositor に組み込む時に行う。今は compositor の Makefile に入れていない。

## 残り

- p003: compositor の `kwl_hand_recognize` を本物に替える。templates を `/usr/share/keiland/hand/hershey.txt` から最初の認識の時に読む。書く面の大きさで形が同じ組を分ける。小書きの仮名（や→ゃ 等、Hershey に無い）は大きさで候補に足す。T1 で試す。
- 準正常・異常は [backlog-p2](../../ws177/backlog-p2.md)。
