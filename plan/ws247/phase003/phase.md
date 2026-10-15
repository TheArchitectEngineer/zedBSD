<!-- awesome-plan project=zedbsd record=ws247-p003 -->

# ws247-p003: nvrtx の compiler（sm75.c・sm75.h）に p001 と同じ出典の規則を当てる

Status: cleared（2026-10-15、q987〜q989 とも cleared）
Parent: [WS247](../ws.md)
Approval: 2026-10-15 ユーザー「sm75.[ch]にも同じルールを適用して監査、修正して」

## 範囲

[p001](../phase001/phase.md) と同じ: 定数・表の block に「Value(s) referenced:」、手順を参考にした関数に「Procedure referenced:」を block の直前の複数行の comment で付ける。referenced: の段落の外の出典の名前（nouveau・Mesa・NVK・NAK・Linux・open-gpu）を除く。開発の管理情報（段・試験の回・WS 番号）も除く。**comment だけを変える。** WS246 p002 の code の refactor（latency の if の連鎖を表に、構造の見直し）は含まない。

## 分担

| Queue | 担当 | 範囲 |
| --- | --- | --- |
| q987 | P1 | compiler/sm75.h（全体） |
| q988 | P2 | compiler/sm75.c の 1〜3026 行（main 7f489c51f の行番号、nvrtx_sm75_binary_free の前まで） |
| q989 | P3 | compiler/sm75.c の 3027 行〜末尾（nvrtx_sm75_binary_free から） |

## 確かめ

p001 と同じ（cpp の比較、build warning 0、監査の grep）。Q1 が merge の後に host 試験 sm75・desktop・vk-d3・vk。

## 結果

- **q987 P1（sm75.h、提出 593a10b91）**: 値の block は基点で全部 referenced: 付きだった。説明と referenced: の 2 つの comment を 1 つの複数行の comment にまとめ（40 block）、Mesa 25.3.6（06f9e2830）で行の番号を照合して直した（sm70_encode.rs の OpXxx の範囲を全部 -1 ほか）。曖昧な出典に関数・表の名前を足した（nvk_descriptor_types.h、RegLatencySM75 など）。全 define の値を出典の実物と照合して食い違い 0。
- **q988 P2（sm75.c 1〜3026 行、提出 f2ed1701a）**: 53 block（Procedure 50: encode の関数を sm70_encode.rs の OpXxx::encode と 1 つずつ照合、schedule は calc_instr_deps.rs。Values 3）。reorder（上から下の list scheduler）は自前の設計として付けない。
- **q989 P3（sm75.c 3027 行〜、提出 028a0b640）**: 関数だけの範囲。新規 13（Values 8・Procedure 5）、既存 7 を具体化。開発の経緯の文 1 つを言い換え。
- 出典の名前・開発の管理情報は 3 範囲とも referenced: の外に 0（基点の時点で 0）。
- **Q1 の確かめ（main 70e7a254f）**: build rc 0・warning 0、監査の script で nvrtx の全 file の referenced: の外の出典の名前 0、host 試験 sm75 287/0・desktop 138 compiled/0 disagreed・vk-d3 122/0・vk 60/0。

## 出典の無い値・ユーザーの判断

- sm75.c の「Value referenced: the SM75 code the CUDA compiler emits」（IMAD の carry、EXIT の遅れ 5 など 3 か所）: 版と path の形でなく参照の tree で確かめられない。扱いをユーザーに聞いた（2026-10-15）。
- 末尾を 128 byte の行まで NOP で埋める（NVRTX_SM75_LINE_INSTRUCTIONS・nvrtx_sm75_finish）、GUESS_* の latency の見込み、register の割り当て、cbuf 0 の layout: 自前。
- NVRTX_SM75_ATTR_GENERIC_END（0x280）は ir.rs VtgIoInfo::mark_attrs の `addr < 0x280` を出典にした（名前の付いた定数は無い）。
