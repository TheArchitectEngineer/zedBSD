<!-- awesome-plan project=zedbsd record=ws291-p001 -->

# ws291-p001 desktop の計測と 60 fps

- 親: [WS291](../ws.md)。
- 状態: planned（2026-10-26 23:05 Q1 新設）

## 範囲

1. main の amdrdna（WS039 の p006・p009・p012 が main に入っているかを先に確かめる）で amd64 の vmunix と desktop の package を build し、10.0.10.3 の USB の zedBSD に入れて BootNext で起動。
2. frame-ops.sh の段（idle・Terminal の起動・type・keys・output・scroll・hover・drag・App Home・Wiseview）を流し、frame-times.py で段ごとの fps・work・period を表に。
3. 60 fps に届かない段は、Keiland の --log-frames と driver の時間（submit・fence・flip の待ち）で原因を分け、amdrdna の driver（src/drivers/gpu/amdrdna/）か Keiland の直せる所を直す。既に 60 fps なら記録して終える。

## 受け入れ

- 全段 60 fps（または届かない段の原因と残りを記録）。前後の数値、build の warning 0。

## 記録

- 2026-10-26 23:05 Q1: 新設。
