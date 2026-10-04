<!-- awesome-plan project=zedbsd record=ws095-p016 -->

# ws095-p016: IME の状態を app ごとに記憶する（system 全体で 1 つにしない）

Status: cleared（2026-10-05 Q1: T1-119 の ime-p016 status=0（手順 5 の直し ac1c8a2 の後）、T1-102 の手順 1〜4 ok）。以前: in-progress（q708-i01、P2 generation14、2026-10-05。実装・build 済み、QEMU（T1）待ち）
Disposition: normal
Parent: [WS095](../ws.md)
Queue: q708-i01（P2 generation14）

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

## 設計と結果（2026-10-05、q708-i01 P2 generation14）

- **今の状態（確かめた）**: 言語（direct・ja）は keiland-ime の process が 1 つ持ち、compositor（`input-method.c`）は status の `language` で知り、`select`・`next` で選ばせる。system 全体で 1 つだった。
- **app の単位（範囲 5）**: keyboard の focus の窓の xdg の `app_id`（`surface->app_id`）があればそれ（同じ app_id の窓は別の process でも 1 つの状態）、無ければその client（接続）。desktop は focus が無い時か desktop の surface（`zwl_desktop_is`）が focus の時。
- **記憶**: compositor の `struct zwl_ime` に app ごとの言語の表（最大 32、`struct zwl_ime_app`）、desktop の言語、今の focus の key。status の `language` が来るたびに今の focus の key（app か desktop）の記憶にする。focus が別の app へ移った時（`zwl_ime_focus`、seat.c からの既存の呼び出しの中、P1 の input.c・seat.c は変えていない）、移る前の言語を前の key に記憶し、移った先が既知の app ならその言語、初めての app なら desktop の言語（desktop が一度も focus を持っていなければ今の言語のまま）、desktop なら desktop の言語を `select` する（今と同じなら送らない）。同じ app の中の窓・widget・caret の移動は key が同じなので何もしない（範囲 2）。log `ZWL IME app key=… language=… from=remembered|inherited|desktop|kept`。
- **app が終わった時**: その client の最後の接続が終わると記憶を捨てる（client の key は即、app_id の key は同じ app_id の窓を持つ他の接続が無い時）。次の起動は desktop の言語を引き継ぐ（範囲 3 の挙動に合う）。focus を持っていた app が終わった時は、次の focus まで言語を誰の物ともしない。
- **右上の status**（範囲 4）: indicator は今の言語（`ime->label`）を描くので、focus の app の言語を表示する（追加の変更なし）。
- **制限**: keiland-ime が落ちて立ち上がり直すと、その時の focus の app の記憶は新しい process の初期の言語（direct）で上書きされる。WS154 の SKK の mode（かな・カナ）は言語の ID の中に入らないので、今は言語（direct・ja）だけを app ごとに持つ（WS154 で mode が language の ID か別の status になったら同じ表に足す）。
- **確認**: zedBSD の `bin/wayland`・`bin/ime-probe` warning 0（`ZEDBSD_CONFIG=plan/ws095/tests/config-amd64-ime.mk BUILD=build/p2-q703`）。ime-probe に `--app-id=ID` を足した。host の試験は無し（compositor の焦点の経路）。
- **試験の依頼（T1、Q1 経由）**: `plan/ws095/tests/ime-p016.sh`（probe A・B・C（A と同じ app）・D で、引き継ぎ・記憶・同じ app の共有・desktop の言語の引き継ぎ）。

### T1-102 の FAIL の判断と直し（2026-10-05、P2）

- ime-p016 手順 5 の FAIL は**実装の不足**: keyboard の focus を持つ app の窓が無くなると seat.c は focus を NULL にするだけで `zwl_ime_focus` を呼ばないので、IME は desktop に focus が移ったことを知らず、desktop の言語に戻らなかった（`app key=desktop` の行が無い）。直し: `ime_app_forget` で focus を持つ app が終わったら keyboard を desktop の物とし、desktop の言語を選ぶ（`ZWL IME app key=desktop language=… from=desktop`）。旧の「誰の物でもない」（`IME_APP_GONE`）は除いた。seat.c は変えていない。
- ime-p004（ws095-p004 の試験）手順 6 の FAIL は**試験の前提の古さ**: 新しい probe は p016 の仕様で desktop の言語（direct）で始まり、direct の key は input method に届かないので bypass が起きない。直し: 手順 6 の前に Alt+Space で日本語にする（`ZWL IME language=ja` が 1 つ増える）。
- build: compositor（`bin/wayland`）warning 0、規約の新しい違反 0。T1 に ime-p004・ime-p016 の再試験を依頼。

## Q1 の判定（2026-10-05）

T1-119 の ime-p016 status=0（手順 5 の直し ac1c8a2 の後）、T1-102 の手順 1〜4 ok。**cleared**。
