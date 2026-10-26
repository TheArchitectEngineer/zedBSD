<!-- awesome-plan project=zedbsd record=ws295 -->

# WS295 Intel の xe の driver（Meteor Lake 以降の iGPU と discrete の Arc、GuC の submission）

- Master: [plan/master.md](../master.md)（2026-10-27 00:18 作成）。Primary Milestone: MG006。Related: [WS294](../ws294/ws.md)（i915 の GuC、取り下げ、GuC はこの WS へ）、[WS038](../ws038/ws.md)（Intel Arc dGPU の番号の予約、この WS に含める）、i915 の [WS245](../ws245/ws.md)・[WS252](../ws252/ws.md)・[WS293](../ws293/ws.md)。
- Status: planning（2026-10-27 00:18 新設。WS だけ、Phase は p001 の調査の後に足す）
- 出典: 2026-10-27 00:18 ユーザー「了解です。Meteor Lakeを持っておらず、Lunar Lakeは持っています。Meteor Lakeは入手したら後日やります。i915のGuCは実装せず、xeドライバを開発するときにGuC対応を実装することにします。xeドライバはMeteor Lake以降のiGPUと、DiscreteのArcのためのものとします。既存i915の利用できるところはコピーして、重複させて作ります。調査のphaseから始めて、全体のphaseをあとで追加します。WSだけ立てておいてください。」

## 単一の到達目標

Meteor Lake 以降の Intel の iGPU と discrete の Arc のための新しい driver（xe）を作る。submission は GuC。i915 は今の世代（Gen12 まで）のまま残し、i915 の使える所は写して xe の側に重複させて持つ（共有にしない）。

## 決まっている事（2026-10-27 00:18 ユーザー）

- 対象: Meteor Lake 以降の iGPU、discrete の Arc。Meteor Lake は実機が無いので入手した後に。Lunar Lake は実機がある（host・接続はユーザーに確かめる）。
- i915 の GuC は実装しない。GuC の対応は xe で。
- i915 の使える所は写して重複させる。
- Linux の driver・Mesa は値・表・手順の参照だけ、既存の [Guardrail](../guardrail.md)「MIT・GPL の出典のデータの引用」をそのまま適用。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 調査（code は書かない） | — | planned |
| p002〜 | p001 の結果で足す | p001 | — |
