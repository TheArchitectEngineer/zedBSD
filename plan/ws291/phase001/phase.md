<!-- awesome-plan project=zedbsd record=ws291-p001 -->

# ws291-p001 desktop の計測と 60 fps

- 親: [WS291](../ws.md)。
- 状態: in-progress（2026-10-26 23:40 P2、q1595-i01）。既に 60 fps、直す所は無し（下の結果）。T1 に頼む試験は無い（実機の計測はこの WS の本体で担当が直に行う）。

## 範囲

1. main の amdrdna（WS039 の p006・p009・p012 が main に入っているかを先に確かめる）で amd64 の vmunix と desktop の package を build し、10.0.10.3 の USB の zedBSD に入れて BootNext で起動。
2. frame-ops.sh の段（idle・Terminal の起動・type・keys・output・scroll・hover・drag・App Home・Wiseview）を流し、frame-times.py で段ごとの fps・work・period を表に。
3. 60 fps に届かない段は、Keiland の --log-frames と driver の時間（submit・fence・flip の待ち）で原因を分け、amdrdna の driver（src/drivers/gpu/amdrdna/）か Keiland の直せる所を直す。既に 60 fps なら記録して終える。

## 受け入れ

- 全段 60 fps（または届かない段の原因と残りを記録）。前後の数値、build の warning 0。

## 記録

- 2026-10-26 23:05 Q1: 新設。
- 2026-10-26 23:40 P2（q1595-i01）: 計測と記録。

## 結果（実機 10.0.10.3、RX 5600 XT、zedBSD c8ba529、1920x1080 60 Hz）

image: `plan/ws291/tests/config.mk`（CI の amd64 ＋ amdrdna の組み込み・control・Navi10 の firmware・aat-input・keiland-shot・mview）、
`tests/session.sh`（Keiland に `--log-frames`）、`tests/autologin`（kei）。build/ws291/usb/hdd-image.img（vmunix の sha256 の先頭 fc152ecc5fa95d6c、
zedBSD の source の warning 0）を USB（KIOXIA）に書き、UEFI の Boot0005「zedBSD USB ws291」を足して BootNext で起動（BootOrder は不変）。

frame-ops（`plan/tools/frame-ops/frame-ops.sh`、起動直後の session、frame-times.py の表、時間は ms）:

| 段 | frames | work p50/max | submit p50/max | period p50/max | fps |
| --- | --- | --- | --- | --- | --- |
| launch（Terminal の起動） | 55 | 6/15 | 6/13 | 16/93 | 59 |
| type | 57 | 2/9 | 2/8 | 17/95 | 58 |
| keys（200 ms 間隔） | 22 | 2/3 | 2/2 | — | — |
| output | 14 | 2/7 | 2/3 | 15/63 | 55 |
| scroll | 34 | 3/4 | 2/3 | 51/53 | 20（入力が 50 ms 間隔） |
| hover | 15 | 8/14 | 2/8 | 33/83 | 23（入力が 15 ms ＋ aat-input の起動） |
| drag | 175 | 4/12 | 4/8 | 17/88 | 60 |
| home（App Home） | 66 | 6/15 | 5/14 | 17/92 | 59 |
| wiseview | 58 | 7/10 | 7/8 | 17/17 | 66 |

- 全段で work（frame の描画）が 16 ms 以下（max 15 ms）、work>16 の frame は 0。動きのある段は period p50 16〜17 ms（60 fps）。scroll・hover の fps は試験の入力の間隔で決まり（ws270-p007 と同じ）、描画の速さではない。period の max 83〜95 ms は段の切れ目（入力の休み）。
- 見た目: keiland-shot の PNG（`build/ws291/measure/` の shot、Settings の初回の窓と Terminal、正常）。
- 判定: desktop は既に 60 fps。amdrdna・Keiland に直す所は無し。
- 2 回目（Terminal を開いたままの session で続けて流した）は launch・scroll・home の段が成り立たない（Terminal は新しい窓を出さない、wheel の先が違う）。frame-ops は起動直後の session で流す: `measure.sh desktop` は既定で zedBSD を再起動してから流す。

## 計測の自動化（Master の Tools の候補）

`plan/ws291/tests/measure.sh`（2026-10-26 Q1 の依頼「計測は 1 本の script に」）: host から 1 回の呼び出しで、どちらの OS が動いているかの判定、
Debian からの BootNext と zedBSD の IP の待ち（MAC から ip neigh、無ければ subnet の ping の sweep）、zedBSD への file・kernel の配置と再起動、
frame-ops の実行・log の回収・frame-times.py の表、mview の実行と `KWL SCANOUT` の行の抜き出しをし、要約だけを出す。長い log は OUT に残す。
RDNA で確立し、NVIDIA・PowerVR の板には MAC・subnet・Debian・boot の項目の環境変数を変えて使う見込み。
