<!-- awesome-plan project=zedbsd record=ws242-p006 -->

# ws242-p006: GSP の起動の段ごとに諦めて firmware の画面に戻す道

Status: planned
Disposition: normal
Parent: [WS242](../ws.md)
Queue: q1678-i01（P2）
依存: ws242-p004
設計: [design.md](../design.md) 第 2 版の V1（表の p005b）。review の B1・M3。ユーザーの回答 Q242-3「諦めの道を先に (推し)」

## 範囲

1. 実装の最初に今の `gsp.c`・`boot.c` の失敗の段（FWSEC-FRTS・booter_load・RISC-V の起動・INIT_DONE）の state・region・memory・console の行方を
   表にし、各段の安全な戻し方（bus master を切る順、GSP falcon の reset と止まったことの確認、FWSEC-SB・booter_unload が走れるか）を決める。
2. 戻せる段では falcon を止め、region を壊し、`nvrtx_console_restore()` で firmware の画面に戻し、memory を返す。止まったことを確かめられない時は
   bus master を切って memory を保つ（今の `FAILED_HOLD`）。
3. host で試せる部分（段 × 結果 → 戻し方の表）を純粋な関数に出して試験。RTX 2070 での段ごとの故意の失敗は p008。

## 受け入れ

- host 試験が通る。amd64 vmunix が warning 0。実機の確かめは p008。
