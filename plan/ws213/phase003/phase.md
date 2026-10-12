<!-- awesome-plan project=zedbsd record=ws213-p003 -->

# ws213-p003: S1: 描く物の一覧（display list）を同じ thread で再生する

Parent: [WS213](../ws.md)
Status: planned（review-2 で条件付きで可。着手は Q1 が WS211 の P3（p003 段 2・p006 の drag）と順を決めてから）
Phase disposition: normal

## 範囲

op の list、描画の関数の `VkCommandBuffer command` の引数（37 file）を `struct kwl_list *` に、compose->command を直に使う所も一覧へ、batch の頂点、bind の鍵（import・種類・世代）、`struct kwl_import` の世代、cursor の形の画像の起動時の作成。再生は今の `compose_record` と同じ所。再生の等価の host 試験（descriptor set の handle を正規化）、Pi で walk と再生の時間を分けて測る。

設計は [p001](../phase001/phase.md)。
