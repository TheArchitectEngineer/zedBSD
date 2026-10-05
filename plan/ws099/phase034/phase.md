<!-- awesome-plan project=zedbsd record=ws099-p034 -->
# ws099-p034: 上部の system bar のデザインの調整（グループの pill と黒い地）

Parent: [WS099](../ws.md)
Status: planning（2026-10-06 ユーザーの追加。参考の画像 4 枚）
Disposition: normal

## 由来

ユーザー（2026-10-06）「Keilandコンポジタにコメントです。上部バーのデザインを調整したいです。添付が、1つめは上部バー左端でのアイコンのまとめかたと、黒いテクスチャをベースにするというアイディアです。2つめは、仮想デスクトップ切り替えの表示です。バーの中央部に置きます。3つめは、通知アイコン領域のアイコンのグルーピングです。4つめは時刻表示のグルーピングです。」

## 参考の画像（ユーザーの添付、Q1 が読んだ要点）

| 画像 | 部分 | 要点 |
| --- | --- | --- |
| [bar-1](images/bar-1.png) | 左端 | 左端に Kei の logo（青い葉の形）、細い区切りの線、その右に **起動中の app の icon を 1 つの暗い（黒い半透明の質感の）pill の中にまとめる**。icon は角の丸い四角の色の地に白い記号（P・歯車・画像・計算機・メモ・M）。bar の地は**黒い texture を base** にするアイディア |
| [bar-2](images/bar-2.png) | 中央 | **仮想 desktop の切り替えを bar の中央**に。小さな丸い横長の点（pill）が 4 つ並び、今の desktop だけが白い輪郭の大きめの pill。縮小の絵（今の thumbnail）ではない |
| [bar-3](images/bar-3.png) | 右 | 通知の icon の領域: 検索（虫眼鏡）・入力の方式（丸の中の A）・Wi-Fi・音量・電池を **1 つの pill にまとめる**。入力の方式の A は丸い地 |
| [bar-4](images/bar-4.png) | 右端 | 通知の icon の pill の右に、**時刻（Mon Oct 5 13:43）を別の pill** にまとめる。間に細い区切り |

## 範囲（案）

- bar の地を黒い texture（暗い glass）に。左・中央・右の要素を pill（角の丸い暗い半透明の帯）でまとめる。
- 左: logo ＋ 区切り ＋ app の icon の pill。中央: 仮想 desktop の点の pill（今の thumbnail の代わり、クリックで切り替え）。右: 状態の icon の pill ＋ 時刻の pill。
- light・dark（WS089 p017）、bar の高さ（ws099-p031）、Alt+Tab の preview（WS142・BUG-209）、USB の icon（ws132）、通知（WS156 の H7: bar の媒体の icon を通知に置き換え）との整合。
- app の icon は ws128-p012（デザインした icon への差し替え）と同じ絵を使う。

## 未決

- 黒い texture の具体（無地の暗い glass か、細かい質感の模様か）、light の外観でも黒い地にするか。
- 中央の点の pill で、desktop の中の窓の有無を点の明るさで示すか。
