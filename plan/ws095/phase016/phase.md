<!-- awesome-plan project=zedbsd record=ws095-p016 -->

# ws095-p016: IME の状態を app ごとに記憶する（system 全体で 1 つにしない）

Status: planning
Disposition: normal
Parent: [WS095](../ws.md)
Queue: 未定

## ユーザーの要望（2026-10-04 夜、原文）

「IMEの状態は、アプリごとに記憶されるようにしてください。システム全体で1つにしなくていいです。デスクトップ画面のIME状態が、次に表示されるアプリのIME状態として引き継がれる挙動だと、細かい使いやすさがあってうれしいです。また、アプリ内のウィンドウごとにはIME状態を持たないほうがよく、アプリ内で共通にします。アプリ内でウィンドウ、ウィジェット、キャレットが移動しただけでIME状態が変更されてほしくないです。」

## 仕様（Q1 の整理、細部は設計で）

1. **app ごとの IME の状態**: IME の on・off（日本語・英数）と、必要なら入力の mode（かな・カナなど、WS154 の SKK の mode を含む）を **app ごとに**記憶する。system 全体で 1 つの状態にしない。
2. **app の中では共通**: 同じ app の窓・widget・caret（text の入力の場所）が変わっても状態を変えない。窓ごと・入力欄ごとに状態を持たない。
3. **新しく表示される app への引き継ぎ**: app が初めて focus を得る時（まだその app の状態の記憶が無い時）は、その直前の **desktop の画面（どの app にも focus が無い状態）の IME の状態**を初期値として引き継ぐ。
4. 既に記憶のある app へ focus が戻った時は、その app の記憶の状態に戻す。右上の IME の status（A／あ、ws095-p005）は focus の app の状態を表示する。
5. **app の単位**の決め方: Wayland の client（connection）か、同じ app の複数の process をまとめる app の ID（xdg の app_id）かを設計で決める（例: 同じ app_id の窓は 1 つの状態を共有）。app が終わった時に記憶を捨てるか残すか（次の起動で前の状態に戻すか）も設計で決める。
6. 実装の場所: compositor の input method の側（text-input の enable・focus の変化を見て、app ごとの状態を切り替える）と keiland-ime の process。今は system 全体で 1 つの状態（Q1 の読み、設計で確かめる）。

## 受け入れ（案）

- app A で日本語、app B で英数にして A・B を行き来すると、それぞれの状態に戻る。同じ app の 2 つの窓・2 つの入力欄の間を移っても状態が変わらない。desktop で日本語にしてから新しい app を開くと日本語で始まる。
- QEMU（T1、キーボードの注入と IME の status の読み）、実機の UAT。C の全文の規約、build warning 0。

## 依存

WS095 の p005（右上の status）、[WS154](../../ws154/ws.md)（IME の選択・SKK の mode）と設計を合わせる。
