<!-- awesome-plan project=zedbsd record=ws071p016 -->

# ws071-p016: タブをメインのペインが持つ・タブらしい見た目

Phase ID: `ws071-p016`
Parent: [WS071](../ws.md)
Status: cleared（2026-09-27、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行）

## 範囲

2026-09-27 ユーザー:「ファイラーのタブは、右側のコンテントペインが所有するのがいいと思うなあ。」「タブは複数あるときだけ表示することにしよう。」
「タブのデザインが若干ボタンっぽいので、タブっぽくしてほしいですね。もっとも、主観的なものですが。」 → [p013](../phase013/phase.md) の続き。

- tab bar は窓の上端の全幅ではなく、content（メイン）のペインの上端だけに置く。sidebar と preview は動かない（タブが切り替えるのは
  content の中身）。タブが 2 つ以上のときだけ出す（p013 のまま）。
- 見た目をボタンからタブへ: pill を並べる代わりに、
  - 表示中のタブは content のペインと同じ面（白）で、ペインの上端に付き、間に線が無い（上の角は丸く、下の両端は外へ反る小さな「足」で
    ペインの上端の線へ流れ込む。タブの面がペインの縁の 1 px を覆う）。文字は太字。
  - 他のタブは窓の地の上の静かな label（面なし、文字は薄い灰）。pointer の下では半透明の白の面（上の角が丸い）。
  - 静かなタブ同士の間に細い縦の区切り（表示中・hover のタブの隣には出さない）。
  - 各タブの右端に × の close（静かなタブは薄く）。
- タブは content のペインの左端から 20 px（ペインの角丸の外）から並べ、幅は 96〜220 で等分。

## 受け入れ

1. host の画面（3 つ、hover、preview と dashboard）と host-p013 の試験（座標を新しい配置に）。
2. Venus（QEMU）で files-p013 が通る（座標を新しい配置に）。画面を撮る。
3. warning 0、`style-check.py` 0（変えた file）、回帰 files-regress PASS、boot test PASS。

## 実装（2026-09-27）

- `userland/base/zdesktop-files/ui-tabs.c`: `fm_tabs_layout` は content の上端の bar（高さ 34）を置き、content だけを下げる。
  `fm_tabs_draw` は content を描いた後に呼ぶ（表示中のタブがペインの縁を覆うため）: 静かなタブ → 区切り → 表示中のタブの順。
  タブの輪郭は多角形（上の角の 1/4 円と足の 1/4 円、`tabs_outline`・`tabs_arc`）で、縁の色で塗った輪郭の内側を 1 px 内へ寄せた白で塗る。
- `ui.c`: `ui_layout` は sidebar と preview を先に置き、content の上に `fm_tabs_layout`。`fm_ui_draw` は content の後にタブ。
- 試験: `host-p013.sh` の bar の click を (300,30)・(457,30) に、`files-p013.sh` の Downloads の click を (100,155)（sidebar は動かない）、
  タブを (300,30)、× を (457,30) に。

## 検証（amd64 だけ）

- host: `host-build.sh`（-Werror）通る。`host-p013.sh` 16/16 ok。画面 `build/ws071-p016-host/{hover,preview-home,preview-list,zoom}.png`。
- guest（QEMU、Venus、lean image `build-files-image.sh build/amd64`、zdesktop-files の warning 0）: `files-p013.sh` PASS。
  画面 `build/ws071-p016/{two,three,closed}.png`。
- 回帰（同じ image）: `files-regress.sh`（p002・p003・p004・p005・p006・p007・p012・p008・p014・p013）PASS。boot test PASS
  （`build/ws071-p016-boot/login.png`）。
- 規約: `ui-tabs.c`・`ui.c` の style-check 0。
- 実機（i915）: 未実施。

画面（ユーザー向けの写し）: `/home/awe/zedBSD-rpi4/build/ws071-shots/p016-20260927-{host-three-hover,host-preview-list,host-preview-home,host-zoom,venus-two,venus-three,venus-closed}.png`。

## 残り

- 見た目は主観なので、ユーザーの反応で色・足の大きさ・区切りを直す。
- ws071-p015（すりガラスの付箋）でタブの面も glass のタブにする（静かなタブも glass の面を持つ）。
- 2026-09-27 追記: この見た目はユーザーの反応で ws071-p015 で作り直した（content の card の中の等幅の行、中央の名前、選択は青い文字・下線・明るい地）。
