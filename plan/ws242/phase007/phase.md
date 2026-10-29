<!-- awesome-plan project=zedbsd record=ws242-p007 -->

# ws242-p007: GTX 16・RTX 30・40 を既定で起動

Status: planned
Disposition: normal
Parent: [WS242](../ws.md)
Queue: q1678-i01（P2）
依存: ws242-p004、ws242-p006
設計: [design.md](../design.md) §3、第 2 版の V5（表の p005c）。ユーザーの決定（2026-10-15）「GTX 16・RTX 30・40 はデフォルトで有効にします。」、回答 Q242-3

## 範囲

- 起動の判断（p003 の関数）で verified を既定の起動の条件から外す（`gop_on_gpu` の条件は残す）。未検証の family の起動では「not yet seen on hardware」の印。
  `verified` は「実機で見た」の記録の意味に残す（欄は足さない、m6）。`nvrtx.gsp=0` で全部を firmware に残すことを文書（driver の説明）に書く。
- 既定を変える行は最後の commit（p004・p006 より先に main に入らないこと）。

## 受け入れ

- identify の host 試験の起動の判断の組が新しい表で通る。amd64 vmunix が warning 0。
