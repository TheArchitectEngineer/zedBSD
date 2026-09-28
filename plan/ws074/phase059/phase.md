<!-- awesome-plan project=zedbsd record=ws074p059 -->

# ws074-p059: Google の調査（デモの目標の前提）

Phase ID: `ws074-p059`（WS074 の次の空き番号。2026-09-28 main の依頼）
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: p057（今の browser）

## 範囲

デモの目標（ws.md の「デモの目標」）の前に、今の browser が Google から受け取る HTML、Chromium の描画との違い、足りない機能を調べ、
目標にする page の版と、そこまでの Phase の列を決める。p031・p032 の前に置く（main の注意）。実装はしない。

## 手順（host、2026-09-28）

- host の build: `sh plan/ws074/tests/host-build.sh plain`（warning 0）。
- 取得: curl で `https://www.google.com/` と `/search?q=kei` を 16 の User-Agent で（`Accept-Encoding: identity`）。home の form を
  そのまま送る版（hidden の値と `iflsig`、`ie=ISO-8859-1`）、`gbv=1`・`udm=14`・`/m`・`/xhtml`・`tbm=`・`google.co.jp` も。
  保存は `build/ws074-google/` だけ（tree に入れない）。
- 静的な数え上げ: home の inline の `<style>` と style 属性の property・selector・at-rule（engine の `css/values.c` の名前と照合）、
  script の数と大きさと ES の構文、ES5 bundle 2 つ（`og.qtm…es5.O`、`xjs.hp…es5.O`）と challenge の script の API。
- 描画: 私たちの `--render`・`--dump=layout`（live の URL）、Chromium 153 の headless（私たちの UA と同じ font）と headful（Xvfb、
  CDP で home に文字を打って Enter）。guest（Venus、`build-browser-image.sh` の image）の `/bin/browser` の窓で live の home。
- JS の engine の機能: `--js` で 41 の小さな program。
- 比較の道具: [tests/google-compare.py](../tests/google-compare.py)（新。live の page を私たちと Chromium で描き、白でない画素の一致を出す）。

## 結果

詳細は [google-goal.md](../google-goal.md)。要点:

- home: 私たちの UA `browser/0.1 (Kei)` には基本の HTML の版（`gbv=1`、table の配置、`<input name=q>`、ES5 の bundle）が返る。
  curl・Dillo・NetSurf と同じ。Chrome の UA には現代の版（textarea と重い CSS・JS）。
- 結果: 基本の HTML の結果はもう無い。text browser と古い browser には「ブラウザを更新してください」、feature phone には 403、私たちの
  UA を含む他の全てには SearchGuard の JS の challenge（難読化した VM、`SG_SS` の cookie、開き直し）。
- この host の Chromium（headless・headful とも、人と同じ手順）は結果の代わりに `/sorry`（reCAPTCHA）へ送られた。結果の page の
  Chromium の参照は取れていない。Google の bot の判定は私たちの実装の外の危険（google-goal.md §3、人の判断を要する）。
- 今の browser の home の崩れ: input が無い、table が block、inline-block が block、flex が block、SVG が無い、JS が RegExp で止まる。
  白でない画素の一致 20.13%（`google-compare.py`、1280x900）。検索は challenge の script が RegExp と `navigator` で止まり、`<noscript>` の
  中身が生の文字で出る。
- JS の engine: ES5 の構文と eval・getter・JSON は動く。RegExp・Date・Promise・Symbol・Map・typed array・encodeURIComponent が無い。
- 目標の版: home は私たちの UA の基本の版、結果は challenge を通った後の page。Phase の列は google-goal.md §4（p032 を form に絞り、
  p060〜p066 を足す）。

## 写真（`/home/awe/zedBSD-rpi4/build/ws074-shots/`、QEMU と host の証拠。実機は未実施）

- `p059-20260928-survey-home-before.png`: 私たち | Chromium（私たちの UA）| 違う画素。
- `p059-20260928-survey-home-ours.png`・`…-home-chromium-keiua.png`・`…-home-chromium-chromeua.png`（Chrome の UA の現代の版）。
- `p059-20260928-guest-google-home.png`: guest（Venus）の `/bin/browser` の窓の live の home（host の headless と同じ崩れ）。
- `p059-20260928-survey-search-ours.png`（challenge の page、noscript の生の文字）・`…-search-chromium-sorry.png`（Chromium の `/sorry`）。

## 環境の記録

- worktree の guest の Venus: `build/ws035-sq-venus` を main の同名に link しないと、guest の Vulkan の device の作成が
  `VK_ERROR_INITIALIZATION_FAILED`（zdesktop・browser とも）。`zdesktop-guest.sh` の既定の `VENUS_RENDERER` が worktree の `build/` を指すため。
- worktree の Noct: main の archive と verified の record を `userland/base/noct/distfiles/` に複写し、patch の mtime を main に合わせた
  （ws073-p027 の手順）。`make toolchain` は LLVM を作り直さない。
- host に `python3-websocket` を入れた（CDP で Chromium を操作する調査用。`google-compare.py` は使わない）。

## 未実施・残り

- 結果の page の HTML と Chromium の描画（`/sorry` のため）。判定されない network から、または利用者の desktop の Chrome で保存した
  page で取る（google-goal.md §3、ユーザーの判断待ち）。
- challenge の VM が実行時に調べる指紋（canvas・WebGL 等）は静的には分からない。p065 で host で走らせて確かめる。
