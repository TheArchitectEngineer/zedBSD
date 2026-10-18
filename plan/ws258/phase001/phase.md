<!-- awesome-plan project=zedbsd record=ws258-p001 -->

# ws258-p001 全ドライバの先頭の comment の出典の監査と直し

Status: in-progress（2026-10-18 q1248-i01、P1）

- 親: [WS258](../ws.md)。Queue: q1248-i01。
- 範囲: `src/drivers/` と `include/drivers/` の .c・.h・.S・.inc（約 1,030 file）。comment だけを変え、動作は変えない。
- 手順: 各 file の先頭の comment（最初の #include より前）を読み、Linux・FreeBSD・Mesa・nouveau・amdgpu・vendor の資料などの出典・参考・派生の記述があれば外し、その内容が要る出典なら該当の関数・表の block に `Values referenced:` か `Procedure referenced:`（版・path・関数か表の名前）で書く。file の役割の説明は残す。code・comment・構造を外部から写していないかも見る（写していれば Q1 に報告）。
- Q1 の予備の grep（先頭の comment に Linux などの語がある file）: src/drivers/gpu/i915 に 66 file ほか。語だけの一致なので、file ごとに読んで決める。
