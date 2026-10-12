<!-- awesome-plan project=zedbsd record=ws213-p004 -->

# ws213-p004: S2: 描画 thread（2 段の pull）

Parent: [WS213](../ws.md)
Status: planned（p003 の Pi の walk の計測の後、p001 の再 review を通ってから）
Phase disposition: normal

## 範囲

p001 の 2〜4 節: want・ready・一覧、R は ready の後 Vulkan を呼ばない不変条件、park（上限付き）、R→M の 1 本の通り道、host image の書き込みは walk と park の中だけ、cursor の shm、M の present の待ちの判定。

設計は [p001](../phase001/phase.md)。
