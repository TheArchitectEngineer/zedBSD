<!-- awesome-plan project=zedbsd record=ws260 -->

# WS260 i915 の Gen9 の display（gen9-*.c）を xd と同じく自前で書き直す

- ID: ws260、Master: [master.md](../master.md)、Primary Milestone: MG006
- Status: incomplete（2026-10-18 Q1 作成。p001 を P1、q1252。p002〜 を P1、q1253）
- 起点（2026-10-18 ユーザー）:「i915由来のdisplayのソースコードをすべて書き直す件は、どうなったんですか？」→ Q1 の報告（display 12・13 は WS245 で書き直し済み、Gen9 は 2026-10-15 の決定で「ガードレールにそって修正」とし、出典の表記だけを WS258 で直し、写しの点検は未実施）→ ユーザー（クリック）「xd と同じく書き直す」。
- 対象: `src/drivers/gpu/i915/display/gen9-*.c`・`*.h`（22 file、display 9: Skylake・Kaby Lake・Coffee Lake・Comet Lake）と、それだけが使う intel/ の header。
- 手本: [WS245](../ws245/ws.md) の xd（Intel の PRM と hardware の振る舞いから自前で書き、Linux の行ごとの言い換えをしない。値は block ごとに `Values referenced:`、手順は `Procedure referenced:`。PRM に載る名前は使ってよい（2026-10-18 WS244 の決定））。
- 注意: 2026-10-15 のユーザーの決定「Gen9 のディスプレイはガードレールにそって修正してください」を、この決定で書き直しに広げた。WS244 の Gen9 の display の項目（p002 の header は済み）と重ならないように分ける。

## 完了の条件（案、設計で確定）

1. gen9-*.c の code・comment・構造に Linux の写しが無い（設計で決めた点検の方法で確かめる）。
2. Gen9 の機種（SV7 など）で、今と同じに起動・内蔵の画面・backlight・desktop が動く（実機の回帰は T1 か、ユーザーの一覧）。
3. build warning 0、host 試験、規約の全文。

## Phase（案）

| Phase | 目的 | 状態 |
| --- | --- | --- |
| [p001](phase001/phase.md) | 設計: 今の gen9-*.c の棚卸し（写しの点検）、xd の部品をどこまで共有するか（xd に display 9 を足すか、別の層か）、段の分け方と回帰の方法 | cleared（2026-10-18 Q1: design.md、判断 5 点はユーザーが回答） |
| [p002](phase002/phase.md) | 試験の土台: golden の書き込みの列と数の計算の表を今の code で取る。gen9-fake-hw を xd-io の backend に繋ぐ口 | in-progress（q1253、P1） |
| [p003](phase003/phase.md) | xd-platform に display 9 の記述（SKL・KBL・CFL・CML、PCH の SPT・KBP・CNP・CMP、port A〜E）。選ぶだけで呼ばない | in-progress（q1253、P1） |
| [p004](phase004/phase.md) | 数の計算: CDCLK の読み、DP の link と M/N、DPLL の語（DP と HDMI の WRPLL）、buf trans の表を xd-clock・xd-dp・xd-ddi へ。gen9-mode.c の該当は殻 | in-progress（q1253、P1） |
| [p005](phase005/phase.md) | WM・DDB の計算を xd-dbuf へ（display 9 の 1 slice・8 level・IPC） | in-progress（q1253、P1） |
| [p006](phase006/phase.md) | AUX・GMBUS・EDID を xd-aux・xd-gmbus・xd-edid へ（gen9-ddc.c を殻に） | in-progress（q1253、P1） |
| [p007](phase007/phase.md) | backlight: SPT/KBP の LPT の形と CNP/CMP の形を xd-panel へ（gen9-backlight.c を殻に） | in-progress（q1253、P1） |
| [p008](phase008/phase.md) | power well（PW2・DDI IO・AUX）を xd-power へ | in-progress（q1253、P1） |
| [p009](phase009/phase.md) | DPLL の enable/disable と DDI clock の選択を xd-clock へ | in-progress（q1253、P1） |
| [p010](phase010/phase.md) | DDI buffer・signal level・iboost・DP_TP と link training を xd-ddi・xd-dp へ（gen9-link.c を殻に） | in-progress（q1253、P1） |
| p011 | transcoder・pipe・plane の点灯と消灯を xd-pipe へ（gen9-modeset.c の残り） | planned（5 LW） |
| p012 | WM・DDB の書き込みと順序、hotplug の sense を xd-dbuf・xd-hpd へ | planned（4 LW） |
| p013 | firmware の点検と flip（firmware の panel の引き継ぎ、plane の flip、irq）を xd-takeover・xd-screen の display 9 の方針へ（gen9-firmware.c・gen9-flip.c） | planned（6 LW） |
| p014 | 外の display（gen9-node・gen9-outputs・gen9-display）を xd-screen・xd-display の口へ。bootfb.c・i915.c・device.c・display.c の呼び先を xd に付け替え、殻を全部消す | planned（6 LW） |
| p015 | 規約の全文の見直し、写しの点検（§1.2 の方法を新しい code に）、T1 に SV7 と 5330 の実機の回帰を 1 回 | planned（3 LW） |


## ユーザーの決定（2026-10-18、クリック）

1. 形は「案 A: xd に display 9 を足す」。
2. 実機の確かめは「最後に 1 回」（全部の段の後に T1 が SV7 と 5330 で 1 回、壊れていれば段の境で二分）。
3. Gen9 の log の文言は xd の形に「揃える」（照合の試験も同じ段で追従）。
4. 起動の語 `i915.gen9.display=` は「xd の語にまとめる」→ 中身は「語を廃止、常に full」（Gen9 も常に全部を駆動、切り分けは `i915.debug=display` と `i915.start=manual` だけ。src/kern/boot.c・include/kern/boot.h の変更を P1 に許可、p014。手順・試験の image の設定も追従）。
5. SV7 に外の display は「繋げない（host 試験だけ）」（外部出力の段の実機は未実施と記録し、ユーザーの試験の一覧に載せる）。