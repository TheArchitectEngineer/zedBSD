<!-- awesome-plan project=zedbsd record=ws074p020 -->

# ws074-p020: `libjpeg-compat` 2: progressive、CMYK/YCCK、`jpeg_save_markers`

Phase ID: `ws074-p020`
Parent: [WS074](../ws.md)
Status: planned
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: ws074-p019

## 範囲（正常系のワンパス）

- progressive の Huffman（SOF2、T.81 G.1.2）: DC の最初と細かくする scan、AC の最初（EOBRUN）と細かくする scan。係数を
  component ごとの block の配列に貯め、最後の scan の後にまとめて逆 DCT する。量子化の表は component が最初の scan に出たときに
  覚える（libjpeg の latch と同じ）。restart は EOBRUN も戻す。
- 4 component: CMYK（Adobe の transform 0 か Adobe 無し）と YCCK（transform 2）。出力は `JCS_CMYK`（YCCK は libjpeg の
  式で CMYK に）。CMYK から RGB への変換は libjpeg-turbo にも無いので作らない（browser の側で行う、p021）。
- `jpeg_save_markers`: APPn と COM の segment を `marker_list` に残す（長さの上限付き、JPOOL_IMAGE）。JFIF・Adobe の読み取りは
  残した bytes からも行う。
- `JCS_EXT_BGRA` は p019 で済み。

## 受け入れ

1. amd64 の build（warning 0）、変えた file の style-check 0。
2. host（plain・ASan）: cjpeg `-progressive`（既定の scan の script: DC の最初と細かくする、AC の最初と細かくする）と Pillow の
   `progressive=True` の file が djpeg と byte ごとに一致。restart 付きの progressive も。CMYK（Pillow）の raw の出力が
   libjpeg-turbo の raw（Pillow の `CMYK;I` の反転を戻した値）と一致。`jpeg_save_markers` で APP1 の bytes が file と同じ。
   p019 の試験が下がらない。算術符号は理由付きで断る。
3. guest（jpeg-guest.sh）で同じ比較。

## 後回し（follow-up）

- 縮小（`scale_num`/`scale_denom`）、`jpeg_read_raw_data`、buffered image、block smoothing（全部の係数が細かくされない
  file の途中の表示）、suspension、12 bit、算術符号、圧縮。
