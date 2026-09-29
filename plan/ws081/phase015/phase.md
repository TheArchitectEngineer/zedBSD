<!-- awesome-plan project=zedbsd record=ws081-p015 -->

# ws081-p015: Notes の「指で書く」の切り替え

<!-- awesome-plan-current:start -->
Status: in-progress（2026-09-29、WS081 の作業用サブエージェント）
Disposition: normal
Parent: [WS081](../ws.md)
Queue: main の指示（2026-09-29 夕「p010 を仕上げる → p015 → p006」）。Awesome Plan の Queue の item ではない
Resume point: 実装中（下の「設計」のとおり touch.c・touch.h・main.c・ui.c・app.h を変える）
<!-- awesome-plan-current:end -->

## 目的（2026-09-29 ユーザーの決定）

既定は今（[p013](../phase013/phase.md)）のまま、指は scroll・pinch。toolbar に切り替えを足し、入れた間は一本指で線を書き、二本指で scroll・pinch する。
実機でペンが使えない場合のデモの備え。

## 受け入れ条件

1. toolbar に切り替え（「Finger」）があり、押すたびに入・切が変わり、入の間は選ばれた色（Kei の青の pill）で見える。
2. 入の間、頁の上の一本指は、選んだ道具（Pen・Marker・Eraser、色・太さ）で線を書く（消しゴムなら消す）。報告は間引かずに使い、線の形は libpdf の
   centripetal Catmull-Rom（design §3.8）で滑らかにつながる。
3. 入の間、二本指は scroll・pinch（p013 と同じ）。一本目の指が書き始めて間もなく（250 ms 未満、または 12 px 未満しか動いていない）二本目が触れたら、
   書きかけの線は取り消して（頁に残らない）、二本指の scroll・pinch になる。それより後の二本目は無視し、線はそのまま続く。
4. 入の間も toolbar の tap は button を押す。掌の規則（ペンが近い間と離れて 500 ms は新しい指を無視、ペンが近づいたら書きかけの指の線は取り消す）は保つ。
5. 切の間は p013 と同じ（指は線を書かない）。
6. host 試験・guest 試験（QEMU）で確かめる。実機は p007。

## 設計

- `touch.c`: `notes_touch_write_mode(touch, on)`。入の間、最初の指が頁の上に触れると「書く指」になり、gesture には渡さず、書く event
  （BEGIN・MOTION・END・ABORT、窓の px と ms の時刻）の queue に入れる。main は `notes_touch_take_write` で取り出す。二本目の扱いは上の 3。
  取り消した時は、書く指をその位置・時刻で gesture に渡し、二本目と一緒に scroll・pinch にする。入の間は double tap の拡大をしない（一本指の tap は点を書く）。
  glide している頁に書く指が触れたら、頁はその場で止まる。
- `main.c`: 書く event を pointer と同じ入力（`NOTES_SOURCE_POINTER`、固定の筆圧）にして `app_input` に通す（道具・色・太さ・消しゴムは pointer と同じ）。
  ABORT は書きかけの線を捨てる（`app_abort_contact`）。指の contact が pointer・ペンの contact に取って代わられたら、その後の指の event は無視する。
  ペンの近さを知らせた直後にも書く event を取り出す（ペンの down より先に指の線を取り消すため）。toolbar の action `NOTES_ACTION_FINGER`（14）。
- `ui.c`・`app.h`: toolbar の道具の群の後に「Finger」の button。`notes_ui_state.finger_write`。
