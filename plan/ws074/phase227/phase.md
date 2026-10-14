<!-- awesome-plan project=zedbsd record=ws074-p227 -->

# ws074-p227: Flex absolute-child static position and alignment keywords

B1 (2026-10-14). 範囲: flex container の absolute 子の static position を「唯一の flex item」として求める（justify-content は主軸、align-self / align-items は交差軸、row・column・reverse・wrap-reverse・direction・縦書き）、`first baseline`・`last baseline`・`safe`/`unsafe` の構文（last baseline は end に退避、safe/unsafe は無視）、flex-start/flex-end を start/end と区別する style の bit。

## q1201-i01 結果（2026-10-14）

Event ws074-p227-q1201-partial-clear-20261014: [証拠](evidence/20261014-q1201/transitions.txt)。同条件 1,455 ID: whole PASS 427→458（後退 0、callback 9,505→10,466）。`flex-abspos-staticpos-*` の justify-content 001–008・rtl・vertWM 001/002、align-self 001–008・rtl 001–004・vertWM 001–004、align-content-001 などが PASS。未達: `last baseline` / `baseline` の本来の整列（end・start に退避しているだけ）、`safe` の clamp。
