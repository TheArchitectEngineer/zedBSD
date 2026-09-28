<!-- awesome-plan project=zedbsd record=ws074p067 -->

# ws074-p067: amazon.co.jp の調査（デモの目標の変更）

Phase ID: `ws074-p067`（WS074 の次の空き番号。2026-09-28 main の依頼）
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: p059（調査の道具）

## 範囲

2026-09-28 ユーザーの決定（デモの目標を Google から amazon.co.jp へ）を受け、p059 と同じ調査を amazon.co.jp のトップと検索の結果
（`/s?k=kei`）で行う: UA ごとに返る HTML、Chromium との比較、足りない機能、目標の版、Phase の列の組み直し。実装はしない。

## 手順（host、2026-09-28）

- curl で 4 つの UA（私たちの `browser/0.1 (Kei)`、Chrome、Chrome に Kei を足したもの、lynx）× 2 page（計 8 request、cookie の
  jar 付き）。保存は `build/ws074-amazon/`（tree に入れない）。私たちの UA の 2 page を capture として `top.html`・`search.html` に、
  script を除いた版を `*-noscript.html` に。
- 外の stylesheet（検索 11 本・トップ 8 本、`m.media-amazon.com`）を一度だけ取得し、inline と style 属性と合わせて property・
  display の値・at-rule・selector・関数を数えた（engine の `css/values.c` の名前と照合）。
- 比較: `plan/ws074/tests/live-compare.py`（p059 の `google-compare.py` を改名し、任意の URL と保存した file を取るようにした）で
  capture を私たちの `--render` と Chromium（私たちの UA、同じ font）で描いた。Chromium の `--blink-settings=scriptEnabled=false` は
  headless で screenshot を作らないので、script を除いた capture で JS なしの配置を見た。
- guest（Venus、p059 の image）の `/bin/browser` の窓で live のトップ。

## 結果

詳細は [amazon-goal.md](../amazon-goal.md)。要点:

- 私たちの正直な UA には challenge なしに desktop の版が返る（トップ 1.0 MB、検索 1.4 MB で結果 57 件が server で描かれている）。
  curl の Chrome の UA のトップは 202 と本文 0（AWS WAF の JS の challenge）。**目標は私たちの UA の版**。
- Chromium は script を除いた HTML でもほぼ同じ配置に描く: 配置は CSS で決まり、JS は付け足し。検索の form は普通の GET
  （`accept-charset=utf-8`、hidden・select・text・submit）で JS なしで検索できる。
- 今の browser: `<link rel=stylesheet>` を読まない（外の CSS 1〜2 MB が効かない）、noscript の style が生の文字、隠す要素が出る。
  白でない画素の一致（script を除いた capture）: トップ 11.56%、検索 2.42%。約 70 の JS の Uncaught（Date・RegExp）。
  検索の capture の `--render` は host で 3.7 s（外の CSS なし）。
- 必要な CSS（検索の page の 2 MB から）: 外の sheet と rule の索引、var（1600）・calc（1560）・@media（306）・!important（2115）、
  flex（494）・inline-block（245）・-webkit-box、border-radius（582）・box-shadow・opacity、:not（980）・:root・::before/::after・:has。
- Phase の列（amazon-goal.md §4）: p032（form、`<select>` の最小を足す）→ p068（外の stylesheet と rule の索引）→ p061（CSS の値）→
  p069（selector と pseudo-element）→ p035（flex の最小）→ p060 → p062 → p070（@font-face WOFF）→ p071（大きな page の速さ）→
  p037 → p072（grid の最小）→ p027・p065 の一部（JS）。Google 用の p063・p065・p066 はデモの列から外す。

## 写真（`/home/awe/zedBSD-rpi4/build/ws074-shots/`、host と QEMU の証拠。実機は未実施）

- `p067-20260928-amazon-top-noscript-before.png`・`…-search-noscript-before.png`: 私たち | Chromium | 違う画素（script を除いた capture）。
- `p067-20260928-amazon-top-before.png`・`…-search-before.png`: script 付きの capture で同じ。
- `p067-20260928-guest-amazon-top.png`: guest（Venus）の窓の live のトップ（host と同じ崩れ）。

## 未実施・残り

- Chromium の実 Chrome の UA での描画（AWS WAF の challenge を JS で通る）は取っていない（私たちの UA の版と比べれば足りる）。
- Amazon の JS が後から読む script（AUI の loader）の中身と構文の調査は JS の Phase で。
