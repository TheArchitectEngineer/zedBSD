<!-- awesome-plan project=zedbsd record=ws099 -->

# WS099: Keiland の compositor（zdesktop）のデモの基準

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: p001（基準の確定と QEMU の通しの試験）から。基準の案はユーザーの確認待ち
<!-- awesome-plan-current:end -->

## 目標（2026-09-30 ユーザー）

WS035 を閉じた後継。ユーザー:「WS099のゴールも、明確な達成基準がないような気がします。それはFGに入れて、WSでは、このソフトがこういう基準を
満たす、という明確なゴールを設定したいです。ソフトごとにそれをWSで作りましょう」→ デモの台本は fg010 に移した（master）。この WS の対象は
**compositor の zdesktop**（`/bin/wayland`、`userland/desktop/wayland/`）と、それが起こす greeter・session の遷移だけ。

## 達成基準（案、2026-09-30 main。ユーザーの確認待ち）

全て QEMU の Venus の自動の試験で確かめ、印の付いたものは 5330 の実機でも確かめる（実機）。

| # | 基準 | 確かめ方 |
| --- | --- | --- |
| C1 | 起動から greeter、login からデスクトップ、Log Out から greeter、Shut Down の各遷移で、黒い画面と文字の console が 0 枚（実機） | p126 の替わり目の試験、実機はユーザーの目視 |
| C2 | 窓の移動、四隅と四辺の resize、最大化と戻し、最小化と戻しが、どれも意図した位置と大きさになる | 自動の試験で位置と大きさを log と画面で比べる |
| C3 | 全画面・最大化を解いた窓は、title bar が system bar に重ならず、drag できる（BUG-114 の形） | 自動の試験（Notes の swipe → Esc を含む） |
| C4 | 窓を閉じたとき、同じ desktop の次の窓に keyboard の focus が移る。desktop の層には移らない | p137 の試験 |
| C5 | App Home と Wiseview の開閉が、窓 10 個でも途中で止まらない。開閉の最初の frame まで 100 ms 以内（実機） | 計測の log、実機は WS075 の計測 |
| C6 | 窓 10 個で、pointer の移動から表示まで中央値 50 ms 以内（実機）。compositor の GPU は WS075 の p023 が担う | WS075 の measure-apps.sh |
| C7 | すりガラスの上の文字の contrast が、既定と生成の 5 枚の壁紙の全てで 4.5:1 以上（WCAG AA） | 撮った画面の文字と背景の画素から計算する試験 |
| C8 | Terminal の窓は本体の四隅が直角、title bar は丸い。他の窓は両方丸い | p134 の試験 |
| C9 | zdesktop の回帰の試験（`plan/ws035/tests/zdesktop-p*.sh` のうち基準の一覧に載せたもの）が全て PASS（[BUG-115](../bugs/BUG-115.md) の p072 を含む） | 一括の回帰の script |
| C10 | 1 時間の連続の操作（窓の開閉を繰り返す試験）で zdesktop が落ちず、`ZWL ERROR` が 0 | 長時間の自動の試験 |

基準に無いものはこの WS で作らない（見つけたら Future Work か Bug Board）。期限は 2026-10-10 ごろ。

## 既知の項目

| 項目 | 基準 |
| --- | --- |
| 暗い壁紙の上のすりガラスの文字（旧 ws035-p135） | C7 |
| [BUG-115](../bugs/BUG-115.md): p072 の試験の失敗 | C9 |
| BUG-114（直った）・BUG-113（試験の誤り）の試験を回帰に入れる | C3・C4 |

## Phase（案）

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| ws099-p001 | 基準の確定（ユーザー）、C1〜C10 を確かめる一括の試験の script（既存の試験をまとめ、足りない C2・C5・C7・C10 を足す）、今の状態での実行と不足の一覧 | planning | — |
