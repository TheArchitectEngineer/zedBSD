<!-- awesome-plan project=zedbsd record=ws245-p050b -->

# ws245-p050b: eDP の step の本体を xd の部品の直の呼びに

Status: planned（2026-10-16 Q1 の判断で p050 を p050a〜d に分けた。design の順のまま p039 の前に行う）
Parent: [WS245](../ws.md)
Queue: q1229-i06（P1）
設計: [design.md](../design.md) §6.2 の p050 の行、§3.3、§4.2

## 範囲

xd-screen の light・dark の step（power・dbuf・setup・pipe dmc・link power・pll・port・pipe・output・check・plane・settle と dark の各 step）の中身を、eDP（内蔵 panel）について xd の部品（xd-panel の PPS と backlight、xd-aux の DPCD、xd-power の DDI IO と AUX の well の参照、xd-dp の訓練、xd-dbuf の WM/DBUF の書き込み、xd-pipe、xd-clock、xd-ddi、vblank の event）を直に呼ぶ形にする。旧の modeset.c・state.c・ddi.c・dp-sink.c・pipe.c・plane.c・aux.c・panel.c・panel-backlight.c・vblank.c の eDP の道の state（Linux の型の PPS・backlight・wakeref・link）は使わなくなる。

## 受け入れ

- host 試験（run-xd-host-tests.sh、新しい check は xd の試験へ）、build（既定と I915_TESTS=y の 5 組）warning 0、段の前後の比べ（旧 host 試験が残る間は refcmp.sh）、R11 の数。
- 5330 の回帰は全部の段の後に 1 回（ユーザーの決定 2026-10-16）。
