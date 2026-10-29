<!-- awesome-plan project=zedbsd record=ws242-p008 -->

# ws242-p008: RTX 2070 の実機の回帰（段ごとの止めと諦めの道を含む）

Status: planned
Disposition: normal
Parent: [WS242](../ws.md)
依存: ws242-p004〜p007、機械（ユーザーの回答 Q242-4「RDNA と差し替え (推し)」: 10.0.10.3 の GPU を RTX 2070 に差し替え、SwitchBot で電源、判定は ssh と boot-test.sh）
設計: [design.md](../design.md) §4、第 2 版の表の p006。review の M3・M6

## 範囲

- 起動・display・desktop・vkcube 相当（WS037 の受け入れと同じ）。`nvrtx.stop=` の段ごとの止めで画面が残ること、p006 の諦めの道を故意に通した時の画面と次の warm reboot。
- 担当は T1（Q1 が割り当てる）。q1678 の範囲の外（この Queue は試験なし）。
