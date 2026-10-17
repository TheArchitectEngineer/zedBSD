<!-- awesome-plan project=zedbsd record=ws251-p002 -->

# ws251-p002: GPU ドライバ（src/drivers/gpu/）の comment と log から開発の管理情報を消す

Status: in-progress（2026-10-17 q1240-i01、P1。i915 から）
Disposition: normal
Parent: [WS251](../ws.md)

## 由来（2026-10-17 21:1x ユーザー、原文）

「そのほかに、コメントからWSとかBUGSとかSTAGEみたいな開発管理の情報を削除することが必要です。"The test build run under QEMU leaves the display hardware alone"みたいな話はplan/に書けばいいのであって、よそのプロジェクトでも流用されるソースコードから削除するべきと思います。このプロジェクトのGPUドライバは、Mesaを使わないという点では、現時点で世界で唯一のOSSのGPUドライバです。おそらく有効活用してくれる人がいるはずです。」

解釈（Q1）: WS251 の削除の対象に、番号の類だけでなく、このプロジェクトの開発・試験の進め方の話（どの試験の build・QEMU・実機の回でどう振る舞うか、どの段で何を入れたか、開発の段の符号）を加える。そうした説明は必要なら plan/ に移す。hardware と code の振る舞いの説明は残す。GPU ドライバは他のプロジェクトでも使われうるので先に行う。

## 範囲

- `src/drivers/gpu/` の全部（i915 → bcm2711 → nvrtx・venus・compiler・amdrdna の順）と、その driver の header（`include/` の GPU の物）。
- 消す物: WS・Phase・Queue・Bug・試験の回の番号、plan/ への参照、日付つきの決定、段の符号（N0・LCD-B・P5-0・F1 の類）、STAGE・stage の開発の段の話、試験の build・QEMU・試験機の運用の話（例「The test build run under QEMU leaves the display hardware alone」）。
- 試験の code（`tests/` の下の host 試験・ktest）の comment も同じ規則。ただし試験が何を確かめるかの説明は残す。
- log の文字列を変える時は照合する試験・道具（plan/tools・plan/ws*/tests・tests/ の scenario）を追従させる。
- 振る舞いは変えない（comment と log の文字列だけ）。HAL の API・toolchain は触らない。

## 完了の条件

- `git grep -nE '\b[Ww][Ss][0-9]{3}\b|\bq[0-9]{3,4}\b|BUG-[0-9]{3}|\bT[0-9]-[0-9]+|plan/'` が src/drivers/gpu/ で 0 件。
- 開発の段・試験の運用の話の目視の点検（file ごとの記録）。
- build 3 組（既定・vke2・execution）と Pi 4 の arm64 の build が warning 0。i915 の display の host 試験（run-xd-host-tests）と変えた log の照合先の試験が通る。
- 振る舞いを変えていない根拠: comment だけの file は kernel の binary の比べ（log の文字列を変えた file は除く）。
